#pragma once

#include <vector>
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "tools/AudioProcessParameter.h"
#include "tools/SynchronBlockProcessor.h"
#include "PluginSettings.h"

#include "SpectrumAnalyzer.h"
#include "TwoDimBlockFreeFiFo.h"
#include "CColorpalette.h"
#include "IconButton.h"
#include "DragValueBox.h"
#include "RangeSlider.h"
#include "JadeLookAndFeel.h"

class JadeSpectrogramAudioProcessor;

// ------------------------------------------------------------------------------------------------
// Parameters: display range (frequency in log(Hz), colour in dB); automatable
// ------------------------------------------------------------------------------------------------
const struct
{
    const std::string ID = "MinFreq";
    std::string name = "MinFreq";
    std::string unitName = "Hz";
    float minValue = log(1.f);
    float maxValue = log(10000.f);
    float defaultValue = log(1.f);
}paramDisplayMinFreq;
const struct
{
    const std::string ID = "MaxFreq";
    std::string name = "MaxFreq";
    std::string unitName = "Hz";
    float minValue = log(500.f);
    float maxValue = log(20000.f);
    float defaultValue = log(20000.f);
}paramDisplayMaxFreq;

const struct
{
    const std::string ID = "MinColor";
    std::string name = "MinColor";
    std::string unitName = "";
    float minValue = g_minColorVal;
    float maxValue = g_maxColorVal;
    float defaultValue = g_minColorVal;
}paramDisplayMinColor;
const struct
{
    const std::string ID = "MaxColor";
    std::string name = "MaxColor";
    std::string unitName = "";
    float minValue = g_minColorVal;
    float maxValue = g_maxColorVal;
    float defaultValue =g_maxColorVal;
}paramDisplayMaxColor;

// ------------------------------------------------------------------------------------------------
// Parameters: display settings, saved with the project, not automatable (decision 9 of the V2 plan)
// ------------------------------------------------------------------------------------------------
// Choice indices: FFT size 512 << index, window = SpectrumAnalyzer::WindowType,
// colour map = CColorPalette::PaletteName.
namespace JadeParamID
{
    // analysis
    inline const juce::String fftSize {"FFTSize"};
    inline const juce::String window {"Window"};
    inline const juce::String overlap {"Overlap"}; // 0: 50 %, 1: 75 %
    inline const juce::String averaging {"Averaging"}; // time constant in ms, 0 = off
    // display
    inline const juce::String colorMap {"ColorMap"};
    inline const juce::String logFreqAxis {"LogFreqAxis"};
    inline const juce::String fixDisplay {"FixDisplay"};
    inline const juce::String timeStart {"TimeStart"}; // visible part of the time window,
    inline const juce::String timeEnd {"TimeEnd"};     // fractions 0 ... 1 (1 = right edge)
    // notes
    inline const juce::String keyboardOverlay {"KeyboardOverlay"}; // piano-roll bands over the spectrogram
    inline const juce::String refPitch {"RefPitch"};   // reference pitch A4 in Hz (note names: overlay and readout)
    // beats
    inline const juce::String bpmGrid {"BpmGrid"};             // vertical lines at bars, beats, subdivisions
    inline const juce::String bpmResolution {"BpmResolution"}; // 0: bars only, 1: 1/2, 2: 1/4, 3: 1/8, 4: 1/16 note
    inline const juce::String tempoFree {"TempoFree"}; // BPM grid: false = synced to the host tempo, true = own tempo
    inline const juce::String freeBpm {"FreeBpm"};     // tempo of the free grid (quarter notes per minute)
}

