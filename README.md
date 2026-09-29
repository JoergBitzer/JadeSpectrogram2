# JadeSpectrogram2
New version of the Jade spectrogram 

## Download

Ready-to-use builds for Windows, macOS (Universal) and Linux (VST3 and Standalone, macOS also AU)
are on the [Releases page](https://github.com/JoergBitzer/JadeSpectrogram2/releases).
They are built automatically by GitHub Actions (`.github/workflows/release.yml`) when a version
tag `vX.Y.Z` is pushed. The macOS binaries are not signed with an Apple Developer ID yet: if macOS
refuses to open them, run `xattr -cr <path to the plugin or app>` in the Terminal.
Each zip contains the manual ([docs/ManualJadeSpectrogram2.pdf](docs/ManualJadeSpectrogram2.pdf)),
a ReadMeFirst.txt with the installation steps and the license files.

## What is new in 2.0: the musical spectrogram

1. Keyboard overlay: semitone bands and note names over the spectrogram
2. BPM grid: bars, beats and subdivisions (up to 1/16 beat) from the host tempo, exact also
   after tempo changes, jumps and loops
3. Logarithmic frequency axis (switchable), exact linear axis with max per pixel
4. Crosshair with frequency, note and level at the mouse position
5. Time axis in seconds, 10 s memory, time zoom
6. Range sliders for the frequency, colour and time range
7. Averaging along time, overlap 50 % / 75 %
8. PNG export of the visible spectrogram with its axes
9. All settings are stored with the project
10. Zero latency: a pure analyzer, the audio passes through unchanged; changing FFT size,
    window or overlap causes no dropouts

The full release notes are in the appendix of the [manual](docs/ManualJadeSpectrogram2.pdf).

### Code basis and build process
1. The new version is self contained. Everything needed to build is in this repository. Clone with --recursive to get the JUCE submodule
2. No dependencies from Eigen anymore (uses the internal FFT from TGMStaticLib, a fast real-valued FFT by Uwe Simmer)
3. The memory exchange between processor and GUI is lock-free (single-producer/single-consumer FIFO)

## Build from source

The plugin uses [JUCE 9](https://juce.com) (tested with 9.0.3, branch `master`) and CMake.
JUCE and [TGMStaticLib](https://github.com/JoergBitzer/TGMStaticLib) are git submodules,
so everything needed is in this repository:

```console
git clone --recursive https://github.com/JoergBitzer/JadeSpectrogram2.git
cd JadeSpectrogram2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target JadeSpectrogram_VST3 JadeSpectrogram_Standalone
# macOS additionally: --target JadeSpectrogram_AU
```

The results are in `build/JadeSpectrogram/JadeSpectrogram_artefacts/Release/`. On Linux,
install the JUCE dependencies first (see `JUCE/docs/Linux Dependencies.md`; the list used
for the release builds is in `.github/workflows/release.yml`).

To update JUCE: `cd JUCE && git fetch --tags && git checkout <version>`, then commit the
new submodule state.

## License

- **Source code of this repository: [MIT License](LICENSE)**, (c) Joerg Bitzer, Jade Hochschule.
- **Plugin binaries:** they contain third-party code:
  - [JUCE 9](https://github.com/juce-framework/JUCE), used under the
    [AGPLv3](https://www.gnu.org/licenses/agpl-3.0.html) (JUCE is dual-licensed
    AGPLv3 / commercial JUCE licence). Therefore the binaries as a whole are
    distributed under the **AGPLv3** (full text: [LICENSE-AGPL-3.0.txt](LICENSE-AGPL-3.0.txt));
    the complete source code is this repository plus JUCE.
  - the VST3 SDK 3.8 by Steinberg (bundled with JUCE 9; MIT License) -- VST is a
    registered trademark of Steinberg Media Technologies GmbH;
  - the Audio Unit SDK by Apple (Apache License 2.0, macOS AU only);
  - the FFT by Uwe Simmer in [TGMStaticLib](https://github.com/JoergBitzer/TGMStaticLib)
    (MIT-style license) and the colormap data of matplotlib (viridis, plasma, inferno; CC0).

MIT code may be combined with AGPLv3 code; the MIT license of the files in this repository
stays as it is, and anyone can reuse them under MIT (for example in a project with a
commercial JUCE licence). The manual is licensed under CC-BY 4.0.

The plugin comes without any warranty (see the licenses).
