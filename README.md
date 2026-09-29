# JadeSpectrogram2
New version of the Jade spectrogram 

## Download

Ready-to-use builds for Windows, macOS (Universal) and Linux (VST3 and Standalone, macOS also AU)
are on the [Releases page](https://github.com/JoergBitzer/JadeSpectrogram2/releases).
They are built automatically by GitHub Actions (`.github/workflows/release.yml`) when a version
tag `vX.Y.Z` is pushed. The macOS binaries are not signed with an Apple Developer ID yet: if macOS
refuses to open them, run `xattr -cr <path to the plugin or app>` in the Terminal.

## What is new

### Code basis and build process
1. The new version is self contained. Everything needed to build is in this repository. Clone with --recursive to get the JUCE submodule
2. No dependencies from Eigen anymore (uses the internal FFT from TGMStaticLib, a fast real-valued FFT by Uwe Simmer)
3. The memory exchange is now block free, by using block-free FiFOs between processor and GUI
   

### new features
1. Added the Level for the mouse readout 
2. The default colormap is now plasma




