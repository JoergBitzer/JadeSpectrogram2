
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginSettings.h"

//==============================================================================
#if WITH_MIDIKEYBOARD   
JadeSpectrogramAudioProcessorEditor::JadeSpectrogramAudioProcessorEditor (JadeSpectrogramAudioProcessor& p)
    : AudioProcessorEditor (&p), m_processorRef (p), m_presetGUI(p.m_presets),
    	m_keyboard(m_processorRef.m_keyboardState, MidiKeyboardComponent::Orientation::horizontalKeyboard), 
        m_wheels(p.m_wheelState), m_editor(p,*p.m_parameterVTS)
#else
JadeSpectrogramAudioProcessorEditor::JadeSpectrogramAudioProcessorEditor (JadeSpectrogramAudioProcessor& p)
    : AudioProcessorEditor (&p), m_processorRef (p), m_presetGUI(p.m_presets), m_editor(p,*p.m_parameterVTS)
#endif
{
    float scaleFactor = m_processorRef.getScaleFactor();
    setResizeLimits (g_minGuiSize_x,static_cast<int>(g_minGuiSize_x*g_guiratio) , g_maxGuiSize_x, static_cast<int>(g_maxGuiSize_x*g_guiratio));
    setResizable(true,true);
    getConstrainer()->setFixedAspectRatio(1./g_guiratio);
    setSize (static_cast<int>(scaleFactor*g_minGuiSize_x), static_cast<int>(scaleFactor*g_minGuiSize_x*g_guiratio));

	addAndMakeVisible(m_presetGUI);
#if WITH_MIDIKEYBOARD      
	addAndMakeVisible(m_keyboard);
    addAndMakeVisible(m_wheels);    
#endif

    // from here your algo editor ---------
    addAndMakeVisible(m_editor);

}

JadeSpectrogramAudioProcessorEditor::~JadeSpectrogramAudioProcessorEditor()
{
}

//==============================================================================
void JadeSpectrogramAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    juce::String text2display = "Hello World! V " + juce::String(PLUGIN_VERSION_MAJOR) + "." + juce::String(PLUGIN_VERSION_MINOR) + "." + juce::String(PLUGIN_VERSION_PATCH);
    g.drawFittedText (text2display, getLocalBounds(), juce::Justification::centred, 1);
}
void JadeSpectrogramAudioProcessorEditor::resized()
{
    int height = getHeight();
    // necessary to change fontsize of comboboxes and PopUpmenus
    // 0.5 is a good compromisecould be slightly higher or lower
    // m_jadeLAF.setFontSize(0.5*height*g_minPresetHandlerHeight/g_minGuiSize_y);
    // top presethandler
#if WITH_PRESETHANDLERGUI    
    m_presetGUI.setBounds(0, 0, getWidth(), height*g_minPresetHandlerHeight/g_minGuiSize_y);
#endif
    // bottom a small midkeyboard
#if WITH_MIDIKEYBOARD    
    m_wheels.setBounds(0, static_cast<int> (height*(1-g_midikeyboardratio)),  static_cast<int> (g_wheelstokeyboardratio*getWidth()),  static_cast<int> (height*g_midikeyboardratio));
    m_keyboard.setBounds(static_cast<int> (g_wheelstokeyboardratio*getWidth()), static_cast<int> (height*(1-g_midikeyboardratio)), static_cast<int> ((1.0-g_wheelstokeyboardratio)*getWidth()),static_cast<int> ( height*g_midikeyboardratio));
#endif
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..

    int width = getWidth();
	float scaleFactor = float(width)/g_minGuiSize_x;
    m_processorRef.setScaleFactor(scaleFactor);
    // use setBounds with scaleFactor
#if WITH_MIDIKEYBOARD    
    #if WITH_PRESETHANDLERGUI    
    m_editor.setBounds(0, static_cast<int> (height*g_minPresetHandlerHeight/g_minGuiSize_y + 1), 
                        getWidth(), static_cast<int> (height*(1-g_midikeyboardratio) - (height*g_minPresetHandlerHeight/g_minGuiSize_y + 1) ));
    #else
    m_editor.setBounds(0, 0, getWidth(), static_cast<int> (height*(1-g_midikeyboardratio) ));

    #endif                        
#else
    #if WITH_PRESETHANDLERGUI    
    m_editor.setBounds(0, static_cast<int> (height*g_minPresetHandlerHeight/g_minGuiSize_y + 1), 
                        getWidth(), static_cast<int> (height - (height*g_minPresetHandlerHeight/g_minGuiSize_y + 1) ));
    #else
    m_editor.setBounds(0, 0, getWidth(), height);

    #endif                        
#endif

}
