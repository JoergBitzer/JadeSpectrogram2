# Tests and test tools

| Folder | Content |
|---|---|
| `pluginTests/` | tests of the plugin code (processor and editor), run with `ctest` |
| `fftBenchmark/` | speed of the internal FFT (TGMStaticLib) against the JUCE FFT |
| `spectrumAnalyzerTester/` | stand-alone check of `SpectrumAnalyzer` |

## Plugin tests (`pluginTests/`)

Each test is a small program that builds the real processor and, where needed, the editor
(offscreen), checks one feature, prints every check with OK/FAIL and returns 0 only if all pass.
They reach private members of the GUI with `#define private public` (test-only).
The tests run with their own home folder (`build/tester/pluginTests/home`), so they never touch
your presets and settings. Linux and macOS.

```console
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJADE_BUILD_TESTS=ON
cmake --build build --target plugin_tests -j8
cd build && ctest --output-on-failure
```

| Test | What it checks |
|---|---|
| `slice_hash` | fingerprint of the analysis (all spectra for a fixed signal with FFT size, window and mix mode changes) against the baseline; a refactor must not change it |
| `latency_test` | impulse through `processBlock`: zero latency for every FFT size and host block size |
| `switch_test` | FFT size and window changes while the audio runs: no allocation, only valid spectrum sizes reach the GUI |
| `fifo_stress` | lock-free FIFO between audio and GUI: no torn or reordered slices (two threads) |
| `midi_alloc_test` | `SynchronBlockProcessor` with MIDI: no allocation in `processBlock` |
| `mixmode_test` | channel mix modes TimeMean and AbsMean agree for L = R |
| `avg_test` | averaging: step response at the time constant, off = bit-identical, no allocation |
| `overlap_test` | 50 % / 75 % overlap: hop, level, columns, no allocation, saved setting |
| `state_test` | all settings saved with the project and restored (incl. GUI size) |
| `range_test` | range sliders: thumbs, range drag (ratio on the log axis), clamping, host gestures, external changes |
| `timezoom_test` | time zoom: visible part, time axis labels, scroll and Fix mode |
| `scale_test` | GUI scaling: layout and slider positions at several window sizes |
| `reset_test` | overview button: full frequency, time and colour range, gestures, positions |
| `nopaint_test` | scrolling image stays exact when the timer runs without paint in between (bug before 2.4.6) |
| `aboutbox_test` | the editor opens without the about box (bug before 2.4.5) |
| `keys_test` | keyboard overlay: band edges, readout note = band note, fading of thin bands |
| `refpitch_test` | reference pitch A4: note and cents, band edges in pixels, drag box, state |
| `peak_test` | peak line: interpolation accuracy with sines, no peak on flanks or noise, dot, harmonic cursor lines |
| `bpm_test` | BPM grid synced: beat position of each slice, lines at the beats, tempo change, resolutions, 3/4, stop, jumps, no tempo, state |
| `tempo_test` | free tempo: switch from synced, drag box, Alt+click bar line, paused/stopped, host tempo while paused, state |
| `jump_test` | synthetic host sequences: start of play, locate, loops on and off the beat, tempo changes, odd block sizes while stopped |
| `cubase_replay` | replays a playhead log recorded in Cubase (`data/`, loop 0 ... 8 ppq, the host jumps inside an audio block): every bar line, also at each loop start |

## Golden master for refactoring (`golden`)

Not a ctest, a tool: it records hashes of the analysis output, pictures of the GUI in several
configurations (incl. the PNG export), many readout texts and the saved state into a folder.
Record before and after a change that should not change anything, and compare:

```console
build/tester/pluginTests/golden /tmp/before    # old code
build/tester/pluginTests/golden /tmp/after     # new code
diff -r /tmp/before /tmp/after && echo identical
```

Run it with a temporary home folder (`HOME=$(mktemp -d) ...`). The version number appears in the
pictures, so compare before raising the version.
