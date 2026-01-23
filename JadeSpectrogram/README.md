# AdvancedAudioTemplate
A template for quick development of audio plugins on a semi-professional level.

Dependencies:
This template is based on JUCE. So you need to clone JUCE and you need one more CMakeLists.txt file from the repository AudioDevOrga.

Your directory structure should look like this

YourDevDir
    CMakeLists.txt (from AudioDevOrga)
    JUCE (directory)
    YourNewProjectDir (see Usage section of this ReadMe)
        CMakeLists.txt (from this template)
        tools (template dir)
        PluginEditor.cpp
        PluginEditor.h
        PluginProcessor.cpp
        PluginProcessor.h
        PluginSettings.h
        JadeSpectrogram.cpp (keep this name for the file, it will be renamed later)
        JadeSpectrogram.h (keep this name for the file, it will be renamed later)

## Purpose
This template provides some basic features for effects and synth, like:
* synchron block processing for arbitrary block sizes 
* preset handler 
* resizable GUI 
* saving/loading of plugin state (with GUI size) 
* keyboard  + pitch-wheel and modulation-wheel (midi insert and display)  
* Access to Version Number given in CMakeLists.txt
* a template plugin cpp and h for easy start (named JadeSpectrogram.cpp and h). 

## History / Versioning
V1.0 basic usage is possible
V1.1 added access to AudioProcessor in the Algo and GUI (necessary for AudioPlayHead and to have getter function for the GUI)

## Usage

