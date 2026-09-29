// Runtime comparison of the internal FFT (TGMStaticLib, class spectrum) and juce::dsp::FFT
// for a real-valued forward transform, as used by the spectrogram (512 ... 8192 points).
// Build it as Release, the timings of a Debug build are meaningless.
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <random>
#include <vector>
#include "FFT.h"

namespace
{
using Clock = std::chrono::steady_clock;

volatile float g_sink = 0.f; // keeps the compiler from removing the transforms

const char* juceEngineName()
{
#if JUCE_MAC || JUCE_IOS
    return "Apple vDSP (Accelerate)";
#elif JUCE_DSP_USE_INTEL_MKL
    return "Intel MKL";
#elif JUCE_DSP_USE_SHARED_FFTW || JUCE_DSP_USE_STATIC_FFTW
    return "FFTW";
#else
    return "JUCE fallback (built-in)";
#endif
}

// median time per call in ns: runs `call` in batches of about 20 ms, 9 batches
template <typename Fn>
double medianNsPerCall(Fn&& call)
{
    // warm up and find a batch size of about 20 ms
    int batch = 1;
    for (;;)
    {
        auto t0 = Clock::now();
        for (int kk = 0; kk < batch; ++kk)
            call();
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (ms > 20.0)
            break;
        batch *= 2;
    }
    std::vector<double> ns;
    for (int run = 0; run < 9; ++run)
    {
        auto t0 = Clock::now();
        for (int kk = 0; kk < batch; ++kk)
            call();
        ns.push_back(std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / batch);
    }
    std::sort(ns.begin(), ns.end());
    return ns[ns.size()/2];
}
}

int main()
{
    std::printf("FFT benchmark, real-valued forward transform, float in -> N/2+1 complex bins out\n");
    std::printf("JUCE engine: %s\n", juceEngineName());
#if defined(NDEBUG)
    std::printf("build: Release (NDEBUG)\n\n");
#else
    std::printf("build: DEBUG - timings are not representative!\n\n");
#endif
    std::printf("%6s | %12s %12s | %12s | %8s | %10s\n",
                "N", "TGM float", "TGM double", "JUCE", "JUCE/TGM", "max rel err");
    std::printf("-------+---------------------------+--------------+----------+------------\n");

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);

    for (int order = 8; order <= 14; ++order)
    {
        const int N = 1 << order;
        const int bins = N/2 + 1;
        std::vector<float> input(static_cast<size_t>(N));
        for (auto& x : input)
            x = dist(rng);
        std::vector<double> inputD(input.begin(), input.end());

        // internal FFT, float interface (this is what SpectrumAnalyzer uses)
        spectrum tgm(N);
        std::vector<float> tgmRe(static_cast<size_t>(bins)), tgmIm(static_cast<size_t>(bins));
        std::vector<float> tgmIn(input);
        double tgmFloatNs = medianNsPerCall([&]{
            tgm.fft(tgmIn.data(), tgmRe.data(), tgmIm.data());
            g_sink = g_sink + tgmRe[1];
        });

        // internal FFT, double interface (no float/double conversion)
        std::vector<double> tgmReD(static_cast<size_t>(bins)), tgmImD(static_cast<size_t>(bins));
        double tgmDoubleNs = medianNsPerCall([&]{
            tgm.fft(inputD.data(), tgmReD.data(), tgmImD.data());
            g_sink = g_sink + static_cast<float>(tgmReD[1]);
        });

        // JUCE FFT: in-place, buffer of 2N floats; the copy of the input is part of the
        // measurement, because the in-place transform destroys it (the TGM FFT copies too)
        juce::dsp::FFT juceFft(order);
        std::vector<float> juceBuf(static_cast<size_t>(2*N));
        double juceNs = medianNsPerCall([&]{
            std::copy(input.begin(), input.end(), juceBuf.begin());
            juceFft.performRealOnlyForwardTransform(juceBuf.data(), true);
            g_sink = g_sink + juceBuf[2];
        });

        // correctness: both must give the same spectrum (JUCE output is interleaved re, im)
        tgm.fft(tgmIn.data(), tgmRe.data(), tgmIm.data());
        std::copy(input.begin(), input.end(), juceBuf.begin());
        juceFft.performRealOnlyForwardTransform(juceBuf.data(), true);
        double maxAbs = 0.0, maxErr = 0.0;
        for (int kk = 0; kk < bins; ++kk)
        {
            std::complex<double> a(tgmRe[size_t(kk)], tgmIm[size_t(kk)]);
            std::complex<double> b(juceBuf[size_t(2*kk)], juceBuf[size_t(2*kk+1)]);
            maxAbs = std::max(maxAbs, std::abs(b));
            maxErr = std::max(maxErr, std::abs(a - b));
        }

        std::printf("%6d | %9.0f ns %9.0f ns | %9.0f ns | %7.2fx | %10.1e\n",
                    N, tgmFloatNs, tgmDoubleNs, juceNs, juceNs / tgmFloatNs, maxErr / maxAbs);
    }
    std::printf("\nJUCE/TGM > 1: the internal FFT (float interface) is faster.\n");
    return 0;
}