// ------------------------------------------------------------------------------------------------
// Constants: FFT sizes, overlaps, logarithmic axis
// ------------------------------------------------------------------------------------------------
// FFT sizes selectable at runtime: 2^9 = 512 ... 2^13 = 8192
constexpr size_t g_minFFTSizeLog2 = 9;
constexpr size_t g_nrOfFFTSizes = 5;
constexpr size_t g_maxFFTSize = size_t(1) << (g_minFFTSizeLog2 + g_nrOfFFTSizes - 1);
constexpr size_t g_nrOfOverlaps = 2; // 0: 50 % (hop = FFT size / 2), 1: 75 % (hop = FFT size / 4)
// logarithmic frequency axis: lowest displayed frequency and number of image rows
const float g_logAxisMinFreq = 20.f;
const size_t g_logAxisRows = 1024;

// ================================================================================================
// JadeSpectrogramAudio: the analysis (audio thread); spectra go to the GUI through a lock-free FIFO
// ================================================================================================
class JadeSpectrogramAudio : public SynchronBlockProcessor
{
public:
    enum class ChannelMixMode
    {
        TimeMean, // mixing in the time domain by calculating the mean of the channels for each sample
        AbsMean, // all following are working in the time domain
        Max,
        Min,
        Left,
        Right
    };

    // --- setup and processing (audio thread) ---
    JadeSpectrogramAudio(JadeSpectrogramAudioProcessor* processor);
    void prepareToPlay(double sampleRate, int max_samplesPerBlock, int max_channels);
    // hides SynchronBlockProcessor::processBlock: applies pending FFT size / window changes first
    void processBlock(juce::AudioBuffer<float>& data, juce::MidiBuffer& midiMessages);
    virtual int processSynchronBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& midiMessages, int NrOfBlocksSinceLastProcessBlock);

    // --- parameters ---
    void addParameter(std::vector < std::unique_ptr<juce::RangedAudioParameter>>& paramVector);
    void prepareParameter(std::unique_ptr<juce::AudioProcessorValueTreeState>&  vts);

    // --- info for the host and the GUI ---
    int getLatency(){return m_Latency.load();}; // delay of the audio pass-through in samples (0 in Analyze mode)
    size_t getSpectrumSize(){return m_publishedFreqSize.load();}; // spectrum size currently produced by the audio thread
    float getSamplerate(){return m_fs.load();};

    // --- analysis settings: callable from any thread (GUI), applied by the audio thread ---
    // setFFTSize and setWindowType only post a request, the audio thread applies it at the start
    // of its next processBlock
    void setFFTSize(size_t newFFTSize){m_requestedFFTSize.store(newFFTSize);};
    size_t getFFTSize() const {return m_requestedFFTSize.load();};
    void setclosestFFTSize_ms(float fftsize_ms);
    size_t getnextpowerof2(float fftsize_ms);
    void setWindowType(SpectrumAnalyzer::WindowType type){m_requestedWindow.store(type);};
    void setChannelMixMode(ChannelMixMode mode){m_mixMode.store(mode);};
    void setPauseMode (bool mode){m_PauseMode.store(mode);};
    // averaging time constant in ms (tests without parameters; otherwise the Averaging parameter)
    void setAveragingMs(float tauMs){m_averagingMs.store(tauMs);};
    void setOverlap(size_t overlapIndex){m_requestedOverlap.store(std::min(overlapIndex, g_nrOfOverlaps-1));}; // tests

    // --- host position: beat position of each slice (BPM grid) ---
    // position of the host at the start of the next processBlock (audio thread, before processBlock)
    struct HostPosition
    {
        bool hasPpq = false;       // ppq and bpm known
        double ppq = 0.0;
        double bpm = 0.0;
        bool hasBarStart = false;
        double barStartPpq = 0.0;
        int numerator = 4, denominator = 4;
    };
    void setHostPosition(const HostPosition& position)
    {
        m_hostPosition = position;
        // latest host tempo for the GUI, also while the transport is stopped or the display paused
        m_hostBpm.store(position.hasPpq && position.bpm > 0.0 ? static_cast<float>(position.bpm) : 0.f, std::memory_order_relaxed);
        m_hostNumerator.store(position.numerator, std::memory_order_relaxed);
        m_hostDenominator.store(position.denominator, std::memory_order_relaxed);
    };
    float getHostBpm() const { return m_hostBpm.load(std::memory_order_relaxed); } // 0: no host tempo
    int getHostNumerator() const { return m_hostNumerator.load(std::memory_order_relaxed); }
    int getHostDenominator() const { return m_hostDenominator.load(std::memory_order_relaxed); }

    // --- GUI side of the FIFO: size of the next slice (0: nothing to read) and the slice itself ---
    size_t getNextMemSliceSize() const { return m_fifo.getNextSliceSize(); };
    SliceInfo getNextMemSliceInfo() const { return m_fifo.getNextSliceInfo(); };
    bool getMemSlice(std::vector<float>& outBlock){ return m_fifo.pop(outBlock); };

