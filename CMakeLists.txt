cmake_minimum_required(VERSION 3.15)

project(TrackerAdjust VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE INTERNAL "")
endif()

set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON)
set(TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(modules/tracktion_engine)

juce_add_gui_app(TrackerAdjust
    BUNDLE_ID "com.trackeradjust.app"
    PRODUCT_NAME "Tracker Adjust"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio output")

juce_generate_juce_header(TrackerAdjust)

target_sources(TrackerAdjust PRIVATE
    src/Main.cpp
    src/MainComponent.cpp
    src/PatternData.cpp
    src/TrackerGrid.cpp
    src/TrackerEngine.cpp
    src/SimpleSampler.cpp
    src/TrackerLookAndFeel.cpp
    src/ToolbarComponent.cpp
    src/ProjectSerializer.cpp
    src/ArrangementComponent.cpp
    src/InstrumentPanel.cpp
    src/SampleEditorComponent.cpp)

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
    juce::juce_recommended_warning_flags)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(TrackerAdjust PRIVATE "-fno-aligned-allocation")
endif()
