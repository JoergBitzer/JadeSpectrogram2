// Impulse through processBlock: zero latency for every FFT size and host block size.
#include "JadeSpectrogram.h"
#include <cstdio>
// Impulse through processBlock, measure where it comes out, compare with getLatency().
int main()
{
    int fails = 0;
    for (size_t fft : {512, 1024, 2048, 4096, 8192})
      for (int hostBlock : {64, 512, 1000})
      {
        JadeSpectrogramAudio algo;
        algo.setFFTSize(fft);
        algo.prepareToPlay(48000.0, hostBlock, 2);
        juce::AudioBuffer<float> buf(2, hostBlock);
        juce::MidiBuffer midi;
        long pos = 0, found = -1;
        const long impulseAt = 777;
        while (pos < 40000 && found < 0)
        {
            buf.clear();
            if (impulseAt >= pos && impulseAt < pos + hostBlock)
                for (int c = 0; c < 2; ++c) buf.setSample(c, int(impulseAt - pos), 1.f);
            algo.processBlock(buf, midi);
            for (int k = 0; k < hostBlock; ++k)
                if (buf.getSample(0, k) > 0.5f) { found = pos + k; break; }
            pos += hostBlock;
        }
        std::printf("fft %5zu block %4d: measured delay %5ld, getLatency %5d %s\n", fft, hostBlock,
                    found - impulseAt, algo.getLatency(), (found - impulseAt == algo.getLatency()) ? "OK" : "MISMATCH");
        fails += (found - impulseAt != algo.getLatency() || algo.getLatency() != 0); // zero latency (Analyze mode)
      }
    std::printf(fails ? "FAILED\n" : "all OK\n");
    return fails ? 1 : 0;
}
