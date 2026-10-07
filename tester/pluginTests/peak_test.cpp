// 2.4.0: peak line of the readout (accuracy of the interpolated maximum, no peak without a real
// maximum in the band), white dot at the peak, harmonic cursor (Shift) lines at k f0.
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
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000.0;
    auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc->setPlayConfigDetails(2, 2, fs, 512); proc->prepareToPlay(fs, 512);
    auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
    auto& gui = ed->m_editor;
    const auto d = gui.displayArea(); const float H = float(d.getHeight());
    double phase[3] = {0, 0, 0};
    auto feed = [&](std::initializer_list<double> freqs, double seconds) {
        juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
        for (int b = 0; b < int(seconds * fs / 512); ++b) {
            for (int i = 0; i < 512; ++i) { double v = 0; int j = 0; for (double f : freqs) { v += 0.25 * std::sin(phase[j]); phase[j] += 2.0 * juce::MathConstants<double>::pi * f / fs; ++j; }
                buf.setSample(0, i, float(v)); buf.setSample(1, i, float(v)); }
            proc->m_algo.processBlock(buf, midi);
            if (b % 10 == 0) gui.timerCallback();
        }
        gui.timerCallback(); };
    auto at = [&](float f) { const int y = juce::roundToInt(gui.frequencyToY(f, H)); const int x = d.getX() + d.getWidth() - 40;
        gui.m_mousePos = {x, d.getY() + y}; gui.m_mouseInDisplay = gui.setLabelText(x, d.getY() + y); };
    struct Case { int fftIndex; double f; const char* name; } cases[] = {{4, 441.3, "8192, 441.3 Hz"}, {3, 441.3, "4096, 441.3 Hz"}, {4, 97.5, "8192, 97.5 Hz (band < 2 bins)"}, {2, 3010.0, "2048, 3010 Hz"}};
    for (auto& c : cases)
    {
        setP(*proc, JadeParamID::fftSize, float(c.fftIndex));
        setP(*proc, "MinFreq", std::log(float(c.f) / 1.5f)); setP(*proc, "MaxFreq", std::log(float(c.f) * 1.5f)); gui.timerCallback(); // a few px per cent
        feed({c.f}, 1.5);
        const double bin = fs / double(512 << c.fftIndex);
        // mouse 30 ct above the tone: inside its note band
        at(float(c.f * std::pow(2.0, 30.0 / 1200.0)));
        const double err = gui.m_peakFreq - c.f, errCt = 1200.0 * std::log2(gui.m_peakFreq / c.f);
        std::printf("%-30s mouse '%s'\n%-30s peak  '%s' (error %.3f Hz = %.3f bins = %.2f ct)\n", c.name, gui.m_readoutText.toRawUTF8(), "", gui.m_peakText.toRawUTF8(), err, err / bin, errCt);
        check(gui.m_peakValid && std::abs(err) < 0.06 * bin && gui.m_peakText.startsWith("peak "), "peak found, interpolated frequency within 6 % of a bin");
        check(gui.m_readoutText.startsWith(juce::String(juce::roundToInt(gui.m_mouseFreq)) + " Hz"), "first line unchanged: the mouse position");
    }
    // no real maximum in the band: mouse 70 ct above 441.3 Hz at 8192 (only the falling skirt in range)
    setP(*proc, JadeParamID::fftSize, 4.f); setP(*proc, "MinFreq", std::log(300.f)); setP(*proc, "MaxFreq", std::log(650.f)); gui.timerCallback(); feed({441.3}, 1.5);
    at(float(441.3 * std::pow(2.0, 70.0 / 1200.0)));
    std::printf("70 ct above the tone: peak valid %d\n", int(gui.m_peakValid));
    check(!gui.m_peakValid, "no peak when the maximum is at the edge of the band (only the skirt)");
    at(600.f); check(!gui.m_peakValid, "no peak in a band with only the noise floor");
    // dot: drawn at the peak (white), not drawn without peak
    feed({441.3}, 1.5); setP(*proc, "MinFreq", std::log(200.f)); setP(*proc, "MaxFreq", std::log(1200.f)); gui.timerCallback();
    at(float(441.3 * std::pow(2.0, 20.0 / 1200.0)));
    { juce::Image img = gui.createComponentSnapshot(gui.getLocalBounds(), true, 1.0f);
      const int py = d.getY() + juce::roundToInt(gui.frequencyToY(gui.m_peakFreq, H)); const int px = gui.m_mousePos.x;
      // the vertical crosshair line passes the dot centre: check left and right of it
      const auto c1 = img.getPixelAt(px + 2, py), c2 = img.getPixelAt(px - 2, py), off = img.getPixelAt(px + 2, py + 8);
      std::printf("dot at (%d, %d): pixels %s %s, 8 px below %s\n", px, py, c1.toDisplayString(false).toRawUTF8(), c2.toDisplayString(false).toRawUTF8(), off.toDisplayString(false).toRawUTF8());
      check(c1.getPerceivedBrightness() > 0.8f && c2.getPerceivedBrightness() > 0.8f && off.getPerceivedBrightness() < 0.8f, "white dot at the peak"); }
    // harmonic cursor: Shift + mouse at the 220 Hz tone: dashed lines at 440, 660, ... Hz, numbered
    feed({220.0}, 1.5); setP(*proc, "MinFreq", std::log(1.f)); setP(*proc, "MaxFreq", std::log(4000.f)); gui.timerCallback();
    at(221.f);
    auto rowBright = [&](const juce::Image& img, int y) { int n = 0; for (int x = d.getX() + 25; x < d.getX() + 200; ++x) if (img.getPixelAt(x, y).getPerceivedBrightness() > 0.45f) ++n; return n; };
    gui.m_showHarmonics = false;
    juce::Image off = gui.createComponentSnapshot(gui.getLocalBounds(), true, 1.0f);
    gui.m_showHarmonics = true;
    juce::Image on = gui.createComponentSnapshot(gui.getLocalBounds(), true, 1.0f);
    int found = 0, expected = 0, between = 0;
    for (int k = 2; double(k) * gui.m_peakFreq < 4000.0; ++k)
    {
        const int y = d.getY() + int(std::floor(gui.frequencyToY(float(k) * gui.m_peakFreq, H)));
        ++expected; if (std::max({rowBright(on, y), rowBright(on, y + 1), rowBright(on, y - 1)}) > 40 && rowBright(off, y) < 10) ++found;
        const int ym = d.getY() + juce::roundToInt(gui.frequencyToY((float(k) + 0.5f) * gui.m_peakFreq, H)); if (rowBright(on, ym) > 40) ++between;
    }
    std::printf("harmonics of %.2f Hz: %d of %d lines found, %d bright rows between them\n", gui.m_peakFreq, found, expected, between);
    check(gui.m_peakValid && found == expected && expected >= 15 && between == 0, "Shift: dashed lines at k f0 (linear axis, 1 ... 4000 Hz)");
    // log axis: the lines stop where they would come closer than 4 px
    setP(*proc, JadeParamID::logFreqAxis, 1.f); setP(*proc, "MinFreq", std::log(20.f)); setP(*proc, "MaxFreq", std::log(20000.f)); gui.timerCallback(); at(221.f);
    { int n = 0; float last = 1e9f, minGap = 1e9f; for (int k = 2; double(k) * gui.m_peakFreq < 20000.0; ++k) { float y = gui.frequencyToY(float(k) * gui.m_peakFreq, H);
        if (std::abs(last - y) < 4.f) break; if (k > 2) minGap = std::min(minGap, std::abs(last - y)); last = y; ++n; }
      std::printf("log axis: %d harmonic lines, smallest spacing %.1f px\n", n, minGap);
      check(n > 5 && n < 60 && minGap >= 4.f, "log axis: harmonic lines only while at least 4 px apart"); }
    std::printf(fails ? "FAILED\n" : "all OK\n");
    return fails ? 1 : 0;
}
