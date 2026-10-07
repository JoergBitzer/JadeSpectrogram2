// Keyboard overlay: band edges, readout note = band note, fading of thin bands, button, state.
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
static void check(bool ok, const char* what) { std::printf("  %-66s %s\n", what, ok ? "OK" : "FAIL"); fails += !ok; }
static void setP(JadeSpectrogramAudioProcessor& p, const juce::String& id, float v)
{ auto* r = dynamic_cast<juce::RangedAudioParameter*>(p.m_parameterVTS->getParameter(id)); r->setValueNotifyingHost(r->convertTo0to1(v)); }
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc->setPlayConfigDetails(2, 2, 48000.0, 512); proc->prepareToPlay(48000.0, 512);
    auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
    auto& gui = ed->m_editor; gui.timerCallback();
    const auto d = gui.displayArea(); const float H = float(d.getHeight());
    const juce::MidiMessage mm;
    struct Cfg { const char* name; bool log; float fmin, fmax; } cfgs[] = {{"log, full", true, 1, 20000}, {"linear, 100 ... 1000 Hz", false, 100, 1000}, {"linear, full", false, 1, 20000}};
    for (auto& c : cfgs)
    {
        setP(*proc, JadeParamID::logFreqAxis, c.log ? 1.f : 0.f);
        setP(*proc, "MinFreq", std::log(c.fmin)); setP(*proc, "MaxFreq", std::log(c.fmax)); gui.timerCallback();
        // every note band whose height is >= 3 px: readout at the centre and just inside both edges = that note
        int checked = 0, wrong = 0;
        const float fmin = gui.yToFrequency(H, H), fmax = gui.yToFrequency(0.f, H);
        for (int n = 0; n <= 135; ++n)
        {
            const float fc = 440.f * std::pow(2.f, float(n - 69) / 12.f), q = std::pow(2.f, 1.f / 24.f);
            if (fc / q < fmin || fc * q > fmax) continue;
            const float ylo = gui.frequencyToY(fc / q, H), yhi = gui.frequencyToY(fc * q, H);
            if (ylo - yhi < 3.f) continue;
            for (float y : {0.5f * (ylo + yhi), yhi + 1.f, ylo - 1.f})
            {
                gui.setLabelText(d.getX() + 50, d.getY() + int(y));
                const juce::String want = mm.getMidiNoteName(n, true, true, 4);
                const juce::String got = gui.m_readoutText.fromFirstOccurrenceOf("Hz | ", false, false).upToFirstOccurrenceOf(" ", false, false); // the note name before the cents (since 2.3.0)
                ++checked; if (got != want) { ++wrong; if (wrong < 4) std::printf("    band %s at y %.1f: readout %s\n", want.toRawUTF8(), y, got.toRawUTF8()); }
            }
        }
        std::printf("  %-26s %d positions checked, %d mismatches\n", c.name, checked, wrong);
        check(checked > 0 && wrong <= checked / 50, "readout note = band note (centre and near both edges)");
    }
    // fading on the linear full view: nothing drawn where a semitone is < 1.5 px (100 Hz), drawn at 10 kHz
    setP(*proc, JadeParamID::logFreqAxis, 0.f); setP(*proc, "MinFreq", 0.f); setP(*proc, "MaxFreq", std::log(20000.f)); gui.timerCallback();
    for (auto& col : gui.m_displaymem) std::fill(col.begin(), col.end(), -80.f);
    gui.m_recomputeAll = true; gui.timerCallback();
    setP(*proc, JadeParamID::keyboardOverlay, 0.f); gui.timerCallback(); juce::Image off = gui.createComponentSnapshot(gui.getLocalBounds(), true, 1.0f);
    setP(*proc, JadeParamID::keyboardOverlay, 1.f); gui.timerCallback(); juce::Image on = gui.createComponentSnapshot(gui.getLocalBounds(), true, 1.0f);
    auto diffRow = [&](float f) { int y = d.getY() + int(gui.frequencyToY(f, H)); int nd = 0; for (int x = d.getX() + 60; x < d.getRight(); x += 7) nd += (on.getPixelAt(x, y) != off.getPixelAt(x, y)); return nd; };
    const int at100 = diffRow(100.f), at10k = diffRow(10000.f);
    std::printf("  linear full view: changed pixels in a row at 100 Hz: %d, at 10 kHz: %d\n", at100, at10k);
    check(at100 == 0 && at10k > 50, "thin bands (100 Hz) faded out, wide bands (10 kHz) drawn");
    check(gui.m_keyboardButton.getToggleState() && gui.m_keyboardButton.getParentComponent() == ed, "button in the title bar, highlighted while on");
    gui.m_keyboardButton.onClick(); // triggerClick() is asynchronous
    std::printf(fails ? "FAILED\n" : "all OK\n");
    return fails ? 1 : 0;
}
