# Jade Spectrogram

This is the source code for the Jade Spectrogram. You can download binaries for Win/Mac/Linus at our KVR page. The source code provided here should be compile on all plattforms. You need CMAKE and a compiler (Visual Studio, XCode, gcc) for the build process.


## version history
1.0 old version with high dependency on the TGM develpoment framework
1.1 new version with 
* the same functionality, but can compile directly and better code basis (Class Analyzer and exchange Algo/GUI is based on non-blocking FIFO)
* new display of the power at a given mouse position in the spectrogram
* more exact scaling 
* bug fix: frequency slider cannot overlap anymore


## How to build

1. clone this project with clone  --recursive
2. build the project

## To do
* still to many warnings mostly float to int and int to size_t and vice versa
* new features

