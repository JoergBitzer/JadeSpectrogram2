#include <cassert>
#include <memory.h>
#include "SpectrumAnalyzer.h"


SpectrumAnalyzer::SpectrumAnalyzer(double sampleRate, size_t fftSize, size_t blockSize, OverlapPercentage overlap, WindowType type)
:m_sampleRate(sampleRate), m_windowType(type), m_overlap(overlap)
{
    setBlockSize(blockSize);
    setFFTSize(fftSize);
}
SpectrumAnalyzer::~SpectrumAnalyzer()
{
}

bool SpectrumAnalyzer::getPeriodogram(const std::vector<float> &inputSignal, std::vector<float> &outputPeriodogram)
{
    assert(outputPeriodogram.size() == m_fftSize / 2 + 1);
    assert(inputSignal.size() == m_hopSize);
    getSpectrum(inputSignal, m_fftRealBuffer, m_fftImagBuffer);

    for (size_t kk = 0; kk < m_fftSize / 2 + 1; kk++)
    {
        outputPeriodogram[kk] = (m_fftRealBuffer[kk] * m_fftRealBuffer[kk] + m_fftImagBuffer[kk] * m_fftImagBuffer[kk]);
    }
    return true;
}

bool SpectrumAnalyzer::getMagnitudeSpectrum(const std::vector<float> &inputSignal, std::vector<float> &outputMagnitudeSpectrum)
{
    assert(outputMagnitudeSpectrum.size() == m_fftSize / 2 + 1);
    assert(inputSignal.size() == m_hopSize);

    
    bool success = getPeriodogram(inputSignal, outputMagnitudeSpectrum);
    if (!success)
        return false;
    for (size_t kk = 0; kk < m_fftSize / 2 + 1; kk++)
    {
        outputMagnitudeSpectrum[kk] = sqrtf(outputMagnitudeSpectrum[kk]);
    }
    return true;
}

bool SpectrumAnalyzer::getSpectrum(const std::vector<float> &inputSignal, std::vector<float> &outputRealSpectrum, std::vector<float> &outputImagSpectrum)
{
    assert(outputRealSpectrum.size() == m_fftSize / 2 + 1);
    assert(outputImagSpectrum.size() == m_fftSize / 2 + 1);
    assert(inputSignal.size() == m_hopSize);

    // clear m_fftInputBuffer
    std::fill(m_fftInputBuffer.begin(), m_fftInputBuffer.end(), 0.f);

    if (m_overlap == OverlapPercentage::perc0)
    {
        // no overlap
        // copy to internal buffer
        memcpy(m_fftInputBuffer.data(), inputSignal.data(), m_blockSize * sizeof(float));
    }
    else if (m_overlap == OverlapPercentage::perc50)
    {
        // 50% overlap
        memcpy(m_fftInputBuffer.data(), m_mem50aIn.data(), (m_hopSize) * sizeof(float));
        memcpy(m_fftInputBuffer.data() + (m_hopSize), inputSignal.data(), (m_hopSize) * sizeof(float));
        // update memory
        memcpy(m_mem50aIn.data(), inputSignal.data(), (m_hopSize) * sizeof(float));
    }
    else if (m_overlap == OverlapPercentage::perc75)
    {
        // 75% overlap
        memcpy(m_fftInputBuffer.data(), m_mem25aIn.data(), (m_hopSize) * sizeof(float));
        memcpy(m_fftInputBuffer.data() + (m_hopSize), m_mem25bIn.data(), (m_hopSize) * sizeof(float));
        memcpy(m_fftInputBuffer.data() + 2 * (m_hopSize), m_mem25cIn.data(), (m_hopSize) * sizeof(float));
        memcpy(m_fftInputBuffer.data() + 3 * (m_hopSize), inputSignal.data(), (m_hopSize) * sizeof(float));

        // update memory
        memcpy(m_mem25cIn.data(), m_mem25bIn.data(), (m_hopSize) * sizeof(float));
        memcpy(m_mem25bIn.data(), m_mem25aIn.data(), (m_hopSize) * sizeof(float));
        memcpy(m_mem25aIn.data(), inputSignal.data(), (m_hopSize) * sizeof(float));
    }
    for (size_t kk = 0; kk < m_blockSize; kk++)
    {
        m_fftInputBuffer[kk] *= m_window[kk];
    }
    // compute FFT (via pointer, because of float/double overloads)
    m_fft.fft(m_fftInputBuffer.data(), outputRealSpectrum.data(), outputImagSpectrum.data());

    return true;
}

bool SpectrumAnalyzer::getFrequencyAxis(std::vector<float> &frequencyAxis)
{
    assert(frequencyAxis.size() == m_fftSize / 2 + 1);
    for (size_t kk = 0; kk < m_fftSize / 2 + 1; kk++)
    {
        frequencyAxis[kk] = static_cast<float>(kk * m_sampleRate / m_fftSize);
    }
    return true;
}

