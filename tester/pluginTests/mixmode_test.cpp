// Channel mix modes: TimeMean and AbsMean give the same spectrum for L = R.
#include "JadeSpectrogram.h"
#include <cstdio>
#include <cmath>
// L == R: TimeMean must give the same spectrum as AbsMean (before the fix it stayed at the initial values)
static std::vector<float> lastSlice(JadeSpectrogramAudio::ChannelMixMode mode)
{
    JadeSpectrogramAudio algo;
    algo.setChannelMixMode(mode);
    algo.prepareToPlay(48000.0, 512, 2);
    juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
    for (int b = 0; b < 20; ++b)
    {
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 512; ++k) buf.setSample(c, k, 0.5f*std::sin(0.1f*float(b*512+k)));
        algo.processBlock(buf, midi);
    }
    std::vector<float> s, last;
    while (size_t n = algo.getNextMemSliceSize()) { s.resize(n); algo.getMemSlice(s); last = s; }
    return last;
}
int main()
{
    auto absMean = lastSlice(JadeSpectrogramAudio::ChannelMixMode::AbsMean);
    auto timeMean = lastSlice(JadeSpectrogramAudio::ChannelMixMode::TimeMean);
    size_t peak = 0; float maxdiff = 0.f;
    for (size_t k = 0; k < absMean.size(); ++k) { if (absMean[k] > absMean[peak]) peak = k; maxdiff = std::max(maxdiff, std::fabs(absMean[k]-timeMean[k])); }
    std::printf("AbsMean peak bin %zu: %.1f dB, TimeMean same bin: %.1f dB, max diff over all bins %.2e dB\n",
                peak, absMean[peak], timeMean[peak], maxdiff);
    return maxdiff > 1e-3f;
}
