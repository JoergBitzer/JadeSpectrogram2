// Reference pitch A4 (2.3.0): readout note and cents, readout = overlay band for other references,
// overlay band edges follow the reference (pixels), drag box (gestures, Shift, wheel, limits), state.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#include <cmath>
#define private public
#define protected public
#include "PluginProcessor.h"
#include "PluginEditor.h"
#undef private
#undef protected
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("  %-70s %s\n", what, ok ? "OK" : "FAIL"); fails += !ok; }
static void setP(JadeSpectrogramAudioProcessor& p, const juce::String& id, float v)
{ auto* r = dynamic_cast<juce::RangedAudioParameter*>(p.m_parameterVTS->getParameter(id)); r->setValueNotifyingHost(r->convertTo0to1(v)); }
static float raw(JadeSpectrogramAudioProcessor& p, const juce::String& id) { return p.m_parameterVTS->getRawParameterValue(id)->load(); }
static juce::MouseEvent ev(juce::Component& c, juce::Point<float> p, juce::Point<float> down, juce::ModifierKeys mods)
{ return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.f, 0.f, 0.f, 0.f, 0.f, &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false); }
struct Gestures : juce::AudioProcessorListener {
    int begins = 0, ends = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++begins; }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++ends; }
};
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc->setPlayConfigDetails(2, 2, 48000.0, 512); proc->prepareToPlay(48000.0, 512);
    auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
    auto& gui = ed->m_editor; gui.timerCallback();
    const auto d = gui.displayArea(); const float H = float(d.getHeight());
    check(raw(*proc, JadeParamID::refPitch) == 440.f && gui.m_refPitchValue.getText() == "440.0" && gui.m_refPitchValue.isVisible() && gui.m_refPitchValue.isEditable(),
          "default A4 = 440.0 Hz, box visible and editable");
    auto readoutAt = [&](float f, double& fShown) { const int y = juce::roundToInt(gui.frequencyToY(f, H)); gui.setLabelText(d.getX() + 50, d.getY() + y);
        fShown = gui.yToFrequency(float(y), H); return gui.m_readoutText; };
    setP(*proc, JadeParamID::logFreqAxis, 1.f); setP(*proc, "MinFreq", std::log(200.f)); setP(*proc, "MaxFreq", std::log(2000.f)); gui.timerCallback();
    // 1) note and cents of the readout = 1200 log2(f/ref) rounded, for several references
    for (float ref : {440.f, 442.f, 415.f, 466.2f, 380.f, 480.f})
    {
        setP(*proc, JadeParamID::refPitch, ref); gui.timerCallback();
        int wrong = 0, checked = 0;
        for (float f = 210.f; f < 1900.f; f *= 1.013f)
        {
            double fs; const juce::String t = readoutAt(f, fs);
            const double semis = 12.0 * std::log2(fs / double(gui.m_refPitch)) + 69.0; const int n = int(std::floor(semis + 0.5));
            const int ct = juce::roundToInt(100.0 * (semis - n));
            const juce::String sign = ct > 0 ? juce::String("+") : (ct < 0 ? juce::String("-") : juce::String(juce::CharPointer_UTF8("\xc2\xb1")));
            const juce::String want = juce::MidiMessage::getMidiNoteName(n, true, true, 4) + " " + sign + juce::String(std::abs(ct)) + " ct";
            const juce::String got = t.fromFirstOccurrenceOf("Hz | ", false, false).upToFirstOccurrenceOf(" |", false, false);
            ++checked; if (got != want) { if (++wrong < 3) std::printf("    ref %.1f, %.1f Hz: '%s' expected '%s'\n", ref, fs, got.toRawUTF8(), want.toRawUTF8()); }
        }
        std::printf("A4 = %.1f Hz: %d readouts, %d wrong, example '%s'\n", ref, checked, wrong, readoutAt(ref, *new double).toRawUTF8());
        check(wrong == 0 && std::abs(gui.m_refPitch - ref) < 0.01, "readout: note and cents relative to the reference");
    }
    { setP(*proc, JadeParamID::refPitch, 442.f); gui.timerCallback(); double fs; readoutAt(440.f, fs);
      std::printf("A4 = 442, readout at %.2f Hz: '%s'\n", fs, gui.m_readoutText.toRawUTF8());
      const int want = juce::roundToInt(1200.0 * std::log2(fs / 442.0)); // pixel row near 440 Hz
      check(want < -4 && gui.m_readoutText.contains("A4 " + juce::String(want) + " ct"), "A4 = 442: about 440 Hz reads as A4 with negative cents"); }
    // 2) overlay band edges in pixels: shade changes on an empty display lie on the quarter-tone
    //    boundaries of the reference (here 452.9 Hz = +50 ct, the edges move by half a semitone)
    setP(*proc, JadeParamID::keyboardOverlay, 1.f);
    for (float ref : {440.f, 452.9f})
    {
        setP(*proc, JadeParamID::refPitch, ref); gui.timerCallback();
        for (auto& col : gui.m_displaymem) std::fill(col.begin(), col.end(), -80.f);
        gui.m_recomputeAll = true; gui.timerCallback();
        juce::Image img = gui.createComponentSnapshot(gui.getLocalBounds(), true, 1.0f);
        const int x = d.getX() + d.getWidth() / 3;
        int edges = 0, onRef = 0, onOther = 0; const float other = ref == 440.f ? 452.9f : 440.f;
        for (int y = d.getY() + 3; y < d.getBottom() - 3; ++y)
        {
            const float b0 = img.getPixelAt(x, y - 1).getPerceivedBrightness(), b1 = img.getPixelAt(x, y + 1).getPerceivedBrightness();
            if (std::abs(b1 - b0) < 0.02f || std::abs(img.getPixelAt(x, y).getPerceivedBrightness() - b0) < 0.005f) continue;
            ++edges; const float f = gui.yToFrequency(float(y - d.getY()) + 0.5f, H);
            auto dist = [&](float r) { const double q = 12.0 * std::log2(f / r) + 0.5; const double frac = q - std::round(q); // distance to a boundary in semitones
                return std::abs(frac) * (H / (12.f * std::log2(2000.f / 200.f))); };            // ... in pixels
            if (dist(ref) < 1.5f) ++onRef; if (dist(other) < 1.5f) ++onOther;
            ++y; // one edge per transition
        }
        std::printf("overlay A4 = %.1f: %d shade edges, %d on its boundaries, %d on those of %.1f\n", ref, edges, onRef, onOther, other);
        check(edges > 10 && onRef == edges && onOther < edges / 3, "overlay band edges on the quarter-tone boundaries of the reference");
    }
    setP(*proc, JadeParamID::keyboardOverlay, 0.f);
    // 3) drag box: up 50 px = +5 Hz in one gesture; Shift down 30 px = -0.3 Hz; wheel; limits
    setP(*proc, JadeParamID::refPitch, 440.f); gui.timerCallback();
    { auto& b = gui.m_refPitchValue; const auto c = b.getLocalBounds().getCentre().toFloat(); Gestures gl; proc->addListener(&gl);
      b.mouseDown(ev(b, c, c, juce::ModifierKeys::leftButtonModifier));
      for (int dy = 1; dy <= 50; ++dy) b.mouseDrag(ev(b, c.translated(0, float(-dy)), c, juce::ModifierKeys::leftButtonModifier));
      b.mouseUp(ev(b, c.translated(0, -50), c, juce::ModifierKeys()));
      std::printf("drag up 50 px: %.2f Hz, gestures %d/%d, box '%s'\n", raw(*proc, JadeParamID::refPitch), gl.begins, gl.ends, b.getText().toRawUTF8());
      check(std::abs(raw(*proc, JadeParamID::refPitch) - 445.f) < 0.01f && gl.begins == 1 && gl.ends == 1 && b.getText() == "445.0", "drag up 50 px: 445.0 Hz in one gesture");
      const auto shift = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
      b.mouseDown(ev(b, c, c, shift)); b.mouseDrag(ev(b, c.translated(0, 30), c, shift)); b.mouseUp(ev(b, c.translated(0, 30), c, juce::ModifierKeys()));
      check(std::abs(raw(*proc, JadeParamID::refPitch) - 444.7f) < 0.01f && b.getText() == "444.7", "Shift drag down 30 px: -0.3 Hz");
      juce::MouseWheelDetails wd{}; wd.deltaY = -1.f; b.mouseWheelMove(ev(b, c, c, juce::ModifierKeys()), wd);
      check(std::abs(raw(*proc, JadeParamID::refPitch) - 443.7f) < 0.01f, "mouse wheel down: -1 Hz");
      proc->removeListener(&gl);
      setP(*proc, JadeParamID::refPitch, 300.f); gui.timerCallback(); const float lo = raw(*proc, JadeParamID::refPitch);
      setP(*proc, JadeParamID::refPitch, 600.f); gui.timerCallback(); const float hi = raw(*proc, JadeParamID::refPitch);
      check(lo == 380.f && hi == 480.f && gui.m_refPitchValue.getText() == "480.0", "range 380 ... 480 Hz"); }
    // 4) state
    setP(*proc, JadeParamID::refPitch, 432.f);
    juce::MemoryBlock state; proc->getStateInformation(state);
    auto proc2 = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc2->setPlayConfigDetails(2, 2, 48000.0, 512); proc2->prepareToPlay(48000.0, 512);
    proc2->setStateInformation(state.getData(), int(state.getSize()));
    auto* ed2 = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc2->createEditorIfNeeded()); ed2->m_editor.timerCallback();
    check(std::abs(ed2->m_editor.m_refPitch - 432.0) < 0.01 && ed2->m_editor.m_refPitchValue.getText() == "432.0", "restored: A4 = 432.0 Hz");
    check(!dynamic_cast<juce::RangedAudioParameter*>(proc->m_parameterVTS->getParameter(JadeParamID::refPitch))->isAutomatable(), "not automatable");
    std::printf(fails ? "FAILED\n" : "all OK\n");
    return fails ? 1 : 0;
}
