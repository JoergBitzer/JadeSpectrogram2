# JadeSpectrogram2 – planning

Ideas for improving the plugin, collected from a code review (September 2026, v1.2.4;
updated for v1.3.0).
Items marked **bug** are verified in the code; the rest are proposals. Within each section
the most useful items come first.

## 1. Bugs found during the review

All fixed in v1.2.5: the `TimeMean` mix mode left the spectrum stale (missing `switch`
case), the default colormap was set in three different ways (now Plasma everywhere), the
mouse readout could index past the spectrum at fs/2, and the README claimed the JUCE FFT
(the plugin uses the internal FFT from `TGMStaticLib`).

## 2. Real-time safety (remaining items)

Done: lock-free SPSC FIFO (v1.2.2), FFT size/window switch without lock or allocation
(v1.2.3), latency reported to the host (v1.2.4).

- Make `m_PauseMode` and `m_mixMode` `std::atomic` (GUI writes, audio thread reads).
- `m_fs` is written in `prepareToPlay` and read by the GUI: make it atomic, or send the
  sample rate together with the slices, as is already done for the slice size.
- `SynchronBlockProcessor` uses `MidiBuffer::addEvents`, which can allocate when MIDI
  arrives. The spectrogram doesn't need MIDI: skip the MIDI handling, or reserve the buffer.
- Remove dead synchronisation: the local `CriticalSection crit` in `paint()`/`resized()`
  (it locks a new object every call, so it protects nothing), the unused
  `CriticalSection m_protect` in the processor and in `SynchronBlockProcessor`.

## 3. Zero latency instead of reported latency

Done in v1.3.0: `SynchronBlockProcessor` (tools, version 2.2) has a
`ProcessingMode::Analyze`. The audio passes unchanged and without delay, and the synchron
blocks are only analysed. The spectrogram uses it, so its latency is 0 for every FFT size.
Other analyzers can use the same mode with `setProcessingMode(ProcessingMode::Analyze)`.
The latency timer from v1.2.4 stays in the processor for the Process mode case.

## 4. Analysis features

- **Logarithmic frequency axis** (and optionally Mel/Bark/ERB), the usual view for music
  and hearing research; the axis currently is linear only.
- **Overlap selection** (0 / 50 / 75 %): `SpectrumAnalyzer` supports `perc75` already, but
  the plugin always uses 50 %. 75 % gives a smoother time axis at the same FFT size.
- **Zero padding**: `SpectrumAnalyzer` supports FFT size > block size; expose it to get a
  smoother frequency axis without losing time resolution.
- **Multi-resolution / time-frequency trade-off** (from the old README): combine a short and
  a long FFT, e.g. per frequency band (long FFT for the low frequencies), or multiply two
  spectrograms of the same frequency grid (short window zero-padded × long window).
  Alternatives worth a look: the reassigned spectrogram, or a constant-Q transform.
- **Stereo views**: L and R side by side, M/S, and a coherence or inter-channel level/phase
  difference display. Would make use of the existing per-channel periodograms.
- **Smoothing / averaging** along time (exponential), and a peak-hold mode.
- **Spectrum slice** at the mouse position: a small line plot of the spectrum at the
  selected time (and the time signal of one frequency bin).
- **Calibration**: an offset in dB so the display can show dB SPL instead of dBFS,
  and a clear unit on the colour bar (the values are a PSD, `10·log10(2·P/fs)`, i.e. dB/Hz).

## 5. Display and GUI

- **Time axis** with seconds, and time zoom (from the old README).
- **Export**: save the current image as PNG, and the data as CSV/MAT (from the old README).
- **Persist the GUI settings** in the plugin state: FFT size, window, colormap, run/fix
  mode and mix mode are not saved today (only the four slider parameters and the scale
  factor are). Either make them `AudioParameterChoice` parameters or store them in the
  `ValueTree`.
- **Faster drawing**: every pixel goes through `BitmapData::setPixelColour` and
  `juce::Colour`. Writing directly into the line pointers of `Image::BitmapData` (and
  caching the palette as ARGB `uint32`) would make large windows and 8192-point FFTs cheaper.
- **Crosshair / cursor** that shows frequency, note and level directly at the mouse.
- **Colour bar labels** follow `g_minColorVal/g_maxColorVal`; they could follow the
  min/max colour sliders so the labels match what is displayed.

## 6. Code quality and tests

- Turn the test programs used during the real-time work into targets under `tester/`:
  FIFO stress test (run with `-fsanitize=thread`), allocation counter around
  `processBlock` while switching FFT size/window, latency measurement with an impulse.
- Set up CI (GitHub Actions) for Linux/Windows/macOS builds that run these tests.
- `TGMStaticLib` links `juce::juce_gui_basics` itself, so it compiles its own copy of the
  JUCE modules, with other options than the plugin (e.g. `JUCE_USE_CURL` on, so the Linux CI
  needs the curl headers). Better: let the library only use the JUCE headers and have the
  plugin provide the modules.
- Reduce warnings (sign conversions, `-Wswitch`, shadowed `p` in `timerCallback`).
- Remove unused code: `BlockFreeFiFo.h`, `m_fftsize`/`setFFTSize` in the processor,
  `getBlock()`/`getNumAvailableToRead()` if they stay unused, the `WOLA` class if not needed.
- FFT: keep the internal FFT. `tester/fftBenchmark` (Release, Linux, JUCE fallback engine)
  measured it 2.0-2.8x faster than `juce::dsp::FFT` for 512-8192 points, with identical
  results (max. relative difference 1.5e-7). On macOS JUCE would use Apple vDSP instead;
  run the benchmark there before drawing conclusions for Mac.

## Suggested order

1. Remaining real-time items (section 2) and the tests/CI (section 6).
2. Log frequency axis, overlap selection, saving the GUI settings.
3. Larger features: stereo views, multi-resolution, export.

## next steps on a finer scale
1. Remaining real-time items (section 2)
2. ~~log frequency axis with a small button to switch between lin/log~~ -- done in 1.5.0 (lower limit at least 20 Hz in log mode).
3. ~~**Crosshair / cursor** that shows frequency, note and level directly at the mouse~~ -- done in 1.4.0.
4. **Export**: copy the current image (just the spectrogram with axis) to the clipboard,
5. - **Smoothing / averaging** along time (exponential, tau as smoothing paramater should be a slider)
6. ~~`TGMStaticLib` compiles its own copy of the JUCE modules~~ -- done in 1.3.4: the plugin
  compiles only `TGMStaticLib/FFT.cpp` (no JUCE dependency), the library is not built.
7. ~~Reduce warnings~~ -- done in 1.3.5: no warnings left in the plugin code (GCC, JUCE recommended warning flags).
8. add a small transparent overlay (button to switch on/off) that shows a musical keyboard (white and black stripes) and the note names for the frequencies on the left side of the spectrogram.