private:
    JadeSpectrogramAudioProcessor* m_processor;

    // --- shared with other threads (atomics) ---
    std::atomic<int> m_Latency {0}; // written by the audio thread on an FFT size switch
    std::atomic<float> m_fs {48000.f}; // written in prepareToPlay, read by the GUI
    std::atomic<size_t> m_publishedFreqSize {2048/2 + 1}; // copy of m_freqsize for the GUI
    std::atomic<size_t> m_requestedFFTSize {2048};
    std::atomic<SpectrumAnalyzer::WindowType> m_requestedWindow {SpectrumAnalyzer::WindowType::Hann};
    std::atomic<size_t> m_requestedOverlap {0};
    std::atomic<ChannelMixMode> m_mixMode; // GUI writes, audio thread reads
    std::atomic<bool> m_PauseMode; // GUI writes, audio thread reads
    std::atomic<float> m_averagingMs {0.f};

    // --- parameters: raw values of the value tree (lock-free) ---
    // FFT size and window parameters; nullptr without prepareParameter (tests), then
    // setFFTSize / setWindowType are used directly
    std::atomic<float>* m_fftSizeParam = nullptr;
    std::atomic<float>* m_windowParam = nullptr;
    std::atomic<float>* m_averagingParam = nullptr;
    std::atomic<float>* m_overlapParam = nullptr;
    jade::AudioProcessParameter<float> m_DisplayMinFreq;
    jade::AudioProcessParameter<float> m_DisplayMaxFreq;
    jade::AudioProcessParameter<float> m_DisplayMinColor;
    jade::AudioProcessParameter<float> m_DisplayMaxColor;

    // --- analyzers: active settings (audio thread only) ---
    size_t m_channels = 2;
    size_t m_fftsize = 2048; // active FFT size, audio thread only
    size_t m_freqsize = 2048/2 + 1; // active spectrum size, audio thread only
    SpectrumAnalyzer::WindowType m_windowChoice; // active window, audio thread only
    // one prepared analyzer pair per FFT size, so switching the size never allocates
    // [overlap][FFT size]
    std::array<std::array<SpectrumAnalyzer, g_nrOfFFTSizes>, g_nrOfOverlaps> m_leftAnalyzers;
    std::array<std::array<SpectrumAnalyzer, g_nrOfFFTSizes>, g_nrOfOverlaps> m_rightAnalyzers;
    size_t m_activeAnalyzer = 0;
    size_t m_activeOverlap = 0;
    void applyPendingChanges(); // audio thread, realtime safe
    void switchFFTSize(size_t newFFTSize); // audio thread, realtime safe

    // --- buffers of one analysis block ---
    std::vector<float> m_power;
    std::vector<float> m_perLeft;
    std::vector<float> m_perRight;
    std::vector<float> m_timeInLeft;
    std::vector<float> m_timeInRight;

    // --- averaging: exponential along time (first order IIR per bin on the power spectrum) ---
    std::vector<float> m_averagedPower;
    bool m_averagingStarted = false; // false: the next block starts the average

    // --- host position and sample counters: the beat position of each slice's end ---
    HostPosition m_hostPosition;
    std::atomic<float> m_hostBpm {0.f};
    std::atomic<int> m_hostNumerator {4}, m_hostDenominator {4};
    juce::int64 m_samplesFed = 0;       // samples given to processBlock so far
    juce::int64 m_blockStartSample = 0; // m_samplesFed at the start of the current host block
    juce::int64 m_sliceEndSample = 0;   // end of the last complete slice

    // --- output: spectra (with their SliceInfo) to the GUI ---
    TwoDimBlockFreeFiFO m_fifo {1000, g_maxFFTSize/2 + 1}; // 1000 time slices should be enough
};

