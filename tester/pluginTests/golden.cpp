// Golden master for refactoring: writes analysis hashes, GUI snapshots (PNG), readout texts and the
// saved state into <outdir>. Run before and after a refactor; the outputs must be identical.
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
static juce::File out;
static juce::String report;
static void setP(JadeSpectrogramAudioProcessor& p, const juce::String& id, float v)
{ auto* r = dynamic_cast<juce::RangedAudioParameter*>(p.m_parameterVTS->getParameter(id)); r->setValueNotifyingHost(r->convertTo0to1(v)); }
struct Head : juce::AudioPlayHead {
    bool playing = true, looping = true; double ppq = 1.3, bpm = 133.0;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setIsPlaying(playing); p.setIsLooping(looping); p.setLoopPoints(LoopPoints{0.0, 8.0}); p.setPpqPosition(ppq); p.setBpm(bpm);
        p.setPpqPositionOfLastBarStart(std::floor(ppq / 4.0) * 4.0); p.setTimeSignature(TimeSignature{4, 4}); return p; }
};
struct Sig { double ph[3] = {0, 0, 0}; juce::Random rnd {7}; long n = 0;
    void fill(juce::AudioBuffer<float>& b, double fs) { for (int i = 0; i < b.getNumSamples(); ++i, ++n) {
        const double t = double(n) / fs; const double f0 = 110.0 * std::pow(2.0, std::floor(std::fmod(t * 2.0, 12.0)) / 12.0);
        double v = 0; for (int h = 1; h <= 8; ++h) v += std::sin(h * ph[0]) / h; ph[0] += 2.0 * juce::MathConstants<double>::pi * f0 * (1.0 + 0.004 * std::sin(6.0 * t)) / fs;
        v = 0.2 * v + 0.05 * std::sin(ph[1]); ph[1] += 2.0 * juce::MathConstants<double>::pi * (200.0 + 3000.0 * std::fmod(t, 3.0)) / fs;
        if (std::fmod(t, 1.7) < 0.05) v += 0.3 * (rnd.nextFloat() - 0.5);
        b.setSample(0, i, float(v)); b.setSample(1, i, float(0.7 * v + 0.01 * std::sin(0.3 * double(n)))); } } };