void SpectrumAnalyzer::reset()
{
    std::fill(m_mem50aIn.begin(), m_mem50aIn.end(), 0.f);
    std::fill(m_mem25aIn.begin(), m_mem25aIn.end(), 0.f);
    std::fill(m_mem25bIn.begin(), m_mem25bIn.end(), 0.f);
    std::fill(m_mem25cIn.begin(), m_mem25cIn.end(), 0.f);
}

void SpectrumAnalyzer::setSampleRate(double sampleRate)
{
    m_sampleRate = sampleRate;
}

void SpectrumAnalyzer::setBlockSize(size_t blockSize)
{
    m_blockSize = blockSize;
    switch (m_overlap)
    {
        case OverlapPercentage::perc0:
            m_hopSize = m_blockSize;
            break;
        case OverlapPercentage::perc50:
            m_hopSize = m_blockSize / 2;
            break;
        case OverlapPercentage::perc75:
            m_hopSize = m_blockSize / 4;
            break;
    }
    setWindowFunction();

    m_mem25aIn.resize(m_blockSize / 4);
    m_mem25bIn.resize(m_blockSize / 4);
    m_mem25cIn.resize(m_blockSize / 4);
    m_mem50aIn.resize(m_blockSize / 2);
}

void SpectrumAnalyzer::setFFTSize(size_t fftSize) // can be different from block size (zero padding)
{
    m_fftSize = fftSize;
    m_fft.setFFTSize(static_cast<int>(fftSize));
    m_fftRealBuffer.resize(fftSize / 2 + 1);
    m_fftImagBuffer.resize(fftSize / 2 + 1);
    m_fftInputBuffer.resize(fftSize);

}

void SpectrumAnalyzer::setOverlap(OverlapPercentage overlap)
{
    m_overlap = overlap;
    setBlockSize(m_blockSize); // to update hop size and memory buffers
    setFFTSize(m_fftSize); // to update FFT buffers
}

std::string SpectrumAnalyzer::getWindowTypeAsString(WindowType type) const
{
    switch (type)
    {
        case WindowType::Rect:
            return "Rectangular";
        case WindowType::Hann:
            return "Hann";
        case WindowType::Hamming:
            return "Hamming";
        case WindowType::BlackmanHarris:
            return "Blackman-Harris";
        case WindowType::FlatTop:
            return "Flat Top";
        case WindowType::HannPoisson:
            return "Hann-Poisson";
        case WindowType::NrOfWindowTypes: // not a window, only the count
        default:
            return "Unknown";
    }
}

void SpectrumAnalyzer::setWindowFunction()
{
    m_window.resize(m_blockSize);
    float normalizeFactor = 0.f;
    for (size_t kk = 0; kk < m_blockSize ; kk++)
    {
        float a0;
        float a1;
        float a2;
        float a3;
        float a4;
        float alpha = 2.f;
        switch (m_windowType)
        {
            case WindowType::Rect:
                m_window[kk] = 1.f;
                break;
            case WindowType::Hann:
                m_window[kk] = 0.5f*(1.f-cosf(2.f*M_PI*kk/m_blockSize));
                break;
            case WindowType::Hamming:
                m_window[kk] = 25.f/46.f-(1.f-25.f/46.f)*cosf(2.f*M_PI*kk/m_blockSize);
                break;
            case WindowType::BlackmanHarris:
                a0 = 0.35875f;
                a1 = 0.48829f;
                a2 = 0.14128f;
                a3 = 0.01168f;
                m_window[kk] = a0 - a1*cosf(2.f*M_PI*kk/m_blockSize) + a2*cosf(4.f*M_PI*kk/m_blockSize) - a3*cosf(6.f*M_PI*kk/m_blockSize);
                break;
            case WindowType::FlatTop:
                a0 = 0.21557895f;
                a1 = 0.41663158f;
                a2 = 0.277263158f;
                a3 = 0.083578947f;
                a4 = 0.006947368f;
                m_window[kk] = a0 - a1*cosf(2.f*M_PI*kk/m_blockSize) + a2*cosf(4.f*M_PI*kk/m_blockSize) 
                             - a3*cosf(6.f*M_PI*kk/m_blockSize) + a4*cosf(8.f*M_PI*kk/m_blockSize);
                break;
            case WindowType::HannPoisson:

                m_window[kk] = 0.5f*(1.f-cosf(2.f*M_PI*kk/m_blockSize))
                                *expf(-alpha*fabsf(static_cast<float> (m_blockSize-2*kk))/m_blockSize);
                break;
            // More window types can be added here (example: gaussian, tukey, Kaiser, ...)

            case WindowType::NrOfWindowTypes: // not a window, only the count
            default:
                m_window[kk] = 1.f;
                break;
        }
        normalizeFactor += m_window[kk]*m_window[kk];
    }
    normalizeFactor /= m_blockSize;
    normalizeFactor = sqrt(normalizeFactor);
    for (size_t kk = 0; kk < m_blockSize ; kk++)
    {
        m_window[kk] /= normalizeFactor;
    }
}
