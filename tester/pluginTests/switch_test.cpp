// FFT size and window changes while the audio runs: no allocation, only valid spectrum sizes reach the GUI.
#include "JadeSpectrogram.h"
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <new>

static thread_local bool t_count = false;
static std::atomic<long> g_allocs{0};
void* operator new(std::size_t n) { if (t_count) g_allocs++; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](std::size_t n) { if (t_count) g_allocs++; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }

int main()
{
    JadeSpectrogramAudio algo;
    algo.prepareToPlay(48000.0, 512, 2);

    std::atomic<bool> done{false};
    std::atomic<long> slices{0}, badSize{0};
    long seen[8193] = {};
    std::thread gui([&]{
        std::vector<float> s;
        while (!done.load()) {
            size_t n = algo.getNextMemSliceSize();
            if (n == 0) { std::this_thread::yield(); continue; }
            if (n != 257 && n != 513 && n != 1025 && n != 2049 && n != 4097) badSize++;
            s.resize(n);
            if (algo.getMemSlice(s)) { slices++; seen[n]++; }
        }
    });

    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    const size_t sizes[] = {512, 8192, 1024, 4096, 2048, 8192, 512};
    long blocks = 0;
    for (int round = 0; round < 7; ++round)
    {
        algo.setFFTSize(sizes[round]);                       // "GUI" request
        algo.setWindowType(static_cast<SpectrumAnalyzer::WindowType>(round % 6));
        for (int b = 0; b < 400; ++b, ++blocks)
        {
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 512; ++k)
                    buf.setSample(c, k, std::sin(0.01f * float(blocks * 512 + k)));
            t_count = true;
            algo.processBlock(buf, midi);
            t_count = false;
        }
        std::printf("round %d fft %5zu -> spectrum size %zu\n", round, sizes[round], algo.getSpectrumSize());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    done = true; gui.join();
    std::printf("blocks %ld, allocations on audio thread: %ld\n", blocks, g_allocs.load());
    std::printf("slices received %ld, bad sizes %ld; per size:", slices.load(), badSize.load());
    for (int n : {257, 513, 1025, 2049, 4097}) std::printf(" %d:%ld", n, seen[n]);
    std::printf("\n");
    return (g_allocs.load() != 0 || badSize.load() != 0);
}