// ================================================================================================
// JadeSpectrogramGUI: the display and its controls (message thread)
// ================================================================================================
class JadeSpectrogramGUI : public juce::Component, public Timer
{
public:
    JadeSpectrogramGUI(JadeSpectrogramAudioProcessor& p, juce::AudioProcessorValueTreeState& apvts);
    ~JadeSpectrogramGUI() override {stopTimer();};
    void paint(juce::Graphics& g) override;
    void resized() override;
    void setScaleFactor(float newscale){m_scaleFactor = newscale;};
    void timerCallback() override;
    std::function<void()> somethingChanged;

    // --- title bar: the editor is the parent of these controls (add, place, show/hide for the about box) ---
    // lin/log switch; the editor places it above the frequency axis (in its title bar)
    juce::Button& getFreqAxisButton() { return m_freqAxisButton; }
    // buttons that the editor shows in its title bar, right of the title image
    juce::Button& getKeyboardButton() { return m_keyboardButton; }
    std::vector<juce::Component*> getTitleBarControls();
    void setTitleBarBounds(float editorScaleFactor);
    void setTitleBarVisible(bool visible);

    // --- export: the visible spectrogram with its axes and the colour bar (no sliders, buttons, crosshair) ---
    juce::Image renderExportImage(float resolutionScale = 2.f);
    bool exportPNG(const juce::File& file, float resolutionScale = 2.f);

private:
    JadeSpectrogramAudioProcessor& m_processor;
    juce::AudioProcessorValueTreeState& m_apvts;
    float m_scaleFactor = 1.f;

    // --- parameters: read the saved settings, write from the controls (with host gestures) ---
    void syncFromParameters(); // lin/log and Run/Fix from the saved parameters
    void setBoolParameter(const juce::String& id, bool value);
    void setFloatParameter(const juce::String& id, float plainValue);

    // --- display memory: the last 10 s of spectra (one column per hop) and their image ---
    std::vector<std::vector<float >> m_displaymem;
    size_t m_displaymem_writepos = 0;
    size_t m_newDataAvailable = 0;
    std::vector<float> m_exchangeSpectrum;
    SliceInfo m_lastSliceInfo;            // of the newest slice
    size_t m_currentHop = 1; // hop of the slices in the display memory (from SliceInfo)
    size_t m_internalWidth;
    size_t m_internalHeight = 1;
    Image m_internalImg {Image::RGB, 1, 1, true};
    size_t m_imageRows = 1; // height of m_internalImg
    bool m_recomputeAll = true;
    juce::Rectangle<int> displayArea() const; // the analysis display in component coordinates
    float displayHeight() const; // height of the analysis display in pixels

    // --- colours: palette, colour range and the colour bar ---
    CColorPalette m_colorpalette {256, CColorPalette::PaletteName::kPlasma};
    float m_maxColorVal = g_maxColorVal;
    float m_minColorVal = g_minColorVal;
    float m_lastColorMin = 0.f, m_lastColorMax = 0.f; // palette range of the current image
    Image m_ColorbarImg; // colour bar at its pixel size, rebuilt only when map, colour range or size change
    int m_colorbarScheme = -1;
    float m_colorbarMin = 0.f, m_colorbarMax = 0.f;
    void updateColorbarImage(int width, int height);

