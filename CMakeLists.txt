cmake_minimum_required(VERSION 3.22)
project(MOONSHOT VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# ---- artwork (images, fonts, player frames) compiled into the plugin
file(GLOB MOONSHOT_ASSETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.png
                                           ${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.jpg
                                           ${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.ttf)
juce_add_binary_data(MoonshotAssets SOURCES ${MOONSHOT_ASSETS})
set_target_properties(MoonshotAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set(MOONSHOT_FORMATS VST3 AU)
else()
    set(MOONSHOT_FORMATS VST3)
endif()

juce_add_plugin(MOONSHOT
    COMPANY_NAME "buyorbealoser"
    PLUGIN_MANUFACTURER_CODE Bbal
    PLUGIN_CODE Mnsh
    FORMATS ${MOONSHOT_FORMATS}
    PRODUCT_NAME "MOONSHOT"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion)

target_sources(MOONSHOT PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/ClipperDSP.h)

target_compile_definitions(MOONSHOT PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MOONSHOT
    PRIVATE
        MoonshotAssets
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Linux dev boxes without the optional X11 extension headers: -DMOONSHOT_MINIMAL_X11=ON
option(MOONSHOT_MINIMAL_X11 "Disable optional X11 extensions (Linux dev only)" OFF)
set(MOONSHOT_X11_DEFS JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0 JUCE_USE_XRENDER=0 JUCE_USE_XINPUT=0)
if(UNIX AND NOT APPLE AND MOONSHOT_MINIMAL_X11)
    target_compile_definitions(MOONSHOT PUBLIC ${MOONSHOT_X11_DEFS})
endif()

# ---- optional tests : cmake -DMOONSHOT_BUILD_TESTS=ON
option(MOONSHOT_BUILD_TESTS "Build the DSP test and the offscreen editor render" OFF)
if(MOONSHOT_BUILD_TESTS)
    juce_add_console_app(MoonshotDspTest PRODUCT_NAME "MoonshotDspTest")
    target_sources(MoonshotDspTest PRIVATE tests/DspTest.cpp Source/ClipperDSP.h)
    target_include_directories(MoonshotDspTest PRIVATE Source)
    target_link_libraries(MoonshotDspTest PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)

    juce_add_console_app(MoonshotPluginTest PRODUCT_NAME "MoonshotPluginTest")
    target_sources(MoonshotPluginTest PRIVATE tests/PluginTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(MoonshotPluginTest PRIVATE Source)
    target_compile_definitions(MoonshotPluginTest PRIVATE JucePlugin_Name="MOONSHOT" JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    if(UNIX AND NOT APPLE AND MOONSHOT_MINIMAL_X11)
        target_compile_definitions(MoonshotPluginTest PRIVATE ${MOONSHOT_X11_DEFS})
    endif()
    target_link_libraries(MoonshotPluginTest PRIVATE MoonshotAssets juce::juce_audio_processors juce::juce_dsp juce::juce_recommended_config_flags)

    juce_add_console_app(MoonshotEditorRender PRODUCT_NAME "MoonshotEditorRender")
    target_sources(MoonshotEditorRender PRIVATE tests/EditorRender.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(MoonshotEditorRender PRIVATE Source)
    target_compile_definitions(MoonshotEditorRender PRIVATE JucePlugin_Name="MOONSHOT" JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    if(UNIX AND NOT APPLE AND MOONSHOT_MINIMAL_X11)
        target_compile_definitions(MoonshotEditorRender PRIVATE ${MOONSHOT_X11_DEFS})
    endif()
    target_link_libraries(MoonshotEditorRender PRIVATE MoonshotAssets juce::juce_audio_processors juce::juce_dsp juce::juce_recommended_config_flags)
endif()
