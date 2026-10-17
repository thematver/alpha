cmake_minimum_required(VERSION 3.22)
project(alpha VERSION 0.2.0 LANGUAGES C CXX OBJC OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS")
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(Alpha
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Alpha Audio"
    COMPANY_WEBSITE ""
    BUNDLE_ID "audio.alpha.character"
    PLUGIN_MANUFACTURER_CODE Alph
    PLUGIN_CODE Alfa
    FORMATS AU Standalone
    PRODUCT_NAME "alpha"
    DESCRIPTION "A little character. A different world."
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    AU_SANDBOX_SAFE TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "alpha can process your audio input in standalone mode."
)
juce_add_binary_data(PatinaFonts SOURCES
    Assets/Fonts/Archivo-Regular.ttf Assets/Fonts/Archivo-Bold.ttf
    Assets/Fonts/GeistMono-Regular.ttf Assets/Fonts/GeistMono-Bold.ttf)
target_sources(Alpha PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/PatinaViews.cpp Source/AlphaDSP.cpp Source/AuthenticCoreDSP.cpp Source/TimePitchDSP.cpp Source/ShaperDSP.cpp)
target_include_directories(Alpha PUBLIC Source)
target_compile_definitions(Alpha PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0 JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(Alpha PRIVATE PatinaFonts juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

add_executable(alpha-dsp-tests Source/AlphaDSP.cpp Source/AuthenticCoreDSP.cpp Source/TimePitchDSP.cpp Source/ShaperDSP.cpp Tests/DSPTests.cpp)
target_include_directories(alpha-dsp-tests PRIVATE Source)
enable_testing()
add_test(NAME DSP COMMAND alpha-dsp-tests)

juce_add_console_app(AlphaProcessorTests PRODUCT_NAME "alpha processor tests")
target_sources(AlphaProcessorTests PRIVATE Tests/ProcessorTests.cpp)
target_link_libraries(AlphaProcessorTests PRIVATE Alpha juce::juce_audio_utils juce::juce_dsp)
target_compile_definitions(AlphaProcessorTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
add_test(NAME Processor COMMAND AlphaProcessorTests)

juce_add_console_app(AlphaTransportTests PRODUCT_NAME "alpha transport tests")
target_sources(AlphaTransportTests PRIVATE Tests/TransportTests.cpp)
target_link_libraries(AlphaTransportTests PRIVATE Alpha juce::juce_audio_utils juce::juce_dsp)
target_compile_definitions(AlphaTransportTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
add_test(NAME Transport COMMAND AlphaTransportTests)

add_executable(alpha-pitch-time-tests Source/TimePitchDSP.cpp Tests/TimePitchTests.cpp)
target_include_directories(alpha-pitch-time-tests PRIVATE Source)
add_test(NAME PitchTime COMMAND alpha-pitch-time-tests)
add_executable(alpha-shaper-tests Source/ShaperDSP.cpp Tests/ShaperTests.cpp)
target_include_directories(alpha-shaper-tests PRIVATE Source)
add_test(NAME Shapers COMMAND alpha-shaper-tests)

juce_add_console_app(AlphaAudioReview PRODUCT_NAME "alpha audio review")
target_sources(AlphaAudioReview PRIVATE Tests/AudioReview.cpp)
target_link_libraries(AlphaAudioReview PRIVATE Alpha juce::juce_audio_utils)
target_compile_definitions(AlphaAudioReview PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)

if(APPLE)
    add_executable(alpha-au-host-tests Tests/AUHostTests.mm)
    target_link_libraries(alpha-au-host-tests PRIVATE "-framework Foundation" "-framework AudioToolbox")
    add_dependencies(alpha-au-host-tests Alpha_AU)
    add_test(NAME AudioUnitHost COMMAND alpha-au-host-tests "$<TARGET_BUNDLE_DIR:Alpha_AU>")
endif()