    // --- frequency axis: lin/log, displayed range and the mapping of bins to image rows ---
    // Frequency axis and image layout (m_axisMap):
    //  Bins:      linear axis with at most one bin per screen pixel: image row = FFT bin,
    //             paint() draws the visible range with an exact transform
    //  LinearMax: linear axis with more bins than pixels: one image row per screen pixel,
    //             each the maximum of its bins (narrow lines do not disappear)
    //  Log:       logarithmic axis, g_logAxisRows image rows
    // In LinearMax and Log the image covers exactly the displayed range (m_mapMinFreq ... m_mapMaxFreq).
    TextButton m_freqAxisButton;
    void freqAxisClicked();
    void setFreqAxisButtonText();
    bool m_logFreqAxis = false;
    float m_maxDisplayFreq = 20000.f;
    float m_minDisplayFreq = 1.f;
    struct RowMap { size_t bin0; size_t bin1; float frac; bool useMax; };
    enum class AxisMap { Bins, LinearMax, Log };
    AxisMap m_axisMap = AxisMap::Bins;
    std::vector<RowMap> m_rowMap; // empty for Bins
    float m_mapMinFreq = 0.f; // frequency range and data the log mapping was built for
    float m_mapMaxFreq = 0.f;
    size_t m_mapBins = 0;
    float m_mapFs = 0.f;
    void updateFrequencyMapping(); // rebuilds the mapping (and resizes the image) if needed
    float rowValue(const std::vector<float>& column, size_t row) const;
    void updateDisplayRange(); // frequency sliders -> m_minDisplayFreq, m_maxDisplayFreq
    float frequencyToY(float freq, float displayH) const; // 0 = top of the display
    float yToFrequency(float y, float displayH) const;
    void drawFrequencyAxis(juce::Graphics& g, int x, int top, float displayH, float textH) const;

    // --- time axis and time zoom ---
    float m_timeStart = 0.f, m_timeEnd = 1.f; // visible fraction of the time window (from the parameters)
    void updateTimeRange();
    float timeSpan() const; // seconds covered by the display memory
    void drawTimeAxis(juce::Graphics& g, juce::Rectangle<int> display, float textH) const;

    // --- range sliders (V2 decision 7): frequency left, colour right, time zoom above; overview button ---
    RangeSlider m_freqRangeSlider {true};
    RangeSlider m_colorRangeSlider {true};
    RangeSlider m_timeRangeSlider {false}; // time zoom in the strip above the display
    void setFreqSliderScale(); // linear or log, as the frequency axis
    IconButton m_resetViewButton; // overview: all three range sliders to their full range
    void resetViewClicked();

    // --- bottom row: pause, Run/Fix, window, overlap, FFT size, averaging, export, colour map ---
    IconButton m_pauseButton; // pause / play symbol
    bool m_isPaused = false;
    void pauseClicked();
    TextButton m_runModeButton;
    bool m_isRunningDisplay = false;
    void runClicked();
    void setDisplayMode(bool fixed); // Fix (fixed image, running cursor) or Scroll
    ComboBox m_windowFktCombo;
    ComboBox m_overlapCombo;
    ComboBox m_fftSizeCombo;
    bool m_hideFFTSizeCombobox = false;
    // averaging along time: "Avg" label, slider with the value ("off" / ms)
    Label m_averagingLabel;
    Slider m_averagingSlider {Slider::LinearHorizontal, Slider::TextBoxRight};
    juce::NormalisableRange<double> makeAveragingRange();
    IconButton m_exportButton;
    std::unique_ptr<juce::FileChooser> m_exportChooser; // alive while the (asynchronous) dialog is open
    void exportClicked();
    ComboBox m_colorScheme;

