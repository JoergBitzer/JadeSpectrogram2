// Synthetic host sequences: the grid (1/4 = beats, 4/4) against the true beat positions, sample-exact.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#include <cmath>
#include <functional>
#define private public
#define protected public
#include "PluginProcessor.h"
#include "PluginEditor.h"
#undef private
#undef protected
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("  %-72s %s\n", what, ok ? "OK" : "FAIL"); fails += !ok; }
static void setP(JadeSpectrogramAudioProcessor& p, const juce::String& id, float v)
{ auto* r = dynamic_cast<juce::RangedAudioParameter*>(p.m_parameterVTS->getParameter(id)); r->setValueNotifyingHost(r->convertTo0to1(v)); }
struct Host { bool playing = true; double ppq = 0, bpm = 120; bool looping = false; double l0 = 0, l1 = 0; };
// sequence: callback(blockIndex, host) may change the host before the block (locate, tempo, play)
static void runCase(const char* name, double fs, int block, int blocks, std::function<void(int, Host&)> events, bool expectNone, int fft = 2, int ov = 0)
{
    auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc->setPlayConfigDetails(2, 2, fs, block); proc->prepareToPlay(fs, block);
    auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
    auto& gui = ed->m_editor;
    setP(*proc, JadeParamID::bpmGrid, 1.f); setP(*proc, JadeParamID::bpmResolution, 2.f);
    setP(*proc, JadeParamID::fftSize, float(fft)); setP(*proc, JadeParamID::overlap, float(ov)); gui.timerCallback();
    juce::AudioBuffer<float> buf(2, block); buf.clear(); juce::MidiBuffer midi;
    Host h; std::vector<double> truth; double S = 0;
    for (int i = 0; i < blocks; ++i)
    {
        events(i, h);
        JadeSpectrogramAudio::HostPosition hp; hp.hasPpq = true; hp.ppq = h.ppq; hp.bpm = h.bpm; hp.hasBarStart = true; hp.barStartPpq = 0;
        hp.isPlaying = h.playing; hp.isLooping = h.looping; hp.loopStartPpq = h.l0; hp.loopEndPpq = h.l1;
        proc->m_algo.setHostPosition(hp); proc->m_algo.processBlock(buf, midi);
        if (i % 8 == 0) gui.timerCallback();
        // truth: beats (integer ppq) met by the played position, sample-exact, with the loop wrap
        if (h.playing) { const double r = h.bpm / 60.0 / fs; double p = h.ppq;
            if (std::abs(p - std::round(p)) < 1e-9) truth.push_back(S);
            for (int k = 0; k < block; ++k) { double q = p + r;
                if (h.looping && p < h.l1 && q >= h.l1) { const double s = S + k + (h.l1 - p) / r; q = h.l0 + (q - h.l1);
                    if (std::abs(h.l0 - std::round(h.l0)) < 1e-9) truth.push_back(s);
                    if (std::floor(q - 1e-12) > std::floor(h.l0 - 1e-12)) truth.push_back(s + (std::floor(q - 1e-12) - h.l0) / r); }
                else if (std::floor(q - 1e-12) > std::floor(p - 1e-12)) truth.push_back(S + k + (std::floor(q - 1e-12) - p) / r);
                p = q; }
            h.ppq = p; }
        S += block;
    }
    gui.timerCallback();
    for (auto& col : gui.m_displaymem) std::fill(col.begin(), col.end(), -80.f);
    gui.m_recomputeAll = true; gui.timerCallback();
    juce::Image img = gui.createComponentSnapshot(gui.getLocalBounds(), true, 1.f);
    const auto d = gui.displayArea(); const int y = d.getCentreY();
    std::vector<float> xs; for (int x = d.getX(); x < d.getRight(); ++x) { auto white = [&](int xx) { auto p = img.getPixelAt(xx, y); return std::min({p.getRed(), p.getGreen(), p.getBlue()}); };
        if (white(x) > 60) { float sw = 0, swx = 0; while (x < d.getRight() && white(x) > 25) { sw += white(x); swx += white(x) * (x + 0.5f); ++x; } xs.push_back(swx / sw); } }
    const double N = double(gui.m_newestColumn.endSample), span = gui.timeSpan();
    std::vector<float> ex; for (double s : truth) { const double x = d.getX() + (1.0 + (s - N) / fs / span) * d.getWidth(); if (x > d.getX() + 3 && x < d.getRight() - 3) ex.push_back(float(x)); }
    int missing = 0, extra = 0; float worst = 0;
    for (float e : ex) { float best = 1e9f; for (float x : xs) best = std::min(best, std::abs(x - e)); if (best > 2.f) ++missing; else worst = std::max(worst, best); }
    for (float x : xs) { float best = 1e9f; for (float e : ex) best = std::min(best, std::abs(x - e)); if (best > 2.f && x > d.getX() + 3 && x < d.getRight() - 3) ++extra; }
    std::printf("%-46s %3zu lines, %3zu true beats, %d missing, %d extra, deviation %.2f px\n", name, xs.size(), ex.size(), missing, extra, worst);
    if (expectNone) check(xs.empty(), name);
    else check(!ex.empty() && missing == 0 && extra == 0 && worst < 1.5f, name);
}
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 44100.0;
    // 1) stopped with odd block sizes (the 441 case): no lines
    for (int blk : {441, 256, 333})
        runCase(("stopped, block " + std::to_string(blk)).c_str(), fs, blk, int(11 * fs / blk), [](int, Host& h) { h.playing = false; h.ppq = 37.9876; }, true);
    // 2) stopped 5 s, then play from bar 1 (ppq 0)
    runCase("stop, then play from bar 1", fs, 256, int(11 * fs / 256), [&](int i, Host& h) { if (i < int(5 * fs / 256)) { h.playing = false; h.ppq = 13.37; } else if (i == int(5 * fs / 256)) { h.playing = true; h.ppq = 0.0; } }, false);
    // 3) locate jumps between blocks: forward and back, onto and off the beat
    runCase("locate jumps between blocks", fs, 256, int(11 * fs / 256), [&](int i, Host& h) { if (i == 400) h.ppq = 21.0; if (i == 900) h.ppq = 3.4567; if (i == 1400) h.ppq = 40.0; }, false);
    // 4) loops: on the bar, off the beat, short loop
    runCase("loop 0 ... 8, block 512 (Cubase)", fs, 512, int(11 * fs / 512), [&](int i, Host& h) { if (i == 0) { h.ppq = 2.45; h.looping = true; h.l0 = 0; h.l1 = 8; } }, false, 3, 0);
    runCase("loop 1.5 ... 6.25 (off the beat), block 256", fs, 256, int(11 * fs / 256), [&](int i, Host& h) { if (i == 0) { h.ppq = 1.5; h.looping = true; h.l0 = 1.5; h.l1 = 6.25; } }, false);
    runCase("loop 4 ... 6 (short), block 333, 75 %", fs, 333, int(11 * fs / 333), [&](int i, Host& h) { if (i == 0) { h.ppq = 4.0; h.looping = true; h.l0 = 4; h.l1 = 6; } }, false, 2, 1);
    // 5) tempo change while playing: no jump, lines follow
    runCase("tempo 120 -> 97 -> 150 while playing", fs, 256, int(11 * fs / 256), [&](int i, Host& h) { if (i == 600) h.bpm = 97; if (i == 1200) h.bpm = 150; }, false);
    std::printf(fails ? "FAILED\n" : "all OK\n");
    return fails ? 1 : 0;
}