static void note(const juce::String& s) { report << s << "\n"; }
static juce::String fnv(const void* data, size_t n) { const auto* c = static_cast<const unsigned char*>(data); juce::uint64 h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 1099511628211ULL; } return juce::String::toHexString(static_cast<juce::int64>(h)); }
static juce::String hashImage(const juce::Image& img) { juce::MemoryOutputStream m; juce::PNGImageFormat().writeImageToStream(img, m); return fnv(m.getData(), m.getDataSize()); }
static void savePNG(const juce::Image& img, const juce::String& name) { auto f = out.getChildFile(name + ".png"); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat().writeImageToStream(img, os); note(name + " " + hashImage(img)); }
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    out = juce::File(juce::String(argv[1])); out.createDirectory();
    // ---------- 1) analysis without GUI: every slice (values and info) hashed ----------
    struct A { int fft, win, ov; float avg; int mix; double fs; int block; } as[] = {
        {2, 1, 0, 0.f, 1, 48000, 512}, {0, 0, 1, 0.f, 0, 44100, 256}, {4, 3, 0, 300.f, 1, 48000, 441}, {3, 4, 1, 50.f, 2, 96000, 1024}, {1, 5, 0, 0.f, 4, 44100, 333}};
    for (auto& a : as)
    {
        auto proc = std::make_unique<JadeSpectrogramAudioProcessor>(); Head head; proc->setPlayHead(&head);
        proc->setPlayConfigDetails(2, 2, a.fs, a.block); proc->prepareToPlay(a.fs, a.block);
        setP(*proc, JadeParamID::fftSize, float(a.fft)); setP(*proc, JadeParamID::window, float(a.win)); setP(*proc, JadeParamID::overlap, float(a.ov)); setP(*proc, JadeParamID::averaging, a.avg);
        proc->m_algo.setChannelMixMode(static_cast<JadeSpectrogramAudio::ChannelMixMode>(a.mix));
        juce::AudioBuffer<float> buf(2, a.block); juce::MidiBuffer midi; Sig sig; juce::MemoryOutputStream m; int slices = 0; std::vector<float> s;
        for (int b = 0; b < int(6.0 * a.fs / a.block); ++b) {
            if (b == int(2.0 * a.fs / a.block)) { setP(*proc, JadeParamID::fftSize, float((a.fft + 2) % 5)); head.bpm = 97.0; }
            if (b == int(4.0 * a.fs / a.block)) { setP(*proc, JadeParamID::overlap, float(1 - a.ov)); head.playing = false; }
            if (b == int(5.0 * a.fs / a.block)) { head.playing = true; head.ppq = 3.0; }
            sig.fill(buf, a.fs); proc->processBlock(buf, midi); if (head.playing) head.ppq = std::fmod(head.ppq + a.block * head.bpm / 60.0 / a.fs, 8.0);
            while (size_t sz = proc->m_algo.getNextMemSliceSize()) { SliceInfo inf = proc->m_algo.getNextMemSliceInfo(); s.resize(sz); proc->m_algo.getMemSlice(s);
                m.write(s.data(), s.size() * sizeof(float)); m.write(&inf.hop, sizeof(inf.hop)); m.writeBool(inf.hasPpq); m.writeDouble(inf.ppq); m.writeDouble(inf.barStartPpq);
                m.writeFloat(inf.barLengthPpq); m.writeFloat(inf.beatPpq); m.writeFloat(inf.bpm); m.writeInt64(inf.endSample); m.writeBool(inf.hasSegmentStart); m.writeDouble(inf.segmentStartPpq); ++slices; } }
        note(juce::String::formatted("analysis fft%d win%d ov%d avg%.0f mix%d fs%.0f block%d: %d slices ", a.fft, a.win, a.ov, a.avg, a.mix, a.fs, a.block, slices)
             + fnv(m.getData(), m.getDataSize()) + " latency " + juce::String(proc->getLatencySamples()));
    }
    // ---------- 2) GUI configurations ----------
    struct G { const char* name; int w; bool log, keys, grid; int res; bool freeT; float bpm, ref; int cmap; float t0, t1, fmin, fmax, cmin, cmax; bool fix, pause, cross, harm; int fft, ov; float avg; };
    G gs[] = {
        {"default",          840, false, false, false, 2, false, 120, 440, 5, 0.f, 1.f, 1, 20000, -80, 20, false, false, false, false, 2, 0, 0},
        {"log_keys_cross",  1100, true,  true,  true,  3, false, 120, 440, 5, .2f, 1.f, 80, 8000, -75, 10, false, false, true,  true,  3, 0, 0},
        {"lin_grid16_fix",  1344, false, false, true,  4, false, 120, 442, 4, .5f, .9f, 1, 3500, -70, 0,  true,  false, true,  false, 2, 1, 100},
        {"free_tempo_pause", 840, true,  true,  true,  2, true,  97.3f, 415, 2, 0.f, 1.f, 40, 12000, -80, 20, false, true, true, true, 4, 1, 0},
        {"bins_jade_avg",    900, false, true,  false, 0, false, 120, 466.2f, 6, .1f, .6f, 100, 1200, -60, -10, false, false, true, false, 4, 0, 800},
        {"mono_bw_hot",     1000, true,  false, true,  1, false, 120, 440, 0, 0.f, 1.f, 20, 20000, -80, 20, true,  true, false, false, 0, 1, 0}};
    for (auto& c : gs)
    {
        auto proc = std::make_unique<JadeSpectrogramAudioProcessor>(); Head head; proc->setPlayHead(&head);
        const double fs = 44100.0; const int block = 256;
        proc->setPlayConfigDetails(2, 2, fs, block); proc->prepareToPlay(fs, block);
        auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
        ed->setSize(c.w, int(float(c.w) * g_guiratio));
        ed->m_aboutboxvisible = false; // not initialised by the editor (2.4.3): random otherwise
        auto& gui = ed->m_editor;
        setP(*proc, JadeParamID::logFreqAxis, c.log); setP(*proc, JadeParamID::keyboardOverlay, c.keys); setP(*proc, JadeParamID::bpmGrid, c.grid);
        setP(*proc, JadeParamID::bpmResolution, float(c.res)); setP(*proc, JadeParamID::freeBpm, c.bpm); setP(*proc, JadeParamID::refPitch, c.ref);
        setP(*proc, JadeParamID::colorMap, float(c.cmap)); setP(*proc, JadeParamID::timeStart, c.t0); setP(*proc, JadeParamID::timeEnd, c.t1);
        setP(*proc, "MinFreq", std::log(c.fmin)); setP(*proc, "MaxFreq", std::log(c.fmax)); setP(*proc, "MinColor", c.cmin); setP(*proc, "MaxColor", c.cmax);
        setP(*proc, JadeParamID::fixDisplay, c.fix); setP(*proc, JadeParamID::fftSize, float(c.fft)); setP(*proc, JadeParamID::overlap, float(c.ov)); setP(*proc, JadeParamID::averaging, c.avg);
        gui.timerCallback();
        if (c.freeT) { gui.m_tempoSyncButton.onClick(); setP(*proc, JadeParamID::freeBpm, c.bpm); }
        juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi; Sig sig;
        for (int b = 0; b < int(12.0 * fs / block); ++b) {
            if (c.pause && b == int(9.0 * fs / block)) gui.pauseClicked();
            sig.fill(buf, fs); proc->processBlock(buf, midi); head.ppq = std::fmod(head.ppq + block * head.bpm / 60.0 / fs, 8.0);
            if (b % 7 == 0) gui.timerCallback(); }
        gui.timerCallback();
        const auto d = gui.displayArea();
        if (c.cross) {
            gui.m_mousePos = {d.getX() + d.getWidth() * 7 / 10, d.getY() + int(gui.frequencyToY(225.f, float(d.getHeight())))};
            gui.m_mouseInDisplay = gui.setLabelText(gui.m_mousePos.x, gui.m_mousePos.y); gui.m_showHarmonics = c.harm;
            note(juce::String(c.name) + " readout '" + gui.m_readoutText + "' peak " + juce::String(int(gui.m_peakValid)) + " '" + gui.m_peakText + "'");
            for (int k = 0; k < 40; ++k) { const int x = d.getX() + (k * 37) % d.getWidth(), y = d.getY() + (k * 53) % d.getHeight();
                gui.setLabelText(x, y); note(juce::String(c.name) + juce::String::formatted(" r%02d ", k) + gui.m_readoutText + " / " + gui.m_peakText); }
            gui.setLabelText(gui.m_mousePos.x, gui.m_mousePos.y); }
        if (c.freeT) { gui.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {float(d.getX() + d.getWidth() / 3), float(d.getCentreY())},
                       juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier), 0.f, 0.f, 0.f, 0.f, 0.f, &gui, &gui, juce::Time(), {}, juce::Time(), 1, false)); }
        savePNG(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.f), juce::String("gui_") + c.name);
        savePNG(gui.renderExportImage(2.f), juce::String("export_") + c.name);
        if (std::string(c.name) == "default") { gui.m_resetViewButton.onClick(); gui.timerCallback(); savePNG(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.f), "gui_default_after_overview"); }
        juce::MemoryBlock st; proc->getStateInformation(st);
        note(juce::String(c.name) + " state " + fnv(st.getData(), st.getSize()) + " bpmText '" + gui.m_bpmValue.getText() + "' ref '" + gui.m_refPitchValue.getText() + "'");
        // state round trip into a new instance
        auto p2 = std::make_unique<JadeSpectrogramAudioProcessor>(); p2->setPlayConfigDetails(2, 2, fs, block); p2->prepareToPlay(fs, block); p2->setStateInformation(st.getData(), int(st.getSize()));
        juce::MemoryBlock st2; p2->getStateInformation(st2); note(juce::String(c.name) + " state round trip " + juce::String(int(st == st2)));
        proc->editorBeingDeleted(ed); delete ed;
    }
    out.getChildFile("report.txt").replaceWithText(report);
    std::printf("%s", report.toRawUTF8());
    return 0;
}
