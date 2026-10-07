// Free tempo of the BPM grid (2.2.0): switch sync -> free continues the host grid, own tempo by
// dragging the BPM box (gestures, Shift fine, not in sync), Alt+click anchor, works paused and
// without host tempo, host tempo display follows the host while stopped/paused, state saved.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "alloc_count.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
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
static juce::MouseEvent ev(juce::Component& c, juce::Point<float> p, juce::Point<float> down, juce::ModifierKeys mods, int clicks = 1)
{ return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.f, 0.f, 0.f, 0.f, 0.f, &c, &c, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), clicks, false); }
struct Gestures : juce::AudioProcessorListener {
    int begins = 0, ends = 0, changes = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { ++changes; }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++begins; }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++ends; }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000.0;
    auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc->setPlayConfigDetails(2, 2, fs, 512); proc->prepareToPlay(fs, 512);
    auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
    auto& gui = ed->m_editor;
    setP(*proc, JadeParamID::bpmGrid, 1.f);
    juce::AudioBuffer<float> buf(2, 512); buf.clear(); juce::MidiBuffer midi;
    double ppq = 0.3; // the host grid does not start at sample 0
    auto run = [&](double seconds, double bpm, bool hasTempo, bool playing) {
        for (int b = 0; b < int(seconds * fs / 512); ++b) {
            JadeSpectrogramAudio::HostPosition hp; hp.hasPpq = hasTempo; hp.bpm = bpm; hp.ppq = ppq; hp.hasBarStart = true; hp.barStartPpq = 0.0; hp.isPlaying = playing;
            proc->m_algo.setHostPosition(hp);
            t_count = true; proc->m_algo.processBlock(buf, midi); t_count = false;
            if (playing) ppq += 512.0 * bpm / 60.0 / fs;
            if (b % 20 == 0) gui.timerCallback();
        }
        gui.timerCallback(); };
    auto linesInRow = [&](std::vector<float>& xs) {
        for (auto& col : gui.m_displaymem) std::fill(col.begin(), col.end(), -80.f);
        gui.m_recomputeAll = true; gui.timerCallback();
        juce::Image img = gui.createComponentSnapshot(gui.getLocalBounds(), true, 1.0f);
        const auto d = gui.displayArea(); const int y = d.getY() + d.getHeight() / 2;
        xs.clear(); int x = d.getX();
        auto white = [&](int xx) { auto c = img.getPixelAt(xx, y); return float(std::min({c.getRed(), c.getGreen(), c.getBlue()})); };
        while (x < d.getRight()) { if (white(x) > 60.f) { float sw = 0, swx = 0; while (x < d.getRight() && white(x) > 25.f) { float w = white(x); sw += w; swx += w * (float(x) + 0.5f); ++x; } xs.push_back(swx / sw); } else ++x; } };
    const auto d = gui.displayArea();
    const double span = gui.timeSpan();
    // expected x of lines every stepQ quarter notes at bpm from a bar line at sample anchor
    auto expected = [&](double anchor, double bpm, double stepQ) {
        std::vector<float> e; const double N = double(gui.m_newestColumn.endSample), spq = 60.0 * fs / bpm;
        const double step = stepQ * spq;
        for (double s = anchor + std::ceil((N - span * fs - anchor) / step) * step; s <= N; s += step) {
            const double x = d.getX() + (1.0 + (s - N) / fs / span) * d.getWidth();
            if (x > d.getX() + 2 && x < d.getRight() - 2) e.push_back(float(x)); }
        return e; };
    auto match = [&](const std::vector<float>& xs, const std::vector<float>& e, const char* what) {
        float w = 0; size_t m = 0; for (float ee : e) { float best = 1e9f; for (float x : xs) best = std::min(best, std::abs(x - ee)); if (best < 3.f) ++m; w = std::max(w, best); }
        std::printf("%s: %zu lines, %zu expected, %zu matched, largest deviation %.2f px, box '%s'\n", what, xs.size(), e.size(), m, w, gui.m_bpmValue.getText().toRawUTF8());
        check(!e.empty() && m == e.size() && xs.size() == e.size() && w < 1.5f, what); };

    // 1) sync, 120 BPM: lines on the host beats; box not editable
    run(10.2, 120.0, true, true);
    std::vector<float> xsSync; linesInRow(xsSync);
    check(!gui.m_tempoFree && !gui.m_bpmValue.isEditable() && gui.m_tempoSyncButton.isVisible() && gui.m_bpmValue.getText() == "120 BPM", "sync: chain button visible, '120 BPM', box not editable");
    { const auto box = gui.m_bpmValue.getLocalBounds().getCentre().toFloat(); Gestures gl; proc->addListener(&gl);
      gui.m_bpmValue.mouseDown(ev(gui.m_bpmValue, box, box, juce::ModifierKeys::leftButtonModifier));
      gui.m_bpmValue.mouseDrag(ev(gui.m_bpmValue, box.translated(0, -40), box, juce::ModifierKeys::leftButtonModifier));
      gui.m_bpmValue.mouseUp(ev(gui.m_bpmValue, box.translated(0, -40), box, juce::ModifierKeys()));
      proc->removeListener(&gl);
      check(gl.changes == 0 && raw(*proc, JadeParamID::freeBpm) == 120.f, "sync: dragging the box changes nothing"); }

    // 1b) synced + paused: host tempo changed -> grid at the new tempo from the last host bar line
    { gui.pauseClicked(); run(0.5, 120.0, true, false); std::vector<float> xp; linesInRow(xp);
      float w = 0; for (size_t i = 0; i < std::min(xp.size(), xsSync.size()); ++i) w = std::max(w, std::abs(xp[i] - xsSync[i]));
      check(xp.size() == xsSync.size() && w < 0.01f, "synced, paused, tempo unchanged: the recorded grid");
      const double anchor = double(gui.lastBarLineSample(gui.m_newestColumn));
      run(0.5, 100.0, true, false); linesInRow(xp);
      match(xp, expected(anchor, 100.0, 1.0), "synced, paused, host 120 -> 100 BPM: grid at 100 BPM from the last bar line");
      check(gui.m_bpmValue.getText() == "100 BPM", "synced, paused: box shows 100 BPM");
      run(0.5, 120.0, true, false); linesInRow(xp);
      w = 0; for (size_t i = 0; i < std::min(xp.size(), xsSync.size()); ++i) w = std::max(w, std::abs(xp[i] - xsSync[i]));
      check(xp.size() == xsSync.size() && w < 0.01f, "synced, paused, host back to 120: the recorded grid again");
      gui.pauseClicked(); }

    // 2) switch to free: same lines as the host grid (tempo and bar position taken over)
    setP(*proc, JadeParamID::freeBpm, 77.f);
    gui.m_tempoSyncButton.onClick();
    std::vector<float> xs; linesInRow(xs);
    { float w = 0; for (size_t i = 0; i < std::min(xs.size(), xsSync.size()); ++i) w = std::max(w, std::abs(xs[i] - xsSync[i]));
      std::printf("free after switch: %zu lines (sync %zu), largest difference %.2f px, free BPM %.1f, box '%s'\n", xs.size(), xsSync.size(), w, raw(*proc, JadeParamID::freeBpm), gui.m_bpmValue.getText().toRawUTF8());
      check(gui.m_tempoFree && gui.m_bpmValue.isEditable() && raw(*proc, JadeParamID::freeBpm) == 120.f && raw(*proc, JadeParamID::tempoFree) > 0.5f, "free: host tempo taken over, box editable");
      check(xs.size() == xsSync.size() && w < 1.01f, "free: the grid continues the host grid (no jump)"); }
    { // anchor on a host bar line: ppq at the anchor is a multiple of 4
      const auto& c = gui.m_newestColumn; const double ppqAnchor = c.ppq - double(c.endSample - gui.m_freeAnchorSample) * 120.0 / 60.0 / fs;
      std::printf("anchor at host ppq %.6f\n", ppqAnchor);
      check(std::abs(ppqAnchor / 4.0 - std::round(ppqAnchor / 4.0)) < 1e-4, "anchor = a bar line of the host grid"); }

    // 3) drag the box up 40 px: +10 BPM, one gesture; Shift: 0.02 BPM per pixel
    { const auto box = gui.m_bpmValue.getLocalBounds().getCentre().toFloat(); Gestures gl; proc->addListener(&gl);
      gui.m_bpmValue.mouseDown(ev(gui.m_bpmValue, box, box, juce::ModifierKeys::leftButtonModifier));
      for (int dy = 1; dy <= 40; ++dy) gui.m_bpmValue.mouseDrag(ev(gui.m_bpmValue, box.translated(0, float(-dy)), box, juce::ModifierKeys::leftButtonModifier));
      gui.m_bpmValue.mouseUp(ev(gui.m_bpmValue, box.translated(0, -40), box, juce::ModifierKeys()));
      proc->removeListener(&gl);
      std::printf("drag up 40 px: %.1f BPM, gestures %d/%d, changes %d, box '%s'\n", raw(*proc, JadeParamID::freeBpm), gl.begins, gl.ends, gl.changes, gui.m_bpmValue.getText().toRawUTF8());
      check(std::abs(raw(*proc, JadeParamID::freeBpm) - 130.f) < 0.01f && gl.begins == 1 && gl.ends == 1 && gl.changes > 0 && gui.m_bpmValue.getText() == "130 BPM", "drag up 40 px: 130 BPM in one gesture");
      const auto shift = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
      gui.m_bpmValue.mouseDown(ev(gui.m_bpmValue, box, box, shift));
      gui.m_bpmValue.mouseDrag(ev(gui.m_bpmValue, box.translated(0, 25), box, shift));
      gui.m_bpmValue.mouseUp(ev(gui.m_bpmValue, box.translated(0, 25), box, juce::ModifierKeys()));
      std::printf("Shift drag down 25 px: %.2f BPM, box '%s'\n", raw(*proc, JadeParamID::freeBpm), gui.m_bpmValue.getText().toRawUTF8());
      check(std::abs(raw(*proc, JadeParamID::freeBpm) - 129.5f) < 0.01f && gui.m_bpmValue.getText() == "129.5 BPM", "Shift drag down 25 px: -0.5 BPM, shown with one decimal");
      juce::MouseWheelDetails wd{}; wd.deltaY = 1.f;
      gui.m_bpmValue.mouseWheelMove(ev(gui.m_bpmValue, box, box, juce::ModifierKeys()), wd);
      check(std::abs(raw(*proc, JadeParamID::freeBpm) - 130.5f) < 0.01f, "mouse wheel: +1 BPM");
      setP(*proc, JadeParamID::freeBpm, 400.f); gui.timerCallback();
      check(raw(*proc, JadeParamID::freeBpm) == 300.f, "range limit 300 BPM"); }

    // 4) free 90 BPM, display paused and transport stopped: lines from the anchor at 90 BPM
    setP(*proc, JadeParamID::freeBpm, 90.f); gui.timerCallback();
    linesInRow(xs); match(xs, expected(double(gui.m_freeAnchorSample), 90.0, 1.0), "free 90 BPM: lines every beat from the anchor");
    gui.pauseClicked(); run(2.0, 120.0, true, false); setP(*proc, JadeParamID::freeBpm, 100.f); gui.timerCallback();
    linesInRow(xs); match(xs, expected(double(gui.m_freeAnchorSample), 100.0, 1.0), "paused + stopped, 100 BPM: lines follow the new tempo");
    setP(*proc, JadeParamID::bpmResolution, 0.f); gui.timerCallback();
    linesInRow(xs); match(xs, expected(double(gui.m_freeAnchorSample), 100.0, 4.0), "free, resolution 1: bar lines only");
    setP(*proc, JadeParamID::bpmResolution, 2.f); gui.timerCallback();

    // 5) Alt+click: a bar line at the click
    { const float cx = float(d.getX()) + 0.437f * float(d.getWidth()); const juce::Point<float> p(cx, float(d.getCentreY()));
      gui.mouseDown(ev(gui, p, p, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier)));
      setP(*proc, JadeParamID::bpmResolution, 0.f); gui.timerCallback(); linesInRow(xs);
      float best = 1e9f; for (float x : xs) best = std::min(best, std::abs(x - cx));
      std::printf("Alt+click at x %.1f: nearest bar line %.2f px away\n", cx, best);
      check(best < 1.5f, "Alt+click: bar line at the click position");
      match(xs, expected(double(gui.m_freeAnchorSample), 100.0, 4.0), "after Alt+click: bars from the new anchor");
      const auto anchor = gui.m_freeAnchorSample;
      gui.mouseDown(ev(gui, p.translated(50, 0), p, juce::ModifierKeys::leftButtonModifier));
      check(gui.m_freeAnchorSample == anchor, "click without Alt: anchor unchanged");
      setP(*proc, JadeParamID::bpmResolution, 2.f); gui.timerCallback(); }
    gui.pauseClicked();

    // 6) sync again: host tempo shown, follows the host while stopped and while paused
    gui.m_tempoSyncButton.onClick();
    run(1.0, 97.0, true, false); gui.timerCallback();
    check(!gui.m_tempoFree && gui.m_bpmValue.getText() == "97 BPM" && raw(*proc, JadeParamID::freeBpm) == 100.f, "sync, stopped transport, host tempo 97: box shows 97 BPM");
    gui.pauseClicked(); run(0.5, 133.3, true, false); gui.timerCallback();
    check(gui.m_bpmValue.getText() == "133.3 BPM", "sync, display paused: box follows the host (133.3 BPM)");
    gui.pauseClicked();

    // 7) no host tempo (Standalone): sync 'no tempo' without lines, free draws its own grid
    run(10.2, 0.0, false, false); linesInRow(xs);
    check(xs.empty() && gui.m_bpmValue.getText() == "no tempo", "no host tempo, sync: no lines, 'no tempo'");
    gui.m_tempoSyncButton.onClick(); linesInRow(xs);
    std::printf("no host tempo, free: %zu lines, box '%s'\n", xs.size(), gui.m_bpmValue.getText().toRawUTF8());
    check(raw(*proc, JadeParamID::freeBpm) == 100.f, "no host tempo: switching keeps the own tempo");
    // the anchor is the newest column: a bar line at the right edge (0 s), outside the expected range
    check(!xs.empty() && xs.back() > float(d.getRight()) - 2.f, "no host tempo: bar line at 0 s (anchor = now)");
    xs.pop_back();
    match(xs, expected(double(gui.m_freeAnchorSample), 100.0, 1.0), "no host tempo, free 100 BPM: lines every beat");
    check(g_allocs == 0, "no allocation on the audio thread");

    // 8) state: mode and tempo saved and restored
    setP(*proc, JadeParamID::freeBpm, 87.4f);
    juce::MemoryBlock state; proc->getStateInformation(state);
    auto proc2 = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc2->setPlayConfigDetails(2, 2, fs, 512); proc2->prepareToPlay(fs, 512);
    proc2->setStateInformation(state.getData(), int(state.getSize()));
    auto* ed2 = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc2->createEditorIfNeeded());
    ed2->m_editor.timerCallback();
    check(ed2->m_editor.m_tempoFree && ed2->m_editor.m_bpmValue.isEditable() && ed2->m_editor.m_bpmValue.getText() == "87.4 BPM", "restored: free mode, 87.4 BPM");
    { auto* tf = dynamic_cast<juce::RangedAudioParameter*>(proc->m_parameterVTS->getParameter(JadeParamID::tempoFree));
      auto* fb = dynamic_cast<juce::RangedAudioParameter*>(proc->m_parameterVTS->getParameter(JadeParamID::freeBpm));
      check(!tf->isAutomatable() && !fb->isAutomatable(), "both parameters not automatable"); }
    { juce::Image icon = gui.m_tempoSyncButton.createComponentSnapshot(gui.m_tempoSyncButton.getLocalBounds(), true, 4.f);
      juce::File("/tmp/claude-1000/-home-bitzer-AudioDev-stereo-widening/35d98a33-49e5-417e-aadf-6a371e5a501e/scratchpad/free/icon_free.png").deleteFile();
      juce::FileOutputStream os(juce::File("/tmp/claude-1000/-home-bitzer-AudioDev-stereo-widening/35d98a33-49e5-417e-aadf-6a371e5a501e/scratchpad/free/icon_free.png")); juce::PNGImageFormat().writeImageToStream(icon, os);
      juce::Image box = gui.m_bpmValue.createComponentSnapshot(gui.m_bpmValue.getLocalBounds(), true, 4.f);
      juce::File("/tmp/claude-1000/-home-bitzer-AudioDev-stereo-widening/35d98a33-49e5-417e-aadf-6a371e5a501e/scratchpad/free/box_free.png").deleteFile();
      juce::FileOutputStream os2(juce::File("/tmp/claude-1000/-home-bitzer-AudioDev-stereo-widening/35d98a33-49e5-417e-aadf-6a371e5a501e/scratchpad/free/box_free.png")); juce::PNGImageFormat().writeImageToStream(box, os2); }
    proc2->editorBeingDeleted(ed2); delete ed2;
    std::printf(fails ? "FAILED\n" : "all OK\n");
    return fails ? 1 : 0;
}
