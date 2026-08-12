cmake_minimum_required(VERSION 3.24)

project(Vesper
    VERSION 1.0.0
    DESCRIPTION "VESPER - a creative sound design environment by Sable Audio"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------ JUCE ----
include(FetchContent)
# 7.0.12: last JUCE line that builds cleanly under MinGW-w64 (JUCE 8's
# Direct2D renderer requires MSVC). CI release builds can use MSVC + 8.x.
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ----------------------------------------------------------- factory data ---
file(GLOB VESPER_FACTORY_PRESETS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/presets/factory/*.vpreset")

juce_add_binary_data(VesperData
    HEADER_NAME BinaryData.h
    NAMESPACE   BinaryData
    SOURCES     ${VESPER_FACTORY_PRESETS}
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/vesper-logo.svg")

# ---------------------------------------------------------------- plugin ----
# AAX requires the Avid SDK + PACE signing tools (NDA); enable by adding AAX
# to FORMATS and setting JUCE_AAX_SDK_PATH once you are an Avid developer.
juce_add_plugin(Vesper
    COMPANY_NAME                "Sable Audio"
    COMPANY_WEBSITE             "https://sableaudio.example"
    COMPANY_EMAIL               "support@sableaudio.example"
    BUNDLE_ID                   com.sableaudio.vesper
    PLUGIN_MANUFACTURER_CODE    Sabl
    PLUGIN_CODE                 Vspr
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Vesper"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Distortion Dynamics EQ Reverb Delay Modulation
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(Vesper PRIVATE
    src/VesperProcessor.cpp
    src/VesperEditor.cpp
    src/core/PresetManager.cpp)

target_include_directories(Vesper PRIVATE src)

target_compile_definitions(Vesper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # Splash disabled (D-053).
    #
    # READ BEFORE DISTRIBUTING. JUCE permits this ONLY under one of:
    #   (a) a paid JUCE licence (Indie / Pro), or
    #   (b) GPLv3 — i.e. Vesper's own source is released under GPLv3.
    # The free "Personal" tier requires the splash to stay on. Building and
    # using it privately is one thing; shipping a closed-source binary with
    # the splash off and no paid licence is a licence violation. Pick (a) or
    # (b) before Vesper goes anywhere near a customer — see docs/LICENSING.md.
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Vesper
    PRIVATE
        VesperData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Denormals off + fast-ish math where safe (keep strict IEEE for tests).
if(MSVC)
    target_compile_options(Vesper PRIVATE /fp:fast /arch:AVX2)
else()
    target_compile_options(Vesper PRIVATE -ffast-math -fno-finite-math-only -msse4.2)
endif()

# MinGW: JUCE's amalgamated TUs exceed the default COFF section limit, and the
# plugin must not depend on the compiler's runtime DLLs on end-user machines.
if(MINGW)
    target_compile_options(Vesper PRIVATE -Wa,-mbig-obj)
    # PUBLIC: must reach the per-format link targets (Vesper_VST3, _Standalone),
    # not just the shared-code lib, or the runtime DLL imports sneak back in.
    target_link_options(Vesper PUBLIC -static)
endif()

# ----------------------------------------------------------------- tests ----
option(VESPER_BUILD_TESTS "Build the Vesper unit test runner" ON)
if(VESPER_BUILD_TESTS)
    juce_add_console_app(VesperTests PRODUCT_NAME "VesperTests")
    target_sources(VesperTests PRIVATE
        tests/TestMain.cpp
        src/core/PresetManager.cpp) # preset recents QA (D-042)
    target_include_directories(VesperTests PRIVATE src)
    target_compile_definitions(VesperTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        )
    target_link_libraries(VesperTests PRIVATE
        VesperData                      # factory presets, for preset QA (D-040)
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(MINGW)
        target_compile_options(VesperTests PRIVATE -Wa,-mbig-obj)
        target_link_options(VesperTests PRIVATE -static)
    endif()
    # Full-processor integration suite (D-043): drives the real
    # VesperProcessor the way a host does. Links the plugin's shared-code
    # target so JucePlugin_* definitions and the whole chain come along.
    juce_add_console_app(VesperIntegration PRODUCT_NAME "VesperIntegration")
    target_sources(VesperIntegration PRIVATE tests/IntegrationMain.cpp)
    target_include_directories(VesperIntegration PRIVATE src)
    # test-only: lets the harness pump the message loop so the processor's
    # AsyncUpdaters (conv IR, match, linear-phase FIR) actually run
    target_compile_definitions(VesperIntegration PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(VesperIntegration PRIVATE
        Vesper
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(MINGW)
        target_compile_options(VesperIntegration PRIVATE -Wa,-mbig-obj)
        target_link_options(VesperIntegration PRIVATE -static)
    endif()
    # juce_add_console_app hardcodes JUCE_STANDALONE_APPLICATION=1, but this
    # target links the Vesper plugin (whose interface says =0) — hosting a
    # plugin is what the harness does, so keep the plugin's definition and
    # drop the console app's to silence the "redefined" warning.
    get_target_property(_int_defs VesperIntegration COMPILE_DEFINITIONS)
    list(REMOVE_ITEM _int_defs JUCE_STANDALONE_APPLICATION=1)
    set_target_properties(VesperIntegration PROPERTIES COMPILE_DEFINITIONS "${_int_defs}")

    enable_testing()
    add_test(NAME VesperTests COMMAND VesperTests)
    add_test(NAME VesperIntegration COMMAND VesperIntegration)
endif()

# Third-party noise, not ours to fix (D-061 zero-warning console):
# juce_core.cpp -> UCRT 'tzname' deprecation; juce_graphics.cpp -> JUCE's own
# "#warning DirectWrite not implemented with MinGW" note. Our code still
# builds with the full recommended warning set — only these two JUCE unity
# sources are muted, per target that compiles them.
set(_juce_noisy_units
    ${juce_SOURCE_DIR}/modules/juce_core/juce_core.cpp
    ${juce_SOURCE_DIR}/modules/juce_graphics/juce_graphics.cpp)
set(_juce_unit_consumers Vesper)
if(TARGET VesperTests)
    list(APPEND _juce_unit_consumers VesperTests VesperIntegration)
endif()
set_source_files_properties(${_juce_noisy_units}
    TARGET_DIRECTORY ${_juce_unit_consumers}
    PROPERTIES COMPILE_OPTIONS "-w")
