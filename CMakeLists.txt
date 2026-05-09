cmake_minimum_required(VERSION 3.15)

project(VCTracker VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE INTERNAL "")
endif()

set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON)
set(TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)

set(TRACKTION_ENGINE_DIR "$ENV{HOME}/Libraries/tracktion_engine" CACHE PATH "Path to tracktion_engine")
set(JUCE_DIR "$ENV{HOME}/Libraries/JUCE" CACHE PATH "Path to JUCE")

set(TRACKER_APP_COMPILE_DEFINITIONS
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_MODULE_AVAILABLE_juce_audio_basics=1
    JUCE_MODULE_AVAILABLE_juce_audio_devices=1
    JUCE_MODULE_AVAILABLE_juce_audio_formats=1
    JUCE_MODULE_AVAILABLE_juce_audio_processors=1
    JUCE_MODULE_AVAILABLE_juce_audio_processors_headless=1
    JUCE_MODULE_AVAILABLE_juce_audio_utils=1
    JUCE_MODULE_AVAILABLE_juce_core=1
    JUCE_MODULE_AVAILABLE_juce_data_structures=1
    JUCE_MODULE_AVAILABLE_juce_dsp=1
    JUCE_MODULE_AVAILABLE_juce_events=1
    JUCE_MODULE_AVAILABLE_juce_graphics=1
    JUCE_MODULE_AVAILABLE_juce_gui_basics=1
    JUCE_MODULE_AVAILABLE_juce_gui_extra=1
    JUCE_MODULE_AVAILABLE_juce_osc=1
    JUCE_MODULE_AVAILABLE_tracktion_core=1
    JUCE_MODULE_AVAILABLE_tracktion_engine=1
    JUCE_MODULE_AVAILABLE_tracktion_graph=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0)

set(TRACKER_SOURCE_DIRS
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/data
    ${CMAKE_SOURCE_DIR}/src/audio
    ${CMAKE_SOURCE_DIR}/src/ui)

