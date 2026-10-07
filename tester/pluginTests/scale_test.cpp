// GUI scaling: layout and slider positions at several window sizes.
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
static juce::MouseEvent ev(juce::Component& c, juce::Point<float> p, juce::Point<float> down)
{ return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys::leftButtonModifier, 0.f, 0.f, 0.f, 0.f, 0.f, &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false); }
static void drag(juce::Slider& s, juce::Point<float> from, juce::Point<float> to)
{ s.mouseDown(ev(s, from, from)); for (int i = 1; i <= 8; ++i) s.mouseDrag(ev(s, from + (to - from) * (float(i) / 8.f), from)); s.mouseUp(ev(s, to, from)); }
static void setP(JadeSpectrogramAudioProcessor& p, const juce::String& id, float v)
{ auto* r = dynamic_cast<juce::RangedAudioParameter*>(p.m_parameterVTS->getParameter(id)); r->setValueNotifyingHost(r->convertTo0to1(v)); }
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc->setPlayConfigDetails(2, 2, 48000.0, 512); proc->prepareToPlay(48000.0, 512);
    auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
    auto& gui = ed->m_editor; gui.timerCallback();
    auto raw = [&](const juce::String& id) { return proc->m_parameterVTS->getRawParameterValue(id)->load(); };
    auto& fs = gui.m_freqRangeSlider; const float cx = 0.5f * float(fs.getWidth());
    auto yOfProp = [&](double pr) { return float(fs.getPositionOfValue(fs.proportionOfLengthToValue(pr))); };
    for (bool logAxis : {false, true})
    {
        std::printf("%s axis:\n", logAxis ? "log" : "linear");
        setP(*proc, JadeParamID::logFreqAxis, logAxis ? 1.f : 0.f);
        setP(*proc, "MinFreq", std::log(1.f)); setP(*proc, "MaxFreq", std::log(20000.f)); gui.timerCallback();
        // lower thumb from the bottom to a quarter of the slider
        drag(fs, {cx, yOfProp(0.0)}, {cx, yOfProp(0.25)}); gui.timerCallback();
        const double mn = std::exp(raw("MinFreq")), expect = logAxis ? 20.0 * std::pow(1000.0, 0.25) : 1.0 + 0.25 * 19999.0;
        std::printf("    lower thumb to 25 %%: MinFreq %.0f Hz (scale predicts %.0f Hz), display from %.0f Hz\n", mn, expect, gui.m_minDisplayFreq);
        check(std::abs(mn - expect) / expect < 0.03 && std::abs(gui.m_minDisplayFreq - float(mn)) < 1.f, logAxis ? "log: 25 % of the slider = 112 Hz, display follows" : "linear: 25 % of the slider = 5000 Hz, display follows");
        // range drag
        setP(*proc, "MinFreq", std::log(logAxis ? 100.f : 2000.f)); setP(*proc, "MaxFreq", std::log(logAxis ? 2000.f : 6000.f)); gui.timerCallback();
        const double a0 = std::exp(raw("MinFreq")), b0 = std::exp(raw("MaxFreq"));
        const float ym = 0.5f * (float(fs.getPositionOfValue(fs.getMinValue())) + float(fs.getPositionOfValue(fs.getMaxValue())));
        drag(fs, {cx, ym}, {cx, ym - 50.f}); gui.timerCallback();
        const double a1 = std::exp(raw("MinFreq")), b1 = std::exp(raw("MaxFreq"));
        std::printf("    range drag: %.0f ... %.0f Hz -> %.0f ... %.0f Hz (width %.0f -> %.0f Hz, ratio %.2f -> %.2f)\n", a0, b0, a1, b1, b0 - a0, b1 - a1, b0 / a0, b1 / a1);
        check(a1 > a0 && (logAxis ? std::abs(b1 / a1 - b0 / a0) < 0.05 : std::abs((b1 - a1) - (b0 - a0)) < 5.0), logAxis ? "log: range drag keeps the ratio" : "linear: range drag keeps the width in Hz");
    }
    // a low saved lower limit (141 Hz) sits near the bottom on the linear scale
    setP(*proc, JadeParamID::logFreqAxis, 0.f); setP(*proc, "MinFreq", std::log(141.f)); setP(*proc, "MaxFreq", std::log(20000.f)); gui.timerCallback();
    std::printf("141 Hz on the linear slider: at %.1f %% of the height (log slider before: 50 %%)\n", 100.0 * fs.valueToProportionOfLength(fs.getMinValue()));
    check(fs.valueToProportionOfLength(fs.getMinValue()) < 0.01, "linear: 141 Hz near the bottom, as in the display");
    // averaging scale
    std::printf("averaging:\n");
    auto& as = gui.m_averagingSlider;
    juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer midi;
    for (int cfg = 0; cfg < 3; ++cfg)
    {
        const int fftIdx[] = {0, 2, 4}; const int ov[] = {1, 0, 0};
        setP(*proc, JadeParamID::fftSize, float(fftIdx[cfg])); setP(*proc, JadeParamID::overlap, float(ov[cfg]));
        for (int i = 0; i < 40; ++i) proc->processBlock(buf, midi);
        gui.timerCallback();
        const double hopMs = 1000.0 * double(gui.m_currentHop) / 48000.0;
        const double v19 = as.proportionOfLengthToValue(0.019), v21 = as.proportionOfLengthToValue(0.021), v100 = as.proportionOfLengthToValue(1.0);
        std::printf("    FFT %4d / %s: hop %.2f ms; 1.9 %% -> %s, 2.1 %% -> %.1f ms ('%s'), 100 %% -> %.0f ms\n", 512 << fftIdx[cfg], ov[cfg] ? "75 %" : "50 %", hopMs,
                    as.getTextFromValue(v19).toRawUTF8(), v21, as.getTextFromValue(v21).toRawUTF8(), v100);
        check(v19 == 0.0 && std::abs(v21 - 2.0 * hopMs) / (2.0 * hopMs) < 0.02 && std::abs(v100 - 2000.0) < 0.5 && as.getTextFromValue(v19) == "off", "    lowest 2 % off, then from 2 hops to 2000 ms");
    }
    setP(*proc, JadeParamID::averaging, 300.f); gui.timerCallback();
    check(std::abs(as.getValue() - 300.0) < 0.5 && as.getTextFromValue(as.getValue()) == "300 ms", "saved 300 ms stays 300 ms (slider value and text)");
    // drag the averaging slider to its left end: off
    const float ay = 0.5f * float(as.getHeight());
    drag(as, {float(as.getPositionOfValue(as.getValue())), ay}, {1.f, ay}); gui.timerCallback();
    std::printf("    dragged to the left end: parameter %.1f ms ('%s')\n", raw(JadeParamID::averaging), as.getTextFromValue(as.getValue()).toRawUTF8());
    check(raw(JadeParamID::averaging) < 0.01f, "drag to the left end writes 0 (off)");
    proc->editorBeingDeleted(ed); delete ed;
    std::printf("%s\n", fails ? "FAILED" : "all OK");
    return fails;
}
