// Fingerprint of the analysis: FNV-1a hash over all spectrum slices for a fixed signal,
// with FFT size, window and mix mode changes in between. Must stay identical for refactors:
// a change of the analysis (on purpose) needs a new baseline below.
#include "JadeSpectrogram.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
int main()
{
    JadeSpectrogramAudio algo;
    algo.prepareToPlay(44100.0, 480, 2);
    juce::AudioBuffer<float> buf(2, 480); juce::MidiBuffer midi;
    uint64_t h = 1469598103934665603ull; long slices = 0;
    std::vector<float> s;
    auto drain = [&]{ while (size_t n = algo.getNextMemSliceSize()) { s.resize(n); algo.getMemSlice(s);
        for (float v : s) { uint32_t b; std::memcpy(&b, &v, 4); for (int k = 0; k < 4; ++k) { h ^= (b >> (8*k)) & 0xff; h *= 1099511628211ull; } } ++slices; } };
    juce::Random rnd(7); long n = 0;
    const size_t ffts[] = {2048, 512, 8192, 1024, 4096};
    for (int round = 0; round < 10; ++round)
    {
        algo.setFFTSize(ffts[round % 5]);
        algo.setWindowType(static_cast<SpectrumAnalyzer::WindowType>(round % 6));
        algo.setChannelMixMode(static_cast<JadeSpectrogramAudio::ChannelMixMode>(round % 6));
        for (int b = 0; b < 150; ++b)
        {
            for (int k = 0; k < 480; ++k, ++n)
            {
                float t = float(n) / 44100.f;
                buf.setSample(0, k, 0.4f*std::sin(2.f*float(M_PI)*440.f*t) + 0.01f*(rnd.nextFloat()-0.5f));
                buf.setSample(1, k, 0.3f*std::sin(2.f*float(M_PI)*(200.f+300.f*t)*t));
            }
            algo.processBlock(buf, midi);
            drain();
        }
    }
    const bool ok = slices == 1086 && h == 0xa24f0ec56cfbff48ull; // baseline since 1.3 (September 2026)
    std::printf("slices %ld hash %016llx (baseline 1086 a24f0ec56cfbff48) %s\n", slices, (unsigned long long)h, ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
