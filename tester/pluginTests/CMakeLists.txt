# Tests of the plugin code: each test is a small program that builds the real processor (and
# editor, offscreen) and checks one feature; it prints its checks and returns 0 only if all pass.
# Build with -DJADE_BUILD_TESTS=ON (top-level CMakeLists.txt), run with ctest. Linux/macOS.
# The tests link the plugin's shared code (it contains JUCE), so JUCE is not compiled again.

set(JADE_PLUGIN_TESTS
    slice_hash latency_test switch_test fifo_stress midi_alloc_test mixmode_test
    avg_test overlap_test state_test
    range_test timezoom_test scale_test reset_test nopaint_test aboutbox_test
    keys_test refpitch_test peak_test
    bpm_test tempo_test jump_test)

# test home folder: the tests must not touch the real presets and settings of the user
set(JADE_TEST_HOME "${CMAKE_CURRENT_BINARY_DIR}/home")
file(MAKE_DIRECTORY "${JADE_TEST_HOME}/Desktop")

function(jade_add_test_program name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE JadeSpectrogram)
    # the include directories and definitions of the plugin code (JUCE modules, JucePlugin_...)
    target_include_directories(${name} PRIVATE
        $<TARGET_PROPERTY:JadeSpectrogram,INCLUDE_DIRECTORIES>
        "${CMAKE_SOURCE_DIR}/JadeSpectrogram"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(${name} PRIVATE $<TARGET_PROPERTY:JadeSpectrogram,COMPILE_DEFINITIONS>)
    # test code compares floats exactly on purpose (bit-identical checks) and reaches private
    # members: no warnings from the strict flags it inherits from the plugin target
    target_compile_options(${name} PRIVATE -w)
    # the plugin code is compiled with link-time optimisation: link in parallel (gcc)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(${name} PRIVATE -flto=auto)
    endif()
    add_dependencies(plugin_tests ${name})
endfunction()

# one target for all test programs: cmake --build <build> --target plugin_tests
add_custom_target(plugin_tests)

foreach(test ${JADE_PLUGIN_TESTS})
    jade_add_test_program(${test})
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES ENVIRONMENT "HOME=${JADE_TEST_HOME}" TIMEOUT 900)
endforeach()
# the Cubase test replays a playhead log recorded in Cubase (loop 0 ... 8 ppq, 512-sample blocks)
jade_add_test_program(cubase_replay)
add_test(NAME cubase_replay COMMAND cubase_replay "${CMAKE_CURRENT_SOURCE_DIR}/data/cubase_loop_0-8_512.txt")
set_tests_properties(cubase_replay PROPERTIES ENVIRONMENT "HOME=${JADE_TEST_HOME}" TIMEOUT 900)

# golden master for refactoring (not a ctest): records analysis hashes, GUI pictures, readouts and
# the saved state into a folder; record before and after a change and compare the folders
jade_add_test_program(golden)
