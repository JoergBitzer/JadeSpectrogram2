# JadeSpectrogram2
New version of the Jade spectrogram 

## What is new

### Code basis and build process
1. The new version is self contained. Everything needed to build is in this repository. Clone with --recursive to get the JUCE submodule
2. No dependencies from Eigen anymore (uses the internal FFT from TGMStaticLib, a fast real-valued FFT by Uwe Simmer)
3. The memory exchange is now block free, by using block-free FiFOs between processor and GUI
   

### new features
1. Added the Level for the mouse readout 
2. The default colormap is now plasma




