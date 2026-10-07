# Example Audio Plugin CMakeLists.txt

# To get started on a new plugin, copy this entire folder (containing this file and C++ sources) to
# a convenient location, and then start making modifications.

# The first line of any CMake project should be a call to `cmake_minimum_required`, which checks
# that the installed CMake will be able to understand the following CMakeLists, and ensures that
# CMake's behaviour is compatible with the named version. This is a standard CMake command, so more
# information can be found in the CMake docs.

cmake_minimum_required(VERSION 3.24)

# The top-level CMakeLists.txt file for a project must contain a literal, direct call to the
# `project()` command. `project()` sets up some helpful variables that describe source/binary
# directories, and the current project version. This is a standard CMake command.

project(REPOSTART VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 17) # or 20

# if you have binaries or a static lib in your projects
# set(CMAKE_POSITION_INDEPENDENT_CODE ON)


#if build fail, uncomment these lines 
# set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
# set_property(GLOBAL PROPERTY USE_FOLDERS YES)
# option(JUCE_ENABLE_MODULE_SOURCE_GROUPS "Enable Module Source Groups" ON)



# If you've installed JUCE somehow (via a package manager, or directly using the CMake install
# target), you'll need to tell this project that it depends on the installed copy of JUCE. If you've
# included JUCE directly in your source tree (perhaps as a submodule), you'll need to tell CMake to
# include that subdirectory as part of the build.

# find_package(JUCE CONFIG REQUIRED)        # If you've installed JUCE to your system
# or

#add_compile_definitions(FACTORY_PRESETS)
add_compile_definitions(USE_JUCE) # internal define (from JB) for some tools

add_subdirectory(JUCE)                    # If you've put JUCE in a subdirectory called JUCE

# add these two libraries later (if needed)
# this is for the eigen library (https://gitlab.com/libeigen/eigen)
# include_directories(Libs/eigen;Libs/eigen/unsupported)

# and the TGM Tools: the plugin only needs the FFT (TGMStaticLib/FFT.cpp, no JUCE dependency),
# which JadeSpectrogram/CMakeLists.txt compiles directly. The library itself is not built, since
# it links juce_gui_basics and would compile a second copy of the JUCE modules.


# if you have access zo the vst2.4 sdk (and the license from Steinberg)
# set the path for JUCE
# juce_set_vst2_sdk_path(Libs/vstsdk2.4)
# and add in juce_add_plugin( FORMATS VST ) there should be VST3 AU and Standalone

# add tester
add_subdirectory(tester/spectrumAnalyzerTester)
add_subdirectory(tester/fftBenchmark)

# Add your projects

add_subdirectory(JadeSpectrogram)

# tests of the plugin code (tester/pluginTests): cmake -DJADE_BUILD_TESTS=ON ..., then ctest
option(JADE_BUILD_TESTS "Build the plugin tests in tester/pluginTests" OFF)
if(JADE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tester/pluginTests)
endif()                  
