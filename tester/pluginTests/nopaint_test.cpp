// Scrolling display (2.4.6): timer ticks without paint in between, then the image against a full
// rebuild of the image from the memory. Before 2.4.6, 424 of 430 columns were wrong with a paint
// after every third tick.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#include <cmath>
#define private public
#define protected public
#include "PluginProcessor.h"
#include "PluginEditor.h"
#undef private
#undef protected
int main()
{
    int fails = 0;
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 44100.0; const int block = 256;
    for (int ticksWithoutPaint : {1, 3, 1000, -1})
    {
        auto proc = std::make_unique<JadeSpectrogramAudioProcessor>();
        proc->setPlayConfigDetails(2, 2, fs, block); proc->prepareToPlay(fs, block);
        auto* ed = dynamic_cast<JadeSpectrogramAudioProcessorEditor*>(proc->createEditorIfNeeded());
        auto& gui = ed->m_editor; gui.timerCallback();
        juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi; double ph = 0; long n = 0;
        juce::Image dummy(juce::Image::ARGB, 10, 10, true);
        for (int b = 0; b < int((ticksWithoutPaint < 0 ? 15.0 : 6.0) * fs / block); ++b) {
            for (int i = 0; i < block; ++i, ++n) { const float v = float(0.3 * std::sin(ph)); ph += 2.0 * M_PI * (300.0 + 2000.0 * std::fmod(double(n) / fs, 2.0)) / fs; buf.setSample(0, i, v); buf.setSample(1, i, v); }
            proc->m_algo.processBlock(buf, midi);
            if (ticksWithoutPaint > 0 && b % 7 == 0) { gui.timerCallback(); if ((b / 7) % ticksWithoutPaint == 0) { juce::Graphics g(dummy); gui.paint(g); } } }
        gui.timerCallback(); // the last spectra into the image (without paint in between, as above)
        juce::Image incremental = gui.m_internalImg.createCopy();
        gui.m_recomputeAll = true; gui.timerCallback();
        juce::Image rebuilt = gui.m_internalImg.createCopy();
        int diffCols = 0; for (int x = 0; x < rebuilt.getWidth(); ++x) { bool d = false; for (int y = 0; y < rebuilt.getHeight() && !d; y += 3) d = incremental.getPixelAt(x, y) != rebuilt.getPixelAt(x, y); diffCols += d; }
        if (ticksWithoutPaint < 0) std::printf("no timer for 15 s, then one tick: %d of %d image columns differ from a full rebuild\n", diffCols, rebuilt.getWidth()); else std::printf("paint after every %d timer tick(s): %d of %d image columns differ from a full rebuild\n", ticksWithoutPaint, diffCols, rebuilt.getWidth());
        fails += diffCols != 0;
    }
    std::printf(fails ? "FAILED\n" : "all OK\n");
    return fails ? 1 : 0;
}
