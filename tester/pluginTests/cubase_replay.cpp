// Replays the Cubase playhead log (BLOCK lines) into the plugin and checks the bar lines (grid
// resolution 1 = bars only) against the true bar positions from the log: every bar inside the
// window drawn (also at every loop jump), nothing while stopped, no extra lines.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <regex>
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
struct Blk { int n; bool playing, looping; double ppq, bar, bpm, l0, l1; };
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    std::vector<Blk> blocks; std::ifstream in(argv[1]); std::string line;
    auto get = [](const std::string& l, const char* k) { auto p = l.find(std::string(" ") + k + "="); return l.substr(p + std::strlen(k) + 2, l.find(' ', p + 1) == std::string::npos ? std::string::npos : l.find(' ', p + 1) - p - std::strlen(k) - 2); };
    while (std::getline(in, line)) if (line.rfind("BLOCK ", 0) == 0) {
        Blk b; b.n = std::stoi(get(line, "n")); b.playing = get(line, "playing") == "1"; b.looping = get(line, "looping") == "1";
        b.ppq = std::stod(get(line, "ppq")); b.bar = std::stod(get(line, "bar")); b.bpm = std::stod(get(line, "bpm"));
        auto loop = get(line, "loop"); b.l0 = std::stod(loop.substr(0, loop.find(".."))); b.l1 = std::stod(loop.substr(loop.find("..") + 2)); blocks.push_back(b); }
    const double fs = 44100.0;
    // true bar positions (sample numbers) from the log: a bar wherever the played position meets a multiple of 4
    std::vector<double> trueBars; { double S0 = 0; for (auto& b : blocks) { if (b.playing) { const double r = b.bpm / 60.0 / fs;
        for (int i = 0; i < b.n; ++i) { double p0 = b.ppq + i * r, p1 = b.ppq + (i + 1) * r; // sample i -> i+1
            auto wrapv = [&](double p) { return (b.looping && b.ppq < b.l1 && p >= b.l1) ? b.l0 + std::fmod(p - b.l1, b.l1 - b.l0) : p; };
            double w0 = wrapv(p0), w1 = wrapv(p1);
            if (i == 0 && std::fmod(w0, 4.0) < 1e-9) trueBars.push_back(S0);
            if (w1 < w0) { if (std::fmod(b.l0, 4.0) < 1e-9) trueBars.push_back(S0 + i + (b.l1 - p0) / r); } // loop jump: the loop start
            else if (std::floor(w1 / 4.0 - 1e-12) > std::floor(w0 / 4.0 - 1e-12)) trueBars.push_back(S0 + i + (std::floor(w1 / 4.0 - 1e-12) * 4.0 - w0) / r); } }
        S0 += b.n; } }
    std::printf("%zu blocks from the Cubase log, %zu true bar lines\n", blocks.size(), trueBars.size());
    struct Cfg { int fft, ov; } cfgs[] = {{3, 0}, {2, 0}, {2, 1}, {4, 1}};
    for (auto c : cfgs)
    {
        auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
        proc->setPlayConfigDetails(2, 2, fs, 512); proc->prepareToPlay(fs, 512);
        auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
        auto& gui = ed->m_editor;
        setP(*proc, JadeParamID::bpmGrid, 1.f); setP(*proc, JadeParamID::bpmResolution, 0.f);
        setP(*proc, JadeParamID::fftSize, float(c.fft)); setP(*proc, JadeParamID::overlap, float(c.ov)); gui.timerCallback();
        juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer midi;
        int snaps = 0, missing = 0, extra = 0, stoppedLines = -1; float worst = 0;
        double S0 = 0;
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            auto& b = blocks[i];
            JadeSpectrogramAudio::HostPosition hp; hp.hasPpq = true; hp.ppq = b.ppq; hp.bpm = b.bpm; hp.hasBarStart = true; hp.barStartPpq = b.bar;
            hp.isPlaying = b.playing; hp.isLooping = b.looping; hp.loopStartPpq = b.l0; hp.loopEndPpq = b.l1;
            proc->m_algo.setHostPosition(hp); proc->m_algo.processBlock(buf, midi); S0 += b.n;
            if (i % 8 == 0) gui.timerCallback();
            const bool snapStopped = i == 740, snapPlaying = b.playing && i % 150 == 0 && i > 746 + 900; // after 10 s of play
            if (!snapStopped && !snapPlaying) continue;
            for (auto& col : gui.m_displaymem) std::fill(col.begin(), col.end(), -80.f);
            gui.m_recomputeAll = true; gui.timerCallback();
            juce::Image img = gui.createComponentSnapshot(gui.getLocalBounds(), true, 1.f);
            const auto d = gui.displayArea(); const int y = d.getCentreY();
            std::vector<float> xs; for (int x = d.getX(); x < d.getRight(); ++x) { auto white = [&](int xx) { auto p = img.getPixelAt(xx, y); return std::min({p.getRed(), p.getGreen(), p.getBlue()}); };
                if (white(x) > 60) { float sw = 0, swx = 0; while (x < d.getRight() && white(x) > 25) { sw += white(x); swx += white(x) * (x + 0.5f); ++x; } xs.push_back(swx / sw); } }
            if (snapStopped) { stoppedLines = int(xs.size()); continue; }
            ++snaps;
            const double N = double(gui.m_newestColumn.endSample), span = gui.timeSpan();
            std::vector<float> ex; for (double s : trueBars) { const double x = d.getX() + (1.0 + (s - N) / fs / span) * d.getWidth(); if (x > d.getX() + 3 && x < d.getRight() - 3) ex.push_back(float(x)); }
            for (float e : ex) { float best = 1e9f; for (float x : xs) best = std::min(best, std::abs(x - e)); if (best > 2.f) ++missing; else worst = std::max(worst, best); }
            for (float x : xs) { float best = 1e9f; for (float e : ex) best = std::min(best, std::abs(x - e)); if (best > 2.f && x > d.getX() + 3 && x < d.getRight() - 3) ++extra; }
        }
        std::printf("FFT %d, overlap %s: stopped %d lines; %d pictures while playing: %d bar lines missing, %d extra, largest deviation %.2f px\n",
                    512 << c.fft, c.ov ? "75%" : "50%", stoppedLines, snaps, missing, extra, worst);
        check(stoppedLines == 0, "stopped: no lines");
        check(snaps > 3 && missing == 0 && extra == 0 && worst < 1.5f, "playing with loop jumps: every bar line, also at each loop start, no extra");
    }
    std::printf(fails ? "FAILED\n" : "all OK\n");
    return fails ? 1 : 0;
}
