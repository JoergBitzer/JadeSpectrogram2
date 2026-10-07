// Time zoom: visible part, time axis labels, scroll and Fix mode.
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
    auto& gui = ed->m_editor;
    juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer midi;
    for (int i = 0; i < 10; ++i) proc->processBlock(buf, midi);
    gui.timerCallback();
    const size_t W = gui.m_internalWidth;
    std::printf("time window %.2f s, %zu columns (hop %zu)\n", gui.timeSpan(), W, gui.m_currentHop);
    check(std::abs(gui.timeSpan() - 10.f) < 0.05f, "time window 10 s");
    const auto d = gui.displayArea();
    // one bright column c: where is it drawn, what does the time axis say there, what does the readout show?
    auto measure = [&](size_t c, float s, float e, bool fix, const char* name)
    {
        if (gui.m_fixedDisplay != fix) gui.runClicked();
        setP(*proc, JadeParamID::timeStart, s); setP(*proc, JadeParamID::timeEnd, e);
        for (auto& col : gui.m_displaymem) std::fill(col.begin(), col.end(), -80.f);
        // Scroll mode: the image column ww shows memory column (writepos + ww) mod W (oldest left)
        size_t mem = fix ? c : (gui.m_displaymem_writepos + c) % W;
        std::fill(gui.m_displaymem[mem].begin(), gui.m_displaymem[mem].end(), 20.f);
        gui.m_recomputeAll = true; gui.timerCallback();
        juce::Image img = gui.createComponentSnapshot(gui.getLocalBounds(), true, 1.0f);
        const int y = d.getY() + d.getHeight() / 2;
        const float dark = img.getPixelAt(d.getX() + 2, d.getY() + 3).getPerceivedBrightness();
        double sw = 0, swx = 0;
        for (int x = d.getX(); x < d.getRight(); ++x) { double L = img.getPixelAt(x, y).getPerceivedBrightness() - dark; if (fix && img.getPixelAt(x, y).getRed() > 200 && img.getPixelAt(x, y).getGreen() < 60) continue; if (L > 0.05) { sw += L; swx += L * (x + 0.5); } }
        const double xc = swx / sw;
        const double fracC = (double(c) + 0.5) / double(W);
        const double xExp = d.getX() + (fracC - s) / (e - s) * d.getWidth();
        const double span = gui.timeSpan(), t0 = fix ? 0.0 : -span;
        const double tDrawn = t0 + (gui.m_timeStart + (gui.m_timeEnd - gui.m_timeStart) * (xc - d.getX()) / d.getWidth()) * span;
        gui.setLabelText(int(xc), y);
        std::printf("  %-26s column %4zu (t = %+6.2f s) drawn at x=%.1f, expected %.1f, time axis there %+6.2f s, readout '%s'\n",
                    name, c, t0 + fracC * span, xc, xExp, tDrawn, gui.m_readoutText.toRawUTF8());
        return std::abs(xc - xExp) < 1.0 && gui.m_readoutText.contains("20.0 dB");
    };
    check(measure(W * 3 / 4, 0.f, 1.f, false, "Scroll, full"), "Scroll, no zoom: column at its time, readout finds it");
    check(measure(W * 3 / 4, 0.5f, 1.f, false, "Scroll, last 5 s"), "Scroll, zoom to the last 5 s");
    check(measure(W * 3 / 4 + 3, 0.7f, 0.8f, false, "Scroll, 1 s window"), "Scroll, zoom to 1 s");
    check(measure(W / 3, 0.2f, 0.6f, true, "Fix, 2 ... 6 s"), "Fix mode (sweep time), zoom 2 ... 6 s");
    // minimum width: 0.5 ... 0.5 is widened to 2 %
    setP(*proc, JadeParamID::timeStart, 0.5f); setP(*proc, JadeParamID::timeEnd, 0.5f); gui.timerCallback();
    std::printf("  zoom 0.5 ... 0.5 becomes %.3f ... %.3f\n", gui.m_timeStart, gui.m_timeEnd);
    check(std::abs(gui.m_timeEnd - gui.m_timeStart - 0.02f) < 1e-4f, "minimum zoom width 2 % (0.2 s)");
    // saved with the project
    setP(*proc, JadeParamID::timeStart, 0.25f); setP(*proc, JadeParamID::timeEnd, 0.75f);
    juce::MemoryBlock st; proc->getStateInformation(st);
    proc->editorBeingDeleted(ed); delete ed;
    auto p2 = std::make_unique<JadeSpectrogramAudioProcessor>(); p2->setStateInformation(st.getData(), int(st.getSize()));
    check(std::abs(p2->m_parameterVTS->getRawParameterValue(JadeParamID::timeStart)->load() - 0.25f) < 1e-4f
          && std::abs(p2->m_parameterVTS->getRawParameterValue(JadeParamID::timeEnd)->load() - 0.75f) < 1e-4f, "time zoom restored from the saved state");
    std::printf("%s\n", fails ? "FAILED" : "all OK");
    return fails;
}
