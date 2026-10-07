// The editor must open without the about box, also when the memory is not empty (fill the heap first).
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#include <cstring>
#define private public
#define protected public
#include "PluginProcessor.h"
#include "PluginEditor.h"
#undef private
#undef protected
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    int open = 0, total = 0;
    for (int i = 0; i < 40; ++i)
    {
        { std::vector<void*> junk; for (int k = 0; k < 200; ++k) { void* p = std::malloc(size_t(1000 + 997 * k)); std::memset(p, 0xA5 + k, size_t(1000 + 997 * k)); junk.push_back(p); } for (auto* p : junk) std::free(p); }
        auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
        proc->setPlayConfigDetails(2, 2, 48000.0, 512); proc->prepareToPlay(48000.0, 512);
        auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
        open += ed->m_aboutboxvisible ? 1 : 0; ++total;
        proc->editorBeingDeleted(ed); delete ed;
    }
    std::printf("editor opened %d times, about box visible %d times -> %s\n", total, open, open == 0 ? "OK" : "FAIL");
    return open == 0 ? 0 : 1;
}
