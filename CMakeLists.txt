cmake_minimum_required(VERSION 3.15)

project(TrackerAdjust VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE INTERNAL "")
endif()

set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON)
set(TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)

set(TRACKTION_ENGINE_DIR "$ENV{HOME}/Libraries/tracktion_engine" CACHE PATH "Path to tracktion_engine")
set(JUCE_DIR "$ENV{HOME}/Libraries/JUCE" CACHE PATH "Path to JUCE")

# Load JUCE first so tracktion_engine finds it already available
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce)

# Load tracktion_engine (skips its own JUCE since juce::juce_core already exists)
add_subdirectory(${TRACKTION_ENGINE_DIR} ${CMAKE_BINARY_DIR}/tracktion_engine)

juce_add_gui_app(TrackerAdjust
    BUNDLE_ID "com.trackeradjust.app"
    PRODUCT_NAME "Tracker Adjust"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio output")

juce_generate_juce_header(TrackerAdjust)

target_sources(TrackerAdjust PRIVATE
    src/Main.cpp
    src/data/PatternData.cpp
    src/audio/TrackerEngine.cpp
    src/audio/SimpleSampler.cpp
    src/audio/TrackerSamplerPlugin.cpp
    src/audio/InstrumentEffectsPlugin.cpp
    src/audio/MetronomePlugin.cpp
    src/ui/MainComponent.cpp
    src/ui/TrackerGrid.cpp
    src/ui/TrackerLookAndFeel.cpp
    src/ui/ToolbarComponent.cpp
    src/ui/ProjectSerializer.cpp
    src/ui/ArrangementComponent.cpp
    src/ui/InstrumentPanel.cpp
    src/ui/SampleEditorComponent.cpp
    src/ui/TabBarComponent.cpp
    src/ui/FileBrowserComponent.cpp)

target_include_directories(TrackerAdjust PRIVATE src src/data src/audio src/ui)

target_compile_features(TrackerAdjust PRIVATE cxx_std_20)

set_target_properties(TrackerAdjust PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

target_compile_definitions(TrackerAdjust PRIVATE
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_VST3=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(TrackerAdjust PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_warning_flags)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(TrackerAdjust PRIVATE "-fno-aligned-allocation")
endif()
