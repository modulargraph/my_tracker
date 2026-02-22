cmake_minimum_required(VERSION 3.15)

project(TrackerAdjust VERSION 0.1.0)

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

target_include_directories(TrackerAdjust PRIVATE src src/data src/audio src/ui)

target_compile_features(TrackerAdjust PRIVATE cxx_std_20)

set_target_properties(TrackerAdjust PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

target_compile_definitions(TrackerAdjust PRIVATE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
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

add_executable(TrackerAdjustTests
    tests/TrackerAdjustTests.cpp
    src/data/PatternData.cpp
    src/ui/ProjectSerializer.cpp
    src/ui/ArrangementComponent.cpp
    src/ui/TrackerLookAndFeel.cpp
    src/ui/TrackerGrid.cpp
    src/ui/PluginAutomationComponent.cpp)

target_include_directories(TrackerAdjustTests PRIVATE
    src src/data src/audio src/ui
    ${CMAKE_BINARY_DIR}/TrackerAdjust_artefacts/JuceLibraryCode)

target_compile_features(TrackerAdjustTests PRIVATE cxx_std_20)

target_compile_definitions(TrackerAdjustTests PRIVATE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(TrackerAdjustTests PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_warning_flags)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_compile_options(TrackerAdjustTests PRIVATE "-fno-aligned-allocation")
endif()

add_test(NAME TrackerAdjustTests COMMAND TrackerAdjustTests)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # --- Out-of-process plugin validator (tiny C binary) ---
    add_executable(PluginValidator src/audio/PluginValidator.c)
    add_dependencies(TrackerAdjust PluginValidator)

    # Copy PluginValidator into app bundle (before codesigning)
    add_custom_command(TARGET TrackerAdjust POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:PluginValidator>"
            "$<TARGET_BUNDLE_DIR:TrackerAdjust>/Contents/MacOS/PluginValidator"
        COMMENT "Copying PluginValidator into app bundle"
        VERBATIM)

    option(TRACKER_CREATE_APP_SHORTCUT "Create fixed app symlink at run/Tracker Adjust.app" ON)
    option(TRACKER_ADHOC_CODESIGN "Ad-hoc sign app and test binaries after build on macOS" ON)

    if(TRACKER_CREATE_APP_SHORTCUT)
        set(TRACKER_APP_SHORTCUT_DIR "${CMAKE_SOURCE_DIR}/run")
        set(TRACKER_APP_SHORTCUT_PATH "${TRACKER_APP_SHORTCUT_DIR}/Tracker Adjust.app")

        add_custom_command(TARGET TrackerAdjust POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${TRACKER_APP_SHORTCUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E rm -f "${TRACKER_APP_SHORTCUT_PATH}"
            COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_BUNDLE_DIR:TrackerAdjust>" "${TRACKER_APP_SHORTCUT_PATH}"
            VERBATIM)
    endif()

    if(TRACKER_ADHOC_CODESIGN)
        find_program(CODESIGN_COMMAND codesign)
        find_program(XATTR_COMMAND xattr)

        if(CODESIGN_COMMAND)
            if(XATTR_COMMAND)
                add_custom_command(TARGET TrackerAdjust POST_BUILD
                    COMMAND ${XATTR_COMMAND} -cr "$<TARGET_BUNDLE_DIR:TrackerAdjust>"
                    COMMAND ${CODESIGN_COMMAND} --force --deep --sign - "$<TARGET_BUNDLE_DIR:TrackerAdjust>"
                    VERBATIM)
            else()
                add_custom_command(TARGET TrackerAdjust POST_BUILD
                    COMMAND ${CODESIGN_COMMAND} --force --deep --sign - "$<TARGET_BUNDLE_DIR:TrackerAdjust>"
                    VERBATIM)
            endif()

            add_custom_command(TARGET TrackerAdjustTests POST_BUILD
                COMMAND ${CODESIGN_COMMAND} --force --sign - "$<TARGET_FILE:TrackerAdjustTests>"
                VERBATIM)
        else()
            message(WARNING "codesign not found; TrackerAdjust targets will not be signed")
        endif()
    endif()
endif()
