#pragma once

// define pi
#include <cmath>
#if __cplusplus < 202002L
    #ifndef M_PI
        #define M_PI 3.14159265358979323846f
    #endif
#else 
    #include <numbers>
    #define M_PI std::numbers::pi 
#endif


#include <vector>
#include <string>
#include "FFT.h"

class SpectrumAnalyzer
{   
public:
    enum class WindowType
    {
        Rect,
        Hann,
        Hamming,
        BlackmanHarris,
        FlatTop,
        HannPoisson,
        NrOfWindowTypes 
    };
    enum class OverlapPercentage
    {
        perc0,
        perc50,
        perc75
    };

    SpectrumAnalyzer(){};
    SpectrumAnalyzer(double sampleRate,  size_t fftSize, size_t blockSize, OverlapPercentage overlap = OverlapPercentage::perc50, WindowType type = WindowType::Hann);
    ~SpectrumAnalyzer();

    bool getPeriodogram(const std::vector<float> &inputSignal, std::vector<float> &outputPeriodogram);
    bool getMagnitudeSpectrum(const std::vector<float>& inputSignal, std::vector<float>& outputMagnitudeSpectrum);
    bool getSpectrum(const std::vector<float>& inputSignal, std::vector<float>& outputRealSpectrum, std::vector<float>& outputImagSpectrum);
    bool getFrequencyAxis(std::vector<float>& frequencyAxis);
    size_t getHopSize() const { return m_hopSize; }
    void setSampleRate(double sampleRate);
    void setBlockSize(size_t blockSize);
    void setFFTSize(size_t fftSize);
    void setOverlap(OverlapPercentage overlap);
    void setWindowType(WindowType type){m_windowType = type; setWindowFunction(); };
    std::string getWindowTypeAsString(WindowType type) const;

private:
    double m_sampleRate = 44100.0;
    size_t m_blockSize = 1024;
    size_t m_fftSize = 1024;
    size_t m_hopSize = 512; // default 50% overlap
    WindowType m_windowType = WindowType::Hann;
    OverlapPercentage m_overlap = OverlapPercentage::perc50;

    std::vector<float> m_window;
    spectrum m_fft;
    void setWindowFunction();

    // memory blocks for 50% overlap
    std::vector<float> m_mem50aIn;
  
    // memory blocks for 75% overlap
    std::vector<float> m_mem25aIn;
    std::vector<float> m_mem25bIn;
    std::vector<float> m_mem25cIn;
  

    std::vector<float> m_fftInputBuffer;
    std::vector<float> m_fftRealBuffer;
    std::vector<float> m_fftImagBuffer;
};