1. Create a new directory (better create a new repository in GitHub)
2. (if Github): Checkout your new project
3. copy template files (https://github.com/JoergBitzer/AdvancedAudioTemplate)
4. rename all instances of "JadeSpectrogram" in the Files with something appropriate 
    (use a renaming-tool like   
```console    
    sed -i 's/JadeSpectrogram/YourNewProjectName/g' *.*
```    
for MacOS (https://stackoverflow.com/questions/4247068/sed-command-with-i-option-failing-on-mac-but-works-on-linux)
for Windows: (https://stackoverflow.com/questions/17144355/how-can-i-replace-every-occurrence-of-a-string-in-a-file-with-powershell)  

```console    
 Get-ChildItem '*.*' -Recurse | ForEach {
      (Get-Content $_ | ForEach  { $_ -replace 'JadeSpectrogram', 'YourNewProjectName' }) |
      Set-Content $_ }
```    
or start the windows subsystem for linux

or use the tools given by visual studio code (Crtl + Shift + H (replace in files)) and use your new diretory./YOUR_PLuggIn_Folder as a filter in "files to include".

5. Rename JadeSpectrogram.cpp and JadeSpectrogram.h into YourNewProjectName.cpp and YourNewProjectName.h (e.g. Linux: 
```console    
    rename 's/JadeSpectrogram/YourNewProjectName/' *.*     
```    
and Windows (cmd, not PS)
```console    
ren JadeSpectrogram.* YourNewProjectName.*
```    

6. Add your new subdiretory to the main CMakeLists.txt (in main directory YourDevDir (e.g. AudioDev)) file
7. add or remove add_compile_definitions to your intention (Do you need a preset manager (default is yes), 
                                                            Do you need a midi-keyboard display (default is no)) 
8. Test if the template builds (should without error) and start coding your plugin

## Important files to look for 

### PluginSettingsh
In this file you can set the block size of your internal synchronous (fixed size) data processor.
Furthermore all global graphic adjustments are defined here.

### JadeSpectrogram.cpp and .h

After renaming the file you use these two files to implement the algorithm and the GUI. Always start with the definition of the parameters.

## Example to use the template

### Gain plugin (of course) 
[source code at](https://github.com/JoergBitzer/AAT_GainExample). I would use it only as a backup, if something goes wrong.

1. Think about a name: Here, GainPlugin

2. Apply Usage
for the 4th step: sed -i 's/JadeSpectrogram/GainPlugin/g' *.*
for MacOS, see (https://stackoverflow.com/questions/4247068/sed-command-with-i-option-failing-on-mac-but-works-on-linux)
for Windows: (https://stackoverflow.com/questions/17144355/how-can-i-replace-every-occurrence-of-a-string-in-a-file-with-powershell) 
```console    
  Get-ChildItem '*.*' -Recurse | ForEach {
      (Get-Content $_ | ForEach  { $_ -replace 'JadeSpectrogram', 'YourNewProjectName' }) |
      Set-Content $_ }
```    
or use the tools given by visual studio code (Crtl + Shift + H (replace in files)) and use your new diretory./YOUR_PLuggIn_Folder as a filter in "files to include".

for the 5th step: rename 's/JadeSpectrogram/GainPlugin/' *.* or by hand (just 2 files)

for the 7th step switch off PresetHandlerGUI (for a simple gain not necessary) 

3. Change the size in PluginSettings.h to something useful for a gain plugin 
```cpp
const int g_minGuiSize_x(200);
const int g_maxGuiSize_x(500);
const int g_minGuiSize_y(400);
```

4. 
In GainPLugin.h add the parameter definition (delete the example)
```cpp
const struct
{
	const std::string ID = "gain";
	const std::string name = "Gain";
	const std::string unitName = "dB";
	const float minValue = -80.f;
	const float maxValue = 20.f;
	const float defaultValue = 0.f;
}g_paramGain;

```
5. Add the support for a smoothed parameter
```cpp
private:
    float m_gain = 1.f;
    std::atomic<float>* m_gainParam = nullptr; 
    float m_gainParamOld = std::numeric_limits<float>::min(); //smallest possible number, will change in the first block
    juce::SmoothedValue<float,juce::ValueSmoothingTypes::Multiplicative> m_smoothedGain;

```

6. change addParameter (delete the example code)
```cpp
    paramVector.push_back(std::make_unique<juce::AudioParameterFloat>(g_paramGain.ID,            // parameterID
                                                        g_paramGain.name,            // parameter name
                                                        g_paramGain.minValue,              // minimum value
                                                        g_paramGain.maxValue,              // maximum value
                                                        g_paramGain.defaultValue));
```
7. change prepareParameter (delete the ignore method)
```cpp
    m_gainParam = vts->getRawParameterValue(g_paramGain.ID);
```


7. change the smoothing time for the SmoothedValues in prepareToPlay
```cpp
    // here your code
    m_smoothedGain.reset(sampleRate,0.02); // 20ms is enough for a smooth gain, 
```

8. Change the processSynchronBlock method (delete ignore method)
```cpp
    // check parameter update
    if (*m_gainParam != m_gainParamOld)
    {
        m_gainParamOld = *m_gainParam;
        m_gain = powf(10.f,m_gainParamOld/20.f);
    }

    int NrOfSamples = buffer.getNumSamples();
    int chns = buffer.getNumChannels();

    //m_smoothedGain.setTargetValue(m_gain);
    m_smoothedGain.setTargetValue(m_gain);
    float curGain;
    for (int channel = 0; channel < chns; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);
        // ..do something to the data...
        for (int kk = 0; kk < NrOfSamples; ++kk)
        {
            if (channel == 0)
                curGain = m_smoothedGain.getNextValue();
            channelData[kk] *= curGain;
        }
    }
```

9. Component adjustment (add one slider)

In the header add the necessary variables it should read like this
```cpp
private:
    AudioProcessorValueTreeState& m_apvts; 
    juce::Slider m_GainSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_GainAttachment;
```

In the cpp file change the constructor
```cpp
    m_GainSlider.setRange (g_paramGain.minValue, g_paramGain.maxValue);         
    m_GainSlider.setTextValueSuffix (g_paramGain.unitName);    
    m_GainSlider.setSliderStyle(juce::Slider::LinearVertical);
    m_GainSlider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxAbove, true, 60, 20);
    m_GainSlider.setValue(g_paramGain.defaultValue);
	m_GainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, g_paramGain.ID, m_GainSlider);
	addAndMakeVisible(m_GainSlider);
```
and the setbounds method
```cpp
	auto r = getLocalBounds();
	m_GainSlider.setBounds(r);
```

10. compile and you have your gain plugin

Some remarks
For most parameter it is better to use small synchron blocks (e.g. 2ms) and smooth the update. If you need smoothing on a sample base (like for gains) use applyRamp from the buffer (it is linear, but faster compared to a sample-based smoothing). 

(a german video exist to build Gain PLugins)

## Second Example EQ (Peak, one band)

The description of this example is very short. You will find the source code on Github (AAT_EQ1)

1. Solve your math first. You will find the formulas for Equalizer by searching for the "RBJ cookbook".
2. Understand what a second order section filter is. (LTI System, with 3 transversal (b0,b1,b2) and 2 recursive coefficients (a1,a2))
3. Build the audio class (3 parameter (Gain, Freq, Q))
4. Build the GUI (3 rotary knobs)
5. Done

### Remarks
For a general solution with more possibilities for Equalizer (cut, shelf), use the given TGMLib / TGMStaticLib


