#include <iostream>
#include <vector>
#include "../../TGMStaticLib/FFT.h"
#include "../../JadeSpectrogram/SpectrumAnalyzer.h"

int main()
{
    int fftSize = 1024;
    int blocksize = 1024;
    double fs = 8000;
    // check spectrogram with simple data
    std::vector<float> in;
    in.resize(blocksize);
    std::vector<float> Periodogram;
    Periodogram.resize(fftSize/2+1);

    SpectrumAnalyzer spec(fs,fftSize,blocksize,SpectrumAnalyzer::OverlapPercentage::perc0,SpectrumAnalyzer::WindowType::Hann);

    spec.getPeriodogram(in,Periodogram);

    return 0;
}