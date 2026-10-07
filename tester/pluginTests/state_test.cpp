// All settings are saved with the project and restored (incl. the GUI size).
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#define private public
#define protected public
#include "PluginProcessor.h"
#include "PluginEditor.h"
#undef private
#undef protected

static void setParam(JadeSpectrogramAudioProcessor& p, const juce::String& id, float plainValue)
{
    auto* par = dynamic_cast<juce::RangedAudioParameter*>(p.m_parameterVTS->getParameter(id));
    par->setValueNotifyingHost(par->convertTo0to1(plainValue));
}
static float plain(JadeSpectrogramAudioProcessor& p, const juce::String& id) { return p.m_parameterVTS->getRawParameterValue(id)->load(); }
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("  %-58s %s\n", what, ok ? "OK" : "FAIL"); fails += !ok; }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000.0;
    // ---- A: change every setting, save
    juce::MemoryBlock state;
    {
        auto a = std::make_unique<JadeSpectrogramAudioProcessor>();
        a->setPlayConfigDetails(2, 2, fs, 512); a->prepareToPlay(fs, 512);
        setParam(*a, JadeParamID::fftSize, 4);     // 8192
        setParam(*a, JadeParamID::window, 4);      // FlatTop
        setParam(*a, JadeParamID::colorMap, 4);    // Viridis
        setParam(*a, JadeParamID::logFreqAxis, 1);
        setParam(*a, JadeParamID::fixDisplay, 1);
        setParam(*a, "MinFreq", std::log(50.f)); setParam(*a, "MaxFreq", std::log(5000.f));
        setParam(*a, "MinColor", -60.f); setParam(*a, "MaxColor", 0.f);
        a->setScaleFactor(1.25f);
        a->getStateInformation(state);
        std::printf("state: %zu bytes\n", state.getSize());
        std::printf("automatable:"); for (auto* p : a->getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p)) std::printf(" %s=%d", r->getParameterID().toRawUTF8(), int(r->isAutomatable())); std::printf("\n");
    }
    // ---- B: restore, process, open the editor
    {
        std::printf("restore into a new instance:\n");
        auto b = std::make_unique<JadeSpectrogramAudioProcessor>();
        b->setPlayConfigDetails(2, 2, fs, 512); b->prepareToPlay(fs, 512);
        b->setStateInformation(state.getData(), int(state.getSize()));
        check(juce::roundToInt(plain(*b, JadeParamID::fftSize)) == 4 && juce::roundToInt(plain(*b, JadeParamID::window)) == 4
              && juce::roundToInt(plain(*b, JadeParamID::colorMap)) == 4 && plain(*b, JadeParamID::logFreqAxis) > 0.5f
              && plain(*b, JadeParamID::fixDisplay) > 0.5f, "parameters restored");
        check(std::abs(std::exp(plain(*b, "MinFreq")) - 50.f) < 0.1f && std::abs(std::exp(plain(*b, "MaxFreq")) - 5000.f) < 1.f
              && std::abs(plain(*b, "MinColor") + 60.f) < 0.01f && std::abs(plain(*b, "MaxColor")) < 0.01f, "frequency and colour range restored");
        check(std::abs(b->getScaleFactor() - 1.25f) < 1e-4f, "window size (scale factor) restored");
        juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi; buf.clear();
        b->processBlock(buf, midi);
        check(b->m_algo.getSpectrumSize() == 4097, "audio thread analyses with FFT size 8192");
        check(b->m_algo.m_windowChoice == SpectrumAnalyzer::WindowType::FlatTop, "audio thread uses the FlatTop window");
        auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(b->createEditorIfNeeded());
        auto& gui = ed->m_editor; gui.timerCallback();
        check(gui.m_fftSizeCombo.getText() == "8192" && gui.m_windowFktCombo.getText() == "FlatTop" && gui.m_colorScheme.getText() == "Viridis", "combo boxes show 8192 / FlatTop / Viridis");
        check(gui.m_colorpalette.m_ColorScheme == CColorPalette::PaletteName::kViridis, "palette is Viridis");
        check(gui.m_logFreqAxis && gui.m_freqAxisButton.getButtonText() == "Lin" && gui.m_axisMap == JadeSpectrogramGUI::AxisMap::Log, "log axis on (button shows 'Lin')");
        check(gui.m_fixedDisplay && gui.m_runModeButton.getButtonText() == "Run", "Fix mode on (button shows 'Run')");
        // GUI changes go into the parameters
        gui.freqAxisClicked(); gui.runClicked(); gui.m_fftSizeCombo.setSelectedItemIndex(1, juce::sendNotificationSync);
        check(plain(*b, JadeParamID::logFreqAxis) < 0.5f && plain(*b, JadeParamID::fixDisplay) < 0.5f && juce::roundToInt(plain(*b, JadeParamID::fftSize)) == 1, "clicks and combo box write the parameters");
        b->processBlock(buf, midi);
        check(b->m_algo.getSpectrumSize() == 513, "new FFT size 1024 reaches the audio thread");
        b->editorBeingDeleted(ed); delete ed;
        // reopen the editor: settings still there
        auto* ed2 = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(b->createEditorIfNeeded());
        ed2->m_editor.timerCallback();
        check(!ed2->m_editor.m_logFreqAxis && ed2->m_editor.m_fftSizeCombo.getText() == "1024", "reopened editor shows the current settings");
        b->editorBeingDeleted(ed2); delete ed2;
    }
    // ---- C: an old (1.6.0) state without the new parameters loads with the defaults
    {
        std::printf("old state without the new parameters:\n");
        auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
        juce::StringArray remove {JadeParamID::fftSize, JadeParamID::window, JadeParamID::colorMap, JadeParamID::logFreqAxis, JadeParamID::fixDisplay};
        for (int i = xml->getNumChildElements(); --i >= 0;)
            if (remove.contains(xml->getChildElement(i)->getStringAttribute("id"))) xml->removeChildElement(xml->getChildElement(i), true);
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary(*xml, old);
        auto c = std::make_unique<JadeSpectrogramAudioProcessor>();
        c->setPlayConfigDetails(2, 2, fs, 512); c->prepareToPlay(fs, 512);
        setParam(*c, JadeParamID::fftSize, 0); // something else first, to see the reset
        c->setStateInformation(old.getData(), int(old.getSize()));
        check(juce::roundToInt(plain(*c, JadeParamID::fftSize)) == 2 && juce::roundToInt(plain(*c, JadeParamID::window)) == 1
              && juce::roundToInt(plain(*c, JadeParamID::colorMap)) == 5 && plain(*c, JadeParamID::logFreqAxis) < 0.5f, "defaults 2048 / Hann / Plasma / linear");
        check(std::abs(std::exp(plain(*c, "MaxFreq")) - 5000.f) < 1.f, "old parameters still read (max 5000 Hz)");
    }
    std::printf("%s\n", fails ? "FAILED" : "all OK");
    return fails;
}
