# JadeSpectrogram2 – planning

Ideas for improving the plugin, collected from a code review (September 2026, v1.2.4;
updated for v1.5.2). The agreed plan for version 2.0 is at the end of this file.
Items marked **bug** are verified in the code; the rest are proposals. Within each section
the most useful items come first.

## 1. Bugs found during the review

All fixed in v1.2.5: the `TimeMean` mix mode left the spectrum stale (missing `switch`
case), the default colormap was set in three different ways (now Plasma everywhere), the
mouse readout could index past the spectrum at fs/2, and the README claimed the JUCE FFT
(the plugin uses the internal FFT from `TGMStaticLib`).

## 2. Real-time safety

Done: lock-free SPSC FIFO (v1.2.2), FFT size/window switch without lock or allocation
(v1.2.3), latency reported to the host (v1.2.4), atomic GUI flags, no dead locks and a
reserved MIDI buffer (v1.3.3).

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

See "Version 2.0" below. Ideas from sections 4-6 that are not part of it (zero padding,
multi-resolution, stereo views, spectrum slice, calibration, faster drawing, tests in
`tester/`) stay candidates for later versions.

## Done on the way to 2.0

- 1.3.3 real-time items (section 2), 1.3.4 only `TGMStaticLib/FFT.cpp` is built,
  1.3.5 no compiler warnings left
- 1.4.0 crosshair with frequency, note and level at the mouse
- 1.5.0 switchable log frequency axis (lower limit at least 20 Hz in log mode)
- 1.5.1 / 1.5.2 linear axis: bins exactly at their frequency, maximum per pixel when there
  are more bins than pixel rows, frequency labels at round steps

# Version 2.0 -- "the musical spectrogram"

Agreed on 2026-09-29. Intermediate versions do not have to look finished, as long as the
final layout below is the target.

## Decisions

1. The plugin grows by the new time zoom strip and the time axis labels, and gets wider for
   the title bar controls: base size about **840 x 580** instead of 800 x 550 (still freely
   scalable, fixed aspect ratio). The title image stays.
2. The readout label in the bottom row is removed; the crosshair shows the same values.
3. Pause becomes a small icon button (pause / play symbols).
4. Averaging gets a horizontal slider in the bottom row.
5. All overlay controls (and everything else that would have gone into an extra control
   row) go into the **title bar**, to save space: Lin/Log (stays above the frequency axis),
   keyboard overlay, BPM grid with its resolution box and the BPM value, Export.
6. Export saves the visible spectrogram with its axes as a **PNG file** (file dialog).
   JUCE's clipboard only takes text; copying the image to the clipboard would need native
   code for each platform and can come later.
7. **Range sliders** (one slider with two thumbs, `TwoValueHorizontal` / `TwoValueVertical`)
   for the time axis, the frequency axis and the colour range. The thumbs cannot cross, and
   dragging between them moves the whole window. They replace the two separate frequency
   sliders and the two colour sliders (colour: thumbs at the current min/max of the colour
   range).
8. Time axis labels **below** the spectrogram, the time zoom slider above it.
   Scroll mode (default): time relative to now, -10 s ... 0 s. Fix mode (fixed image,
   running cursor): **sweep time** as on an ECG monitor, 0 s at the left edge ... 10 s at the
   right; the labels stay still and the cursor shows the current position of the sweep.
   With time zoom the labels show the selected part (e.g. -6 s ... -3 s, or 4 s ... 7 s).