set(TRACKER_MODULE_INCLUDE_DIRS
    ${CMAKE_BINARY_DIR}/VCTracker_artefacts/JuceLibraryCode
    ${TRACKTION_ENGINE_DIR}/modules
    ${JUCE_DIR}/modules
    ${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK
    ${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/LV2_SDK
    ${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/LV2_SDK/lv2
    ${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/LV2_SDK/serd
    ${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/LV2_SDK/sord
    ${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/LV2_SDK/sord/src
    ${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/LV2_SDK/sratom
    ${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/LV2_SDK/lilv
    ${JUCE_DIR}/modules/juce_audio_processors_headless/format_types/LV2_SDK/lilv/src)

set(TRACKER_CORE_SOURCES
    src/data/Pattern.cpp
    src/data/PatternData.cpp
    src/audio/TrackerEngine.cpp
    src/audio/SimpleSampler.cpp
    src/audio/TrackerSamplerPlugin.cpp
    src/audio/InstrumentEffectsPlugin.cpp
    src/audio/MetronomePlugin.cpp
    src/audio/SendEffectsPlugin.cpp
    src/audio/MixerPlugin.cpp
    src/audio/ChannelStripPlugin.cpp
    src/audio/TrackOutputPlugin.cpp
    src/ui/MainComponent.cpp
    src/ui/TrackerGrid.cpp
    src/ui/TrackerLookAndFeel.cpp
    src/ui/ToolbarComponent.cpp
    src/ui/ProjectSerializer.cpp
    src/ui/ArrangementComponent.cpp
    src/ui/InstrumentPanel.cpp
    src/ui/SampleEditorComponent.cpp
    src/ui/WaveformView.cpp
    src/ui/TabBarComponent.cpp
    src/ui/FileBrowserComponent.cpp
    src/ui/MixerComponent.cpp
    src/ui/MixerHitTest.cpp
    src/ui/MixerParamModel.cpp
    src/ui/MixerStripPainter.cpp
    src/ui/SendEffectsComponent.cpp
    src/audio/PluginCatalogService.cpp
    src/ui/AudioPluginSettingsComponent.cpp
    src/ui/PluginAutomationComponent.cpp)

# Load JUCE first so tracktion_engine finds it already available
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce)

# Load tracktion_engine (skips its own JUCE since juce::juce_core already exists)
add_subdirectory(${TRACKTION_ENGINE_DIR} ${CMAKE_BINARY_DIR}/tracktion_engine)

juce_add_gui_app(VCTracker
    BUNDLE_ID "com.vctracker.app"
    PRODUCT_NAME "VCTracker"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio output")

juce_generate_juce_header(VCTracker)

add_library(VCTrackerCore OBJECT ${TRACKER_CORE_SOURCES})

target_include_directories(VCTrackerCore PRIVATE
    ${TRACKER_SOURCE_DIRS}
    ${TRACKER_MODULE_INCLUDE_DIRS})

target_compile_features(VCTrackerCore PRIVATE cxx_std_20)

target_compile_definitions(VCTrackerCore PRIVATE ${TRACKER_APP_COMPILE_DEFINITIONS})
target_compile_definitions(VCTrackerCore PRIVATE
    $<$<CONFIG:Debug>:DEBUG=1>
    $<$<CONFIG:Debug>:_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>)

target_link_libraries(VCTrackerCore PRIVATE juce::juce_recommended_warning_flags)

target_precompile_headers(VCTrackerCore PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/src/Precompiled.h>")

set_target_properties(VCTrackerCore PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(VCTrackerCore PRIVATE "-fno-aligned-allocation")
endif()

target_sources(VCTracker PRIVATE
    src/Main.cpp
    $<TARGET_OBJECTS:VCTrackerCore>)

target_include_directories(VCTracker PRIVATE ${TRACKER_SOURCE_DIRS})

target_compile_features(VCTracker PRIVATE cxx_std_20)

set_target_properties(VCTracker PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

target_compile_definitions(VCTracker PRIVATE ${TRACKER_APP_COMPILE_DEFINITIONS})

target_link_libraries(VCTracker PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_warning_flags)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(VCTracker PRIVATE "-fno-aligned-allocation")
endif()

add_executable(PluginScanWorker
    src/audio/PluginScanWorker.cpp)

target_compile_features(PluginScanWorker PRIVATE cxx_std_20)

target_compile_definitions(PluginScanWorker PRIVATE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(PluginScanWorker PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_warning_flags)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(PluginScanWorker PRIVATE "-fno-aligned-allocation")
endif()

add_executable(PluginEditorSmoke
    src/audio/PluginEditorSmoke.cpp)

target_compile_features(PluginEditorSmoke PRIVATE cxx_std_20)

target_compile_definitions(PluginEditorSmoke PRIVATE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(PluginEditorSmoke PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_warning_flags)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(PluginEditorSmoke PRIVATE "-fno-aligned-allocation")
endif()

add_executable(VCTrackerTests
    tests/VCTrackerTests.cpp
    src/data/Pattern.cpp
    src/data/PatternData.cpp
    src/audio/PatternMidiBuilder.cpp
    src/audio/ChannelStripPlugin.cpp
    src/audio/TrackOutputPlugin.cpp
    src/ui/ProjectSerializer.cpp
    src/ui/ArrangementComponent.cpp
    src/ui/TrackerLookAndFeel.cpp
    src/ui/TrackerGrid.cpp
    src/ui/PluginAutomationComponent.cpp)

target_include_directories(VCTrackerTests PRIVATE
    src src/data src/audio src/ui
    ${CMAKE_BINARY_DIR}/VCTracker_artefacts/JuceLibraryCode)

target_compile_features(VCTrackerTests PRIVATE cxx_std_20)

target_compile_definitions(VCTrackerTests PRIVATE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(VCTrackerTests PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_warning_flags)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(VCTrackerTests PRIVATE "-fno-aligned-allocation")
endif()

add_test(NAME VCTrackerTests COMMAND VCTrackerTests)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # --- Out-of-process plugin validator (tiny C binary) ---
    add_executable(PluginValidator src/audio/PluginValidator.c)
    add_dependencies(VCTracker PluginScanWorker PluginEditorSmoke PluginValidator)

    # Copy scanner helpers into app bundle (before codesigning)
    add_custom_command(TARGET VCTracker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:PluginScanWorker>"
            "$<TARGET_BUNDLE_DIR:VCTracker>/Contents/MacOS/PluginScanWorker"
        COMMENT "Copying PluginScanWorker into app bundle"
        VERBATIM)

    add_custom_command(TARGET VCTracker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:PluginValidator>"
            "$<TARGET_BUNDLE_DIR:VCTracker>/Contents/MacOS/PluginValidator"
        COMMENT "Copying PluginValidator into app bundle"
        VERBATIM)

    add_custom_command(TARGET VCTracker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:PluginEditorSmoke>"
            "$<TARGET_BUNDLE_DIR:VCTracker>/Contents/MacOS/PluginEditorSmoke"
        COMMENT "Copying PluginEditorSmoke into app bundle"
        VERBATIM)

    option(TRACKER_CREATE_APP_SHORTCUT "Create fixed app symlink at run/VCTracker.app" ON)
    option(TRACKER_ADHOC_CODESIGN "Ad-hoc sign app and test binaries after build on macOS" ON)

    if(TRACKER_CREATE_APP_SHORTCUT)
        set(TRACKER_APP_SHORTCUT_DIR "${CMAKE_SOURCE_DIR}/run")
        set(TRACKER_APP_SHORTCUT_PATH "${TRACKER_APP_SHORTCUT_DIR}/VCTracker.app")

        add_custom_command(TARGET VCTracker POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${TRACKER_APP_SHORTCUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E rm -f "${TRACKER_APP_SHORTCUT_PATH}"
            COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_BUNDLE_DIR:VCTracker>" "${TRACKER_APP_SHORTCUT_PATH}"
            VERBATIM)
    endif()

    if(TRACKER_ADHOC_CODESIGN)
        find_program(CODESIGN_COMMAND codesign)
        find_program(XATTR_COMMAND xattr)

        if(CODESIGN_COMMAND)
            if(XATTR_COMMAND)
                add_custom_command(TARGET VCTracker POST_BUILD
                    COMMAND ${XATTR_COMMAND} -cr "$<TARGET_BUNDLE_DIR:VCTracker>"
                    COMMAND ${CODESIGN_COMMAND} --force --deep --sign - "$<TARGET_BUNDLE_DIR:VCTracker>"
                    VERBATIM)
            else()
                add_custom_command(TARGET VCTracker POST_BUILD
                    COMMAND ${CODESIGN_COMMAND} --force --deep --sign - "$<TARGET_BUNDLE_DIR:VCTracker>"
                    VERBATIM)
            endif()

            add_custom_command(TARGET VCTrackerTests POST_BUILD
                COMMAND ${CODESIGN_COMMAND} --force --sign - "$<TARGET_FILE:VCTrackerTests>"
                VERBATIM)

            add_custom_command(TARGET PluginScanWorker POST_BUILD
                COMMAND ${CODESIGN_COMMAND} --force --sign - "$<TARGET_FILE:PluginScanWorker>"
                VERBATIM)

            add_custom_command(TARGET PluginEditorSmoke POST_BUILD
                COMMAND ${CODESIGN_COMMAND} --force --sign - "$<TARGET_FILE:PluginEditorSmoke>"
                VERBATIM)
        else()
            message(WARNING "codesign not found; VCTracker targets will not be signed")
        endif()
    endif()
endif()
