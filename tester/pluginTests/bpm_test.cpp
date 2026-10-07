// BPM grid (synced to the host): beat position of each slice, lines at the beats on the time axis,
// tempo change, resolutions in note values (1 = bars ... 1/16), 3/4 time, stopped transport, jumps,
// no host tempo, grid off, state; no allocation in the audio thread.
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
static void check(bool ok, const char* what) { std::printf("  %-68s %s\n", what, ok ? "OK" : "FAIL"); fails += !ok; }
static void setP(JadeSpectrogramAudioProcessor& p, const juce::String& id, float v)
{ auto* r = dynamic_cast<juce::RangedAudioParameter*>(p.m_parameterVTS->getParameter(id)); r->setValueNotifyingHost(r->convertTo0to1(v)); }
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000.0;
    // 1) beat position of each slice
    std::printf("slice beat positions (120 BPM from ppq 0):\n");
    for (int hostBlock : {512, 480, 1000})
    {
        JadeSpectrogramAudio a; a.prepareToPlay(fs, hostBlock, 2);
        juce::AudioBuffer<float> buf(2, hostBlock); buf.clear(); juce::MidiBuffer midi; std::vector<float> s;
        long long fed = 0, sliceEnd = 0; double maxErr = 0; int slices = 0;
        for (int b = 0; b < 400; ++b)
        {
            if (b == 200) { a.setFFTSize(8192); } // switch: the block restarts at the next host block
            JadeSpectrogramAudio::HostPosition hp; hp.hasPpq = true; hp.bpm = 120.0; hp.ppq = double(fed) * 2.0 / fs; hp.hasBarStart = true; hp.barStartPpq = 0.0; hp.isPlaying = true;
            a.setHostPosition(hp);
            if (b == 200) sliceEnd = fed;
            a.processBlock(buf, midi); fed += hostBlock;
            while (size_t m = a.getNextMemSliceSize())
            {
                SliceInfo info = a.getNextMemSliceInfo(); s.resize(m); a.getMemSlice(s);
                sliceEnd += (long long) info.hop;
                maxErr = std::max(maxErr, std::abs(info.ppq - double(sliceEnd) * 2.0 / fs)); ++slices;
            }
        }
        std::printf("    host block %4d: %d slices, largest error %.2e beats\n", hostBlock, slices, maxErr);
        check(maxErr < 1e-9 && slices > 0, "    slice ppq = position of its last sample (also across an FFT switch)");
    }
    // 2) GUI: line positions against the time axis
    auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc->setPlayConfigDetails(2, 2, fs, 512); proc->prepareToPlay(fs, 512);
    auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
    auto& gui = ed->m_editor;
    setP(*proc, JadeParamID::bpmGrid, 1.f);
    juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer midi;
    double ppq = 0.0; long long n = 0; int num = 4;
    auto run = [&](double seconds, double bpm, bool hasTempo, bool playing) {
        for (int b = 0; b < int(seconds * fs / 512); ++b) {
            JadeSpectrogramAudio::HostPosition hp; hp.hasPpq = hasTempo; hp.bpm = bpm; hp.ppq = ppq; hp.hasBarStart = true; hp.barStartPpq = 0.0; hp.numerator = num; hp.isPlaying = playing;
            proc->m_algo.setHostPosition(hp);
            t_count = true; proc->m_algo.processBlock(buf, midi); t_count = false;
            if (playing) ppq += 512.0 * bpm / 60.0 / fs; n += 512;
            if (b % 20 == 0) gui.timerCallback();
        }
        gui.timerCallback(); };
    auto linesInRow = [&](std::vector<float>& xs) {
        for (auto& col : gui.m_displaymem) std::fill(col.begin(), col.end(), -80.f);
        gui.m_recomputeAll = true; gui.timerCallback();
        juce::Image img = gui.createComponentSnapshot(gui.getLocalBounds(), true, 1.0f);
        const auto d = gui.displayArea(); const int y = d.getY() + d.getHeight() / 2;
        xs.clear(); int x = d.getX();
        // grid lines are white: all three channels well above the dark blue background
        auto white = [&](int xx) { auto c = img.getPixelAt(xx, y); return float(std::min({c.getRed(), c.getGreen(), c.getBlue()})); };
        while (x < d.getRight()) { if (white(x) > 60.f) { float sw = 0, swx = 0; while (x < d.getRight() && white(x) > 25.f) { float w = white(x); sw += w; swx += w * (float(x) + 0.5f); ++x; } xs.push_back(swx / sw); } else ++x; } };
    run(10.2, 120.0, true, true);
    std::vector<float> xs; linesInRow(xs);
    // expected: every beat p inside the window: time relative to the newest slice end
    const auto d = gui.displayArea(); const double span = gui.timeSpan();
    const double newest = gui.m_newestColumn.ppq;
    std::vector<float> ex; for (double p = std::ceil(newest - span * 2.0); p <= newest; p += 1.0) { const double t = -(newest - p) / 2.0; const double x = d.getX() + (1.0 + t / span) * d.getWidth(); if (x > d.getX() + 2 && x < d.getRight() - 2) ex.push_back(float(x)); }
    float worst = 0; size_t matched = 0; for (float e : ex) { float best = 1e9f; for (float x : xs) best = std::min(best, std::abs(x - e)); if (best < 3.f) ++matched; worst = std::max(worst, best); }
    std::printf("GUI, 120 BPM, whole window: %zu lines found, %zu beats expected, %zu matched, largest deviation %.2f px, %s, allocations %ld\n", xs.size(), ex.size(), matched, worst, gui.m_bpmValue.getText().toRawUTF8(), g_allocs);
    check(matched == ex.size() && xs.size() == ex.size() && worst < 1.5f, "a line at every beat, at its time on the axis (< 1.5 px)");
    check(gui.m_bpmValue.getText() == "120 BPM" && gui.m_bpmResolutionCombo.isVisible() && gui.m_bpmValue.isVisible(), "title bar: '120 BPM', resolution box visible with the grid");
    check(g_allocs == 0, "no allocation on the audio thread");
    check(gui.m_bpmResolutionCombo.getText() == "1/4", "default resolution 1/4 note (= every beat in 4/4)");
    // resolutions in note values, counted from the bar start
    auto checkStep = [&](double step, const char* what) {
        std::vector<float> e; for (double p = std::ceil((newest - span * 2.0) / step) * step; p <= newest; p += step) { const double t = -(newest - p) / 2.0; const double x = d.getX() + (1.0 + t / span) * d.getWidth(); if (x > d.getX() + 2 && x < d.getRight() - 2) e.push_back(float(x)); }
        float w = 0; size_t m = 0; for (float ee : e) { float best = 1e9f; for (float x : xs) best = std::min(best, std::abs(x - ee)); if (best < 3.f) ++m; w = std::max(w, best); }
        std::printf("%s: %zu lines, %zu expected, %zu matched, largest deviation %.2f px\n", what, xs.size(), e.size(), m, w);
        check(m == e.size() && xs.size() == e.size() && w < 1.5f, what); };
    setP(*proc, JadeParamID::bpmResolution, 0.f); gui.timerCallback(); linesInRow(xs); checkStep(4.0, "4/4, resolution 1: bar lines only (every 4 beats)");
    setP(*proc, JadeParamID::bpmResolution, 1.f); gui.timerCallback(); linesInRow(xs); checkStep(2.0, "4/4, resolution 1/2: every half note");
    setP(*proc, JadeParamID::bpmResolution, 3.f); gui.timerCallback(); linesInRow(xs); checkStep(0.5, "4/4, resolution 1/8: every eighth note");
    setP(*proc, JadeParamID::bpmResolution, 2.f); gui.timerCallback();
    // tempo change 120 -> 90 in the middle of the window: lines follow the actual beats
    run(5.0, 90.0, true, true); linesInRow(xs);
    { const double newest2 = gui.m_newestColumn.ppq; std::vector<float> ex2;
      // beats of the last 5 s at 90 BPM, before that at 120 BPM
      const double ppqAtChange = newest2 - 5.0 * 1.5;
      for (double p = std::ceil(ppqAtChange - (span - 5.0) * 2.0); p <= newest2; p += 1.0) { double tt = p >= ppqAtChange ? -(newest2 - p) / 1.5 : -5.0 - (ppqAtChange - p) / 2.0; double x = d.getX() + (1.0 + tt / span) * d.getWidth(); if (x > d.getX() + 2 && x < d.getRight() - 2) ex2.push_back(float(x)); }
      float w2 = 0; size_t m2 = 0; for (float e : ex2) { float best = 1e9f; for (float x : xs) best = std::min(best, std::abs(x - e)); if (best < 3.f) ++m2; w2 = std::max(w2, best); }
      std::printf("GUI, 120 -> 90 BPM: %zu lines, %zu expected, %zu matched, largest deviation %.2f px, %s\n", xs.size(), ex2.size(), m2, w2, gui.m_bpmValue.getText().toRawUTF8());
      check(m2 == ex2.size() && xs.size() == ex2.size() && w2 < 1.5f && gui.m_bpmValue.getText() == "90 BPM", "tempo change: lines on the beats of both tempi"); }
    // resolution 1/16 note: four lines per beat (where they are at least 5 px apart)
    setP(*proc, JadeParamID::bpmResolution, 4.f); gui.timerCallback(); linesInRow(xs);
    std::printf("resolution 1/16: %zu lines\n", xs.size());
    check(xs.size() > 3 * ex.size(), "resolution 1/16 note: about four times the lines");
    // 3/4: resolution 1 gives a line every 3 beats, 1/4 every beat
    { num = 3; setP(*proc, JadeParamID::bpmResolution, 2.f); run(10.2, 120.0, true, true); linesInRow(xs);
      const double nw = gui.m_newestColumn.ppq;
      auto cnt = [&](double step) { size_t c = 0; for (double p = std::ceil((nw - span * 2.0) / step) * step; p <= nw; p += step) { const double x = d.getX() + (1.0 - (nw - p) / 2.0 / span) * d.getWidth(); if (x > d.getX() + 2 && x < d.getRight() - 2) ++c; } return c; };
      std::printf("3/4, resolution 1/4: %zu lines, %zu beats expected\n", xs.size(), cnt(1.0));
      check(xs.size() == cnt(1.0), "3/4, resolution 1/4: every beat");
      setP(*proc, JadeParamID::bpmResolution, 0.f); gui.timerCallback(); linesInRow(xs);
      std::printf("3/4, resolution 1: %zu lines, %zu bars expected\n", xs.size(), cnt(3.0));
      check(xs.size() == cnt(3.0), "3/4, resolution 1: a line every 3 beats (bars)");
      num = 4; }
    setP(*proc, JadeParamID::bpmResolution, 2.f);
    // transport stopped (position does not advance) and a jump: no lines there
    run(10.2, 120.0, true, false); linesInRow(xs);
    std::printf("transport stopped for the whole window: %zu lines\n", xs.size());
    check(xs.empty(), "stopped transport: no lines");
    run(3.0, 120.0, true, true); ppq += 57.3; run(3.0, 120.0, true, true); ppq -= 20.0; run(3.0, 120.0, true, true); linesInRow(xs);
    { const double newest3 = gui.m_newestColumn.ppq; size_t expectedMax = size_t(9.0 * 2.0) + 2; std::printf("with two jumps (forward, back/loop): %zu lines (at most %zu beats in the played 9 s)\n", xs.size(), expectedMax); juce::ignoreUnused(newest3);
      check(xs.size() <= expectedMax && xs.size() >= 15, "jumps: no burst of lines at the jumps"); }
    // no host tempo (Standalone): no lines, 'no tempo'
    run(10.2, 0.0, false, false); linesInRow(xs);
    check(xs.empty() && gui.m_bpmValue.getText() == "no tempo", "no host tempo: no lines, 'no tempo'");
    // grid off: box and value hidden; state saved
    gui.m_bpmButton.onClick();
    check(!gui.m_bpmGrid && !gui.m_bpmResolutionCombo.isVisible() && !gui.m_bpmValue.isVisible(), "grid off: resolution box and BPM value hidden");
    setP(*proc, JadeParamID::bpmGrid, 1.f); setP(*proc, JadeParamID::bpmResolution, 3.f);
    juce::MemoryBlock st; proc->getStateInformation(st);
    proc->editorBeingDeleted(ed); delete ed;
    auto p2 = std::make_unique<JadeSpectrogramAudioProcessor>(); p2->setStateInformation(st.getData(), int(st.getSize()));
    auto* ed2 = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(p2->createEditorIfNeeded());
    check(ed2->m_editor.m_bpmGrid && ed2->m_editor.m_bpmResolutionCombo.getText() == "1/8", "grid and resolution restored from the saved state");
    p2->editorBeingDeleted(ed2); delete ed2;
    std::printf("%s\n", fails ? "FAILED" : "all OK");
    return fails ? 1 : 0;
}
