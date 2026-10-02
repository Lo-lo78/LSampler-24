cmake_minimum_required(VERSION 3.22)

project(LSampler24 VERSION 0.3.0 LANGUAGES C CXX)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(LSampler24
    COMPANY_NAME "Lo-lo"
    BUNDLE_ID "org.lolo.lsampler24"
    PLUGIN_MANUFACTURER_CODE LoLo
    PLUGIN_CODE Ls24
    FORMATS VST3
    PRODUCT_NAME "LSampler-24"
    DESCRIPTION "Accessible 24-slot sampler"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(LSampler24 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ScreenReaderAnnouncer.cpp
    Source/ScreenReaderAnnouncer.h
    Source/SamplePool.cpp
    Source/SamplePool.h
    Source/Parameters.h
    Source/SlotAudioState.h
    Source/SlotAudioState.cpp
    Source/SamplerVoice.cpp
    Source/SamplerVoice.h
    Source/LibraryManager.cpp
    Source/LibraryManager.h
)

target_compile_features(LSampler24 PRIVATE cxx_std_20)
target_compile_definitions(LSampler24 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
)
target_link_libraries(LSampler24 PRIVATE
    juce::juce_audio_utils
    juce::juce_cryptography
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
if(WIN32)
    target_link_libraries(LSampler24 PRIVATE Uiautomationcore OleAut32)
endif()


option(LSAMPLER_BUILD_TESTS "Build native sampler regression tests" OFF)
if(LSAMPLER_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(LSampler24Tests PRODUCT_NAME "LSampler-24 Regression Tests")
    target_sources(LSampler24Tests PRIVATE Tests/SamplerTests.cpp)
    target_compile_features(LSampler24Tests PRIVATE cxx_std_20)
    target_link_libraries(LSampler24Tests PRIVATE LSampler24 juce::juce_audio_utils)
    add_test(NAME sampler-regression COMMAND LSampler24Tests)
endif()