9. **All settings are saved** with the project: FFT size, window, overlap, colormap,
   lin/log, Run/Fix, averaging, keyboard and BPM overlay, BPM resolution, time and frequency
   range, colour range, window size. The new ones are parameters that are not automatable
   (they do not clutter the host's automation list).

## Target layout (base size about 840 x 580)

```
+--------------------------------------------------------------------------------+
|[Lin] JadeSpectrogram ...          [keys][BPM][1/4 v] 120 BPM [export]  [logo]  |  title bar
|      ======[#################################]======    time zoom (range)      |  NEW
+--+---+---------------------------------------------------------+----+----+----+
|  |20k|                                                         |    | 20 |    |
|/\|   |                                                         | co |    | /\ |  frequency range
|  |   |                  spectrogram                            | lo |    |    |  slider (left),
|\/|   |                                                         | ur |    | \/ |  colour range
|  |  1|                                                         |    |-80 |    |  slider (right)
+--+---+---------------------------------------------------------+----+----+----+
|      | -10 s     -8 s     -6 s     -4 s     -2 s      0 s      |              |  NEW time axis
|      |  (Fix mode: 0 s ... 10 s sweep time, cursor = now)         |              |
|      |[||][Fix] [Hann v] [50% v] [2048 v]  Avg ===o==== 120 ms  | [cmap v]     |  bottom row
+--------------------------------------------------------------------------------+
```

## Steps (one branch and one minor version each)

1. ~~**1.6.0 Layout**~~ -- done: base size 840 x 580, time axis labels (relative time in
   Scroll mode, sweep time in Fix mode), empty zoom strip, Pause as icon button, readout
   label removed, bottom row left to right below the display.
2. ~~**1.7.0 Save all settings**~~ -- done: FFT size, window, colour map, lin/log and
   Run/Fix are non-automatable parameters (JadeParamID); the following steps add theirs.
3. ~~**1.8.0 Range sliders for frequency and colour**~~ -- done: `RangeSlider` (drag between
   the thumbs moves the range) and `RangeParameterBinding` (RangeSlider.h), selected range
   in light red.
4. ~~**1.9.0 Averaging**~~ -- done (step response: 63 % after 491 ms at tau = 500 ms). (item 5 of the old list): exponential smoothing along time, a first
   order IIR filter per bin on the power spectrum in the audio thread (before the dB
   conversion, so it does not depend on the GUI frame rate), coefficient from tau and the
   hop size. The leftmost slider position is off (tau below one block: alpha = 1). The value
   is shown in ms. Reset when the FFT size changes.
5. ~~**1.10.0 Overlap 50 / 75 %**~~ -- done (the hop travels with the slices in `SliceInfo`;
   fixed the block order of the 75 % mode in `SpectrumAnalyzer`). (item 9): `SpectrumAnalyzer` supports 75 % already. Needed
   for 8192-point FFTs, otherwise the time axis jumps too much. Parameter, saved.
6. ~~**1.11.0 Time window 10 s and time zoom**~~ -- done (TimeStart/TimeEnd as fractions of the
   window, minimum 2 %). (item 11): internal memory 10 s, horizontal
   range slider above the display, time labels below follow the zoom (decision 8: relative
   time in Scroll mode, sweep time in Fix mode).
7. ~~**1.12.0 Keyboard overlay**~~ -- done: piano-roll bands (quarter tone below to above each note,
   the same boundaries as the readout), lines at E/F and B/C, names at C and at every note when
   zoomed; thin bands fade out on the linear axis. (item 8): switchable, small transparent overlay of the spectrogram with a musical keyboard (white and black stripes) and the note
   names at their frequencies; button icon: a small keyboard with black and white keys (max 8 keys).
8. ~~**1.13.0 BPM grid**~~ -- done: beat position of each slice end from the host (SliceInfo),
   bar/beat/subdivision lines (1, 1/2, 1/4, 1/8 beat; text instead of note symbols, which many
   fonts lack), finer levels left out below 5 px spacing, none at stops/jumps. (item 10): switched with a metronome icon button (drawn as a path);
   BPM from the host, displayed in the title bar; vertical
   lines at every beat, 1/2, 1/4 or 1/8 note, drawn on top of the spectrogram. Resolution
   box with note symbols, only visible while the grid is on; resolution is a parameter.
   The host's beat position (ppq) travels through the FIFO with each slice (like the slice
   size), so the lines sit on the beats also after tempo changes and transport jumps;
   BPM and position follow the host when the transport starts, stops or jumps.
9. ~~**1.14.0 Export PNG**~~ -- done: the visible spectrogram with frequency and time axis and the
   colour bar, twice the screen resolution, no crosshair; file dialog. (item 12, decision 6): the visible spectrogram with correct axes.
10. **2.0.0 Release**: manual (new screenshot, all new controls, release notes), marketing
    texts, pluginval (strictness 10, 5 runs), the test programs, CI build, tag v2.0.0.

## Open questions

- **Space in the title bar**: at 800 px the title image (500 px) and the logo leave about
  160 px; keys, BPM, resolution box, BPM value and Export need about 180 px. Options:
  a shorter title image for 2.0 (e.g. "JadeSpectrogram 2 - the musical spectrogram", with
  "by Hoertechnik und Audiologie" moved to the about box), or a base width of about 840 px.
  Answer: increase the base width to 840 px, keep the title image.
- **Colour sliders** (right side): also one range slider, for consistency with decision 7?
  Answer: one range slider, with the thumbs at the current min/max of the colour range. The
  colour range is saved with the project (decision 9).
- **Icon for the BPM grid**: e.g. a metronome.
  Answer: good idea, draw the icon yourself
- **Time labels in Fix mode** (fixed image, running cursor): seconds relative to the
  cursor, or absolute since the start of the display?
  Answer: if we use reletive seconds the x axis would move, wouldn't it?
  Proposal (Claude): yes, relative labels would have to move with the cursor. Therefore
  sweep time in Fix mode: 0 s at the left edge ... 10 s at the right, labels stay still,
  the cursor shows the current sweep position; Scroll mode keeps -10 s ... 0 s
  (see decision 8). Confirmed.
