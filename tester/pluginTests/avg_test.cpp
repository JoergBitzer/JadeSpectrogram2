// Averaging along time: step response at the time constant, off = bit-identical, no allocations
// while switching.
#include "JadeSpectrogram.h"
#include "alloc_count.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("  %-62s %s\n", what, ok ? "OK" : "FAIL"); fails += !ok; }
static uint64_t runHash(float tauMs)
{
    JadeSpectrogramAudio algo; algo.setAveragingMs(tauMs); algo.prepareToPlay(48000.0, 480, 2);
    juce::AudioBuffer<float> buf(2, 480); juce::MidiBuffer midi; juce::Random rnd(3);
    uint64_t h = 1469598103934665603ull; std::vector<float> s;
    for (int b = 0; b < 400; ++b)
    {
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 480; ++k) buf.setSample(c, k, 0.3f*std::sin(0.05f*float(b*480+k)) + 0.05f*(rnd.nextFloat()-0.5f));
        algo.processBlock(buf, midi);
        while (size_t n = algo.getNextMemSliceSize()) { s.resize(n); algo.getMemSlice(s);
            for (float v : s) { uint32_t x; std::memcpy(&x, &v, 4); for (int q = 0; q < 4; ++q) { h ^= (x >> (8*q)) & 0xff; h *= 1099511628211ull; } } }
    }
    return h;
}
int main()
{
    // 1) step response at the bin of a 1 kHz sine (FFT 2048, 48 kHz: hop 1024 = 21.33 ms)
    const double fs = 48000.0, tau = 0.5, hop = 1024.0/fs;
    JadeSpectrogramAudio algo; algo.setAveragingMs(float(tau*1000.0)); algo.prepareToPlay(fs, 512, 2);
    juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
    std::vector<double> p; std::vector<float> s; long n = 0; const size_t bin = size_t(std::round(1000.0/(fs/2048.0)));
    for (int b = 0; b < int(6.0*fs/512); ++b)
    {
        for (int k = 0; k < 512; ++k, ++n) // silence for 2 s, then a 1 kHz sine (same in both channels)
            for (int c = 0; c < 2; ++c) buf.setSample(c, k, n < long(2.0*fs) ? 0.f : float(0.5*std::sin(2*M_PI*1000.0*double(n)/fs)));
        algo.processBlock(buf, midi);
        while (size_t m = algo.getNextMemSliceSize()) { s.resize(m); algo.getMemSlice(s); p.push_back(std::pow(10.0, s[bin]/10.0)); }
    }
    const size_t onset = size_t(std::ceil(2.0/hop)); // first slice with the sine (roughly)
    const double pEnd = p.back();
    size_t k63 = onset; while (k63 < p.size() && p[k63] < 0.632*pEnd) ++k63;
    // theory for alpha = T/tau: (1-alpha)^n = e^-1 -> n = -1/ln(1-alpha); 50 % window overlap adds up to one hop
    const double alpha = hop/tau, nTheory = -1.0/std::log(1.0 - alpha);
    std::printf("step response: 63 %% after %.0f ms (theory %.0f ms + up to one hop for the window); end level %.1f dB\n",
                double(k63 - onset)*hop*1000.0, nTheory*hop*1000.0, 10.0*std::log10(pEnd));
    check(std::abs(double(k63 - onset) - nTheory) <= 2.0, "63 % point at the time constant (within 2 hops)");
    // after 5 tau the average has settled: last 20 slices within 0.1 dB
    double lo = 1e30, hi = 0; for (size_t k = p.size()-20; k < p.size(); ++k) { lo = std::min(lo, p[k]); hi = std::max(hi, p[k]); }
    check(10.0*std::log10(hi/lo) < 0.1, "settled after 5 tau (last 20 slices within 0.1 dB)");
    // 2) off is really off
    const uint64_t h0 = runHash(0.f), h10 = runHash(10.f), h500 = runHash(500.f);
    check(h0 == h10, "10 ms (below one hop of 10 ms? no: hop 5.3..21 ms) handled");
    std::printf("  hashes: off %016llx, 10 ms %016llx, 500 ms %016llx\n", (unsigned long long)h0, (unsigned long long)h10, (unsigned long long)h500);
    check(h0 != h500, "500 ms changes the output");
    // 3) no allocation with averaging on while switching FFT sizes
    {
        JadeSpectrogramAudio a; a.setAveragingMs(300.f); a.prepareToPlay(fs, 512, 2);
        juce::AudioBuffer<float> b2(2, 512); b2.clear();
        for (size_t f : {512, 8192, 2048, 4096, 1024})
        {
            a.setFFTSize(f);
            for (int i = 0; i < 50; ++i) { t_count = true; a.processBlock(b2, midi); t_count = false; while (size_t m = a.getNextMemSliceSize()) { s.resize(m); a.getMemSlice(s); } }
        }
        check(g_allocs == 0, "no allocation on the audio thread (averaging on, 5 FFT switches)");
    }
    std::printf("%s\n", fails ? "FAILED" : "all OK");
    return fails;
}
