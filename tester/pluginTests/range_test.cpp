// RangeSlider + RangeParameterBinding: thumbs, range drag, clamping, gestures, external changes.
// The frequency slider shows Hz on the scale of the axis (linear or logarithmic, since 1.11.1);
// the parameters store log(Hz).
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#define private public
#define protected public
#include "PluginProcessor.h"
#include "PluginEditor.h"
#undef private
#undef protected
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("  %-66s %s\n", what, ok ? "OK" : "FAIL"); fails += !ok; }
struct Gestures : juce::AudioProcessorListener {
    int begins = 0, ends = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++begins; }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++ends; }
};
static juce::MouseEvent ev(juce::Component& c, juce::Point<float> p, juce::Point<float> down)
{
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys::leftButtonModifier, 0.f, 0.f, 0.f, 0.f, 0.f,
                            &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false);
}
static void drag(juce::Slider& s, juce::Point<float> from, juce::Point<float> to, int steps = 8)
{
    s.mouseDown(ev(s, from, from));
    for (int i = 1; i <= steps; ++i) s.mouseDrag(ev(s, from + (to - from) * (float(i) / float(steps)), from));
    s.mouseUp(ev(s, to, from));
}
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc->setPlayConfigDetails(2, 2, 48000.0, 512); proc->prepareToPlay(48000.0, 512);
    auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
    auto& gui = ed->m_editor; gui.timerCallback();
    Gestures g; proc->addListener(&g);
    auto raw = [&](const char* id) { return proc->m_parameterVTS->getRawParameterValue(id)->load(); };
    auto& fs = gui.m_freqRangeSlider;
    std::printf("frequency slider %s, thumbs %.0f ... %.0f Hz\n", fs.getBounds().toString().toRawUTF8(), fs.getMinValue(), fs.getMaxValue());
    check(std::abs(std::exp(raw("MinFreq")) - 1.f) < 0.01f && std::abs(std::exp(raw("MaxFreq")) - 20000.f) < 1.f, "defaults 1 Hz ... 20 kHz");
    const float cx = 0.5f * float(fs.getWidth());
    // 1) upper thumb from the top down to the middle
    const float yMax = float(fs.getPositionOfValue(fs.getMaxValue())), yMid = 0.5f * float(fs.getHeight());
    drag(fs, {cx, yMax}, {cx, yMid});
    gui.timerCallback();
    const float maxAfter = std::exp(raw("MaxFreq"));
    std::printf("  upper thumb dragged: max %.0f Hz, min %.1f Hz\n", maxAfter, std::exp(raw("MinFreq")));
    check(maxAfter < 15000.f && maxAfter > 5000.f && std::abs(maxAfter - float(fs.getMaxValue())) < 1.f
          && std::abs(std::exp(raw("MinFreq")) - 1.f) < 0.01f, "upper thumb (linear axis) to the middle: only MaxFreq, about 10 kHz");
    check(g.begins == 2 && g.ends == 2, "one gesture (begin/end) per parameter for the drag");
    check(std::abs(gui.m_maxDisplayFreq - std::max(maxAfter, 500.f)) < 1.f, "display range follows (MaxFreq >= 500 Hz by its range)");
    // 2) log axis, 100 Hz ... 2000 Hz, drag between the thumbs upwards: range moves, ratio stays
    proc->m_parameterVTS->getParameter(JadeParamID::logFreqAxis)->setValueNotifyingHost(1.f); gui.timerCallback();
    auto* pmin = dynamic_cast<juce::RangedAudioParameter*>(proc->m_parameterVTS->getParameter("MinFreq"));
    auto* pmax = dynamic_cast<juce::RangedAudioParameter*>(proc->m_parameterVTS->getParameter("MaxFreq"));
    pmin->setValueNotifyingHost(pmin->convertTo0to1(std::log(100.f))); pmax->setValueNotifyingHost(pmax->convertTo0to1(std::log(2000.f)));
    gui.timerCallback();
    check(std::abs(fs.getMinValue() - 100.0) < 0.5 && std::abs(fs.getMaxValue() - 2000.0) < 1.0, "external parameter change moves the thumbs");
    const float yLo = float(fs.getPositionOfValue(fs.getMinValue())), yHi = float(fs.getPositionOfValue(fs.getMaxValue()));
    const int b0 = g.begins;
    drag(fs, {cx, 0.5f * (yLo + yHi)}, {cx, 0.5f * (yLo + yHi) - 60.f});
    gui.timerCallback();
    const float mn = std::exp(raw("MinFreq")), mx = std::exp(raw("MaxFreq"));
    std::printf("  range dragged up by 60 px: %.1f ... %.1f Hz, ratio %.3f (was 20.000)\n", mn, mx, mx / mn);
    check(mn > 100.f && mx > 2000.f && std::abs(mx / mn - 20.f) < 0.05f, "drag between the thumbs moves both, ratio kept");
    check(g.begins - b0 == 2, "one gesture pair for the range drag");
    // 3) range drag far beyond the top: stops at 20 kHz, width kept as far as possible
    drag(fs, {cx, 0.5f * (float(fs.getPositionOfValue(fs.getMinValue())) + float(fs.getPositionOfValue(fs.getMaxValue())))}, {cx, -2000.f});
    gui.timerCallback();
    std::printf("  range dragged beyond the top: %.1f ... %.1f Hz\n", std::exp(raw("MinFreq")), std::exp(raw("MaxFreq")));
    check(std::abs(std::exp(raw("MaxFreq")) - 20000.f) < 2.f && std::abs(std::exp(raw("MaxFreq")) / std::exp(raw("MinFreq")) - 20.f) < 0.05f, "stops at 20 kHz with the same ratio");
    // 4) no jump during a drag: external change while the mouse is down is applied after mouseUp
    const float yT = float(fs.getPositionOfValue(fs.getMaxValue()));
    fs.mouseDown(ev(fs, {cx, yT}, {cx, yT}));
    pmin->setValueNotifyingHost(pmin->convertTo0to1(std::log(30.f)));
    gui.timerCallback();
    const bool untouched = std::abs(fs.getMinValue() - 1000.0) < 5.0;
    fs.mouseUp(ev(fs, {cx, yT}, {cx, yT}));
    check(untouched && std::abs(fs.getMinValue() - 30.0) < 0.2, "external change during a drag waits for mouseUp");
    // 5) colour range: thumbs, palette and a full image rebuild
    auto& cs = gui.m_colorRangeSlider;
    const float yCmax = float(cs.getPositionOfValue(cs.getMaxValue()));
    drag(cs, {cx, yCmax}, {cx, yCmax + 100.f});
    gui.m_recomputeAll = false; gui.timerCallback();
    const float cmax = raw("MaxColor");
    std::printf("  colour: upper thumb dragged down: max %.1f dB\n", cmax);
    check(cmax < 10.f && cmax > -80.f, "colour thumb changes MaxColor");
    check(std::abs(gui.m_colorpalette.m_Max - cmax) < 0.01f, "palette uses the new colour range");
    proc->removeListener(&g);
    proc->editorBeingDeleted(ed); delete ed;
    std::printf("%s\n", fails ? "FAILED" : "all OK");
    return fails;
}
