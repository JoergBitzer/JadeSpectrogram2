#include <iostream>
#include <vector>
#include "../../TGMStaticLib/FFT.h"
#include "../../JadeSpectrogram/SpectrumAnalyzer.h"

int main()
{
    int fftSize = 4*256;
    int blocksize = 256;
    double fs = 8000;
    // check spectrogram with simple data
    std::vector<float> in;

    std::vector<float> Periodogram;
    Periodogram.resize(fftSize/2+1);

    SpectrumAnalyzer spec(fs,fftSize,blocksize,SpectrumAnalyzer::OverlapPercentage::perc0,SpectrumAnalyzer::WindowType::HannPoisson);
    size_t hopSize = spec.getHopSize();
    std::cout << "Hop size: " << hopSize << std::endl;
    in.resize(hopSize);


    float f0 = 250.0f;
    int kk = 0;
    int nr_of_frames = 1;
    for (int frame = 0; frame < nr_of_frames; frame++)
    {
        std::cout << "Frame " << frame << ":" << std::endl;
        for(int n=0;n<hopSize;n++)
        {
            if (frame<5)
                in[n] = static_cast<float>(std::sin(2.0*M_PI*f0*(static_cast<double>(kk++)/fs)));
            else
                in[n] = static_cast<float>(std::sin(2.0*M_PI*3*f0*(static_cast<double>(kk++)/fs)));

        }
        spec.getPeriodogram(in,Periodogram);
        std::cout << "Periodogram values: " << std::endl;
        for(int k=0;k<fftSize/2+1;k++)
        {
            std::cout << "Bin " << k << ": " << Periodogram[k] << std::endl;
        }
    }
    return 0;
}