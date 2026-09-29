# JadeSpectrogram2 – planning

Ideas for improving the plugin, collected from a code review (September 2026, v1.2.4).
Items marked **bug** are verified in the code; the rest are proposals. Within each section
the most useful items come first.

## 1. Bugs found during the review

- **bug: `TimeMean` mix mode leaves the spectrum stale.** `processSynchronBlock` averages
  the channels in the time domain for `TimeMean`, but the `switch (m_mixMode)` that fills
  `m_power` has no `TimeMean` case, so `m_power` keeps the previous values (the compiler
  warns: `enumeration value 'TimeMean' not handled in switch`). This is not visible yet
  because no GUI element calls `setChannelMixMode`. Fix: `case TimeMean: m_power = m_perLeft;`.
- **bug: the default colormap is inconsistent.** The palette is constructed as `kHot`, the
  combo box shows item 7 "Jade" (`setSelectedItemIndex(6, dontSendNotification)`, so the
  palette is not changed), and the README says the default is plasma. Choose one and apply it
  in the constructor.
- **bug: the mouse readout can index past the spectrum.** `setLabelText` computes
  `freqindex = m_internalHeight * freq / fshalf`. At `freq == fs/2` (top of the display
  when the max frequency is clamped to fs/2, e.g. fs ≤ 40 kHz) the index equals
  `m_internalHeight`, and `m_displaymem.at()` throws. Use `(m_internalHeight - 1)` and clamp.
- **README claims a JUCE FFT**, but the analyzers use the `spectrum` class from
  `TGMStaticLib/FFT.h` (Uwe Simmer's FFT, double precision internally). Either fix the
  README or actually switch (see 5.).

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

The plugin is a pure analyzer, but `SynchronBlockProcessor` delays the audio output by the
hop size (256 … 4096 samples). v1.2.4 reports this to the host, but many hosts only apply a
latency change when the transport stops, and a changing latency is annoying in a mix.

- Better: pass the audio through unchanged and only *copy* it into the analysis buffer.
  The output is then bit-identical to the input, the latency is 0 and never changes, and
  most of the `SynchronBlockProcessor` output bookkeeping disappears.

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
- Reduce warnings (sign conversions, `-Wswitch`, shadowed `p` in `timerCallback`).
- Remove unused code: `BlockFreeFiFo.h`, `m_fftsize`/`setFFTSize` in the processor,
  `getBlock()`/`getNumAvailableToRead()` if they stay unused, the `WOLA` class if not needed.
- Consider the JUCE FFT (`juce::dsp::FFT`) or a float FFT: the TGM `spectrum` class
  computes in double and converts, which costs time at large FFT sizes.

## Suggested order

1. Fix the bugs in section 1 (small, visible to users).
2. Zero-latency pass-through (section 3): removes a whole class of host problems.
3. Remaining real-time items (section 2) and the tests/CI (section 6).
4. Log frequency axis, overlap selection, saving the GUI settings.
5. Larger features: stereo views, multi-resolution, export.
