# Runtime comparison internal FFT (TGMStaticLib) vs. juce::dsp::FFT
# build and run (Release!):
#   cmake --build build --config Release --target fftBenchmark
#   ./build/tester/fftBenchmark/fftBenchmark_artefacts/Release/fftBenchmark
juce_add_console_app(fftBenchmark PRODUCT_NAME "fftBenchmark")

target_sources(fftBenchmark PRIVATE fftBenchmarkMain.cpp ../../TGMStaticLib/FFT.cpp)
target_include_directories(fftBenchmark PRIVATE ../../TGMStaticLib)

target_compile_definitions(fftBenchmark PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0)

# same optimisation flags as the plugin
target_link_libraries(fftBenchmark
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
