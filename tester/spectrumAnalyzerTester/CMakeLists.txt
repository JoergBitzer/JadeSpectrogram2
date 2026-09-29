cmake_minimum_required(VERSION 3.22)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 17)

project (specTestApp)

add_executable(specTestApp specTestMain.cpp ../../TGMStaticLib/FFT.cpp ../../JadeSpectrogram/SpectrumAnalyzer.cpp)
target_include_directories(specTestApp PRIVATE ../../TGMStaticLib)
