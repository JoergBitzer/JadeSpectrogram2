// Overlap 50 % / 75 %: hop, level, number of columns, no allocation when switching, saved setting.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "alloc_count.h"
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
struct Run { size_t slices = 0, hop = 0; double levelDb = 0; };
static Run run(size_t overlap, size_t fft, double fs = 48000.0)
{
    JadeSpectrogramAudio algo; algo.setFFTSize(fft); algo.setOverlap(overlap); algo.prepareToPlay(fs, 512, 2);
    juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi; std::vector<float> s; Run r; long n = 0;
    const size_t bin = size_t(std::round(1000.0 / (fs / double(fft))));
    for (int b = 0; b < int(fs / 512); ++b) // 1 s
    {
        for (int k = 0; k < 512; ++k, ++n) { float v = float(0.5 * std::sin(2 * M_PI * 1000.0 * double(n) / fs)); buf.setSample(0, k, v); buf.setSample(1, k, v); }
        algo.processBlock(buf, midi);
        while (size_t m = algo.getNextMemSliceSize()) { r.hop = algo.getNextMemSliceInfo().hop; s.resize(m); algo.getMemSlice(s); ++r.slices; r.levelDb = s[bin]; }
    }
    return r;
}
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::printf("analysis:\n");
    for (size_t fft : {512, 2048, 8192})
    {
        Run a = run(0, fft), b = run(1, fft);
        std::printf("  FFT %4zu: 50 %%: hop %4zu, %3zu slices/s, sine %.2f dB | 75 %%: hop %4zu, %3zu slices/s, sine %.2f dB\n", fft, a.hop, a.slices, a.levelDb, b.hop, b.slices, b.levelDb);
        check(a.hop == fft / 2 && b.hop == fft / 4 && b.slices >= 2 * a.slices - 2 && b.slices <= 2 * a.slices + 2 && std::abs(a.levelDb - b.levelDb) < 0.05, "  hop N/4, twice the slices, same level");
    }
    {
        JadeSpectrogramAudio a; a.prepareToPlay(48000.0, 512, 2);
        juce::AudioBuffer<float> b2(2, 512); b2.clear(); juce::MidiBuffer midi; std::vector<float> s;
        for (int round = 0; round < 10; ++round)
        {
            a.setOverlap(size_t(round % 2)); a.setFFTSize(size_t(512) << (round % 5));
            for (int i = 0; i < 30; ++i) { t_count = true; a.processBlock(b2, midi); t_count = false; while (size_t m = a.getNextMemSliceSize()) { s.resize(m); a.getMemSlice(s); } }
        }
        check(g_allocs == 0, "no allocation while switching overlap and FFT size (10 switches)");
    }
    // GUI: display memory follows the hop, the time axis keeps 8 s
    std::printf("GUI:\n");
    auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc->setPlayConfigDetails(2, 2, 48000.0, 512); proc->prepareToPlay(48000.0, 512);
    auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
    auto& gui = ed->m_editor;
    juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer midi;
    auto feed = [&] { for (int i = 0; i < 20; ++i) proc->processBlock(buf, midi); gui.timerCallback(); };
    feed();
    const size_t w50 = gui.m_internalWidth; const float span50 = gui.timeSpan();
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(proc->m_parameterVTS->getParameter(JadeParamID::overlap));
    p->setValueNotifyingHost(p->convertTo0to1(1.f));
    feed();
    std::printf("  50 %%: %zu columns, %.2f s | 75 %%: %zu columns, %.2f s, hop %zu, combo '%s'\n", w50, span50, gui.m_internalWidth, gui.timeSpan(), gui.m_currentHop, gui.m_overlapCombo.getText().toRawUTF8());
    // 10 s memory: 10 s / hop columns, rounded down (468 and 937 at 48 kHz), so twice +-1
    check(std::abs(long(gui.m_internalWidth) - 2 * long(w50)) <= 1 && std::abs(gui.timeSpan() - span50) < 0.05f && gui.m_currentHop == 512 && gui.m_overlapCombo.getText() == "75 %", "75 %: twice the columns, same 10 s, combo shows 75 %");
    juce::MemoryBlock st; proc->getStateInformation(st);
    proc->editorBeingDeleted(ed); delete ed;
    auto proc2 = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc2->setPlayConfigDetails(2, 2, 48000.0, 512); proc2->prepareToPlay(48000.0, 512);
    proc2->setStateInformation(st.getData(), int(st.getSize()));
    proc2->processBlock(buf, midi);
    check(proc2->m_algo.m_activeOverlap == 1, "overlap 75 % restored from the saved state");
    std::printf("%s\n", fails ? "FAILED" : "all OK");
    return fails;
}
