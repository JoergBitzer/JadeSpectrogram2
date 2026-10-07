// SynchronBlockProcessor with MIDI input: does processBlock allocate?
#include "tools/SynchronBlockProcessor.h"
#include <cstdio>
#include <cstdlib>
#include <new>
static thread_local bool t_count = false; static long g_allocs = 0;
void* operator new(std::size_t n) { if (t_count) ++g_allocs; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](std::size_t n) { if (t_count) ++g_allocs; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
struct Count : SynchronBlockProcessor { long events = 0;
    int processSynchronBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& m, int) override { events += m.getNumEvents(); return 0; } };
int main()
{
    Count p; p.setProcessingMode(SynchronBlockProcessor::ProcessingMode::Analyze);
    p.prepareSynchronProcessing(2, 1024);
    juce::AudioBuffer<float> buf(2, 256); juce::MidiBuffer midi; midi.ensureSize(4096);
    for (int b = 0; b < 400; ++b)
    {
        midi.clear();
        for (int e = 0; e < 8; ++e) midi.addEvent(juce::MidiMessage::noteOn(1, 60 + e, 0.8f), e * 30);
        t_count = true; p.processBlock(buf, midi); t_count = false;
    }
    std::printf("MIDI events forwarded %ld, allocations in processBlock: %ld\n", p.events, g_allocs);
    return g_allocs != 0;
}
