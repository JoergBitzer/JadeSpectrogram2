// Overview button: all three range sliders to their full range (lin and log axis), parameters
// written inside host gestures; the button is reachable at its place (not covered), export moved.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#define private public
#define protected public
#include "PluginProcessor.h"
#include "PluginEditor.h"
#undef private
#undef protected

static void setParam(JadeSpectrogramAudioProcessor& p, const juce::String& id, float v)
{ auto* par = dynamic_cast<juce::RangedAudioParameter*>(p.m_parameterVTS->getParameter(id)); par->setValueNotifyingHost(par->convertTo0to1(v)); }
static float plain(JadeSpectrogramAudioProcessor& p, const juce::String& id) { return p.m_parameterVTS->getRawParameterValue(id)->load(); }
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("  %-62s %s\n", what, ok ? "OK" : "FAIL"); fails += !ok; }
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
    auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
    proc->setPlayConfigDetails(2, 2, 48000.0, 512); proc->prepareToPlay(48000.0, 512);
    auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
    auto& gui = ed->m_editor; ed->setVisible(true); // getComponentAt needs a visible component
    for (bool logAxis : {false, true})
    {
        setParam(*proc, JadeParamID::logFreqAxis, logAxis ? 1.f : 0.f);
        setParam(*proc, "MinFreq", std::log(300.f)); setParam(*proc, "MaxFreq", std::log(3000.f));
        setParam(*proc, "MinColor", -50.f); setParam(*proc, "MaxColor", -10.f);
        setParam(*proc, JadeParamID::timeStart, 0.4f); setParam(*proc, JadeParamID::timeEnd, 0.7f);
        gui.timerCallback();
        Gestures gl; proc->addListener(&gl);
        // click where the button is drawn
        auto c = gui.m_resetViewButton.getBounds().getCentre();
        auto* hit = gui.getComponentAt(c);
        auto pos = ed->getLocalPoint(&gui, c);
        auto* hitEd = ed->getComponentAt(pos);
        gui.m_resetViewButton.onClick(); // triggerClick() is asynchronous
        gui.timerCallback();
        proc->removeListener(&gl);
        const float fmin = std::exp(plain(*proc, "MinFreq")), fmax = std::exp(plain(*proc, "MaxFreq"));
        std::printf("%s axis: freq %.1f .. %.0f Hz (slider %.1f .. %.0f), colour %.0f .. %.0f dB, time %.2f .. %.2f, gestures %d/%d, changes %d\n",
            logAxis ? "log" : "lin", fmin, fmax, gui.m_freqRangeSlider.getMinimum(), gui.m_freqRangeSlider.getMaximum(),
            plain(*proc, "MinColor"), plain(*proc, "MaxColor"), plain(*proc, JadeParamID::timeStart), plain(*proc, JadeParamID::timeEnd), gl.begins, gl.ends, gl.changes);
        check(std::abs(fmin - float(gui.m_freqRangeSlider.getMinimum())) < 0.01f*fmin && std::abs(fmax - float(gui.m_freqRangeSlider.getMaximum())) < 1.f
              && (logAxis ? fmin > 19.f : fmin < 1.1f), "frequency: full range of the axis");
        check(plain(*proc, "MinColor") == -80.f && plain(*proc, "MaxColor") == 20.f, "colour: -80 .. 20 dB");
        check(plain(*proc, JadeParamID::timeStart) == 0.f && plain(*proc, JadeParamID::timeEnd) == 1.f, "time: whole 10 s");
        check(gl.begins == 6 && gl.ends == 6 && gl.changes == 6, "six parameters, each in a host gesture");
        check(hit == &gui.m_resetViewButton && (hitEd == &gui.m_resetViewButton), "button reachable at its place (not covered)");
    }
    const auto d = gui.displayArea();
    check(gui.m_exportButton.isVisible() && gui.m_exportButton.getRight() == d.getRight() && gui.m_exportButton.getY() > d.getBottom(), "export button in the bottom row, at the right end of the display");
    check(gui.m_exportButton.getRight() < gui.m_colorScheme.getX() && gui.m_averagingSlider.getRight() < gui.m_exportButton.getX(), "export between averaging and colour map");
    return fails ? 1 : 0;
}