    // --- crosshair and readout: value under the mouse, peak in its note band, harmonic cursor ---
    void mouseMove(const MouseEvent &event) override;
    void mouseExit(const MouseEvent &event) override;
    void modifierKeysChanged(const ModifierKeys& modifiers) override; // Shift: harmonic cursor
    juce::Point<int> m_mousePos;
    bool m_mouseInDisplay = false; // crosshair is drawn while the mouse is over the display
    float m_mouseFreq = 0.f;
    juce::String m_readoutText; // frequency | note | level at the mouse (shown by the crosshair)
    bool setLabelText(int x, int y); // true if (x, y) is inside the analysis display
    String noteText(double freq) const; // e.g. "A4 +4 ct" (reference pitch, overlay rounding)
    // strongest maximum in the note band of the mouse (at least +-1 bin) of the column under the
    // mouse, parabolic interpolation; second readout line and a dot at its position
    bool m_peakValid = false;
    float m_peakFreq = 0.f, m_peakLevel = 0.f;
    String m_peakText;
    bool m_showHarmonics = false; // Shift held: lines at the harmonics of the peak (or the mouse frequency)
    void drawCrosshair(juce::Graphics& g, juce::Rectangle<int> display);
    void drawHarmonics(juce::Graphics& g, juce::Rectangle<int> display) const;

    // --- notes: keyboard overlay (one band per semitone, a quarter tone below to above the note), A4 ---
    IconButton m_keyboardButton;
    bool m_keyboardOverlay = false;
    void drawKeyboardOverlay(juce::Graphics& g, juce::Rectangle<int> display, float textH) const;
    DragValueBox m_refPitchValue; // reference pitch A4 (tuning fork), below the frequency axis
    double m_refPitch = 440.0;
    void updateRefPitchText();

    // --- beats: BPM grid (metronome button, resolution, sync/free, BPM value) from host or own tempo ---
    IconButton m_bpmButton;
    bool m_bpmGrid = false;
    ComboBox m_bpmResolutionCombo;
    IconButton m_tempoSyncButton; // chain: synced to the host, broken chain: free tempo
    DragValueBox m_bpmValue; // host tempo (synced) or the tempo of the free grid (editable)
    void updateBpmLabel();
    void tempoSyncClicked();
    // musical position of one memory column (from its slice)
    struct ColumnBeat { bool has = false; double ppq = 0.0; double barStart = 0.0; float barLen = 4.f; float beatLen = 1.f; float bpm = 0.f;
                        juce::int64 endSample = -1; }; // -1: column not written yet
    std::vector<ColumnBeat> m_columnBeat; // beat position of each memory column (like m_displaymem)
    ColumnBeat m_newestColumn; // the last written column
    ColumnBeat beatOfColumn(size_t memoryColumn) const; // host position, or the free grid position
    juce::int64 lastBarLineSample(const ColumnBeat& column) const; // host bar line at or before the column end
    // synced and paused, host tempo changed since the newest column: grid at the current host tempo
    bool useLiveHostTempo() const;
    // free tempo
    bool m_tempoFree = false;
    double m_freeBpm = 120.0;
    juce::int64 m_freeAnchorSample = 0; // a bar line of the free grid (sample count of the slices)
    int m_freeNumerator = 4, m_freeDenominator = 4;
    void anchorFreeGrid(); // continue the host grid (last bar line), or start at the newest column
    bool sampleAtX(float x, juce::int64& sample) const; // sample count at x in the display
    void mouseDown(const MouseEvent& event) override; // Alt+click: bar line of the free grid here
    void drawBeatGrid(juce::Graphics& g, juce::Rectangle<int> display) const;

    // --- title bar state (controls: see the sections above) ---
    bool m_titleBarVisible = true;

    // --- attachments and bindings: declared last, so they are destroyed before their controls ---
    std::unique_ptr<RangeParameterBinding> m_freqRangeBinding;
    std::unique_ptr<RangeParameterBinding> m_colorRangeBinding;
    std::unique_ptr<RangeParameterBinding> m_timeRangeBinding;
    std::unique_ptr<SliderParameterBinding> m_averagingBinding; // own scale: see makeAveragingRange
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> m_colorSchemeAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> m_windowAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> m_fftSizeAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> m_overlapAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> m_bpmResolutionAttachment;
};
