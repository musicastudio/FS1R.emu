# The plugin layer: a controller for fs1rLib, never a synthesiser.
#   cmake -B build/cmake -S . -DFS1R_BUILD_PLUGIN=ON
#   cmake --build build/cmake --config Release
# Needs the submodules: git submodule update --init --recursive
juce_add_plugin(fs1r_plugin
    PRODUCT_NAME "FS1R.emu"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Musica.Studio"
    PLUGIN_MANUFACTURER_CODE Msst
    PLUGIN_CODE Fs1r
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

juce_add_binary_data(fs1r_plugin_data SOURCES parameterDescriptions_fs1r.json)
set_target_properties(fs1r_plugin_data PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(fs1r_plugin PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    PanelView.cpp
    EditorPages.cpp
    ParameterDescriptions.cpp
    PatchManager.cpp)

target_compile_definitions(fs1r_plugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(fs1r_plugin PRIVATE
    fs1rLib
    fs1r_plugin_data
    juce::juce_audio_utils
    juce::juce_audio_processors
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

# CLAP through clap-juce-extensions, which reuses the same target.
if(TARGET clap_juce_extensions)
    clap_juce_extensions_plugin(TARGET fs1r_plugin
        CLAP_ID "studio.musica.fs1r-emu"
        CLAP_FEATURES instrument synthesizer "FM" stereo)
endif()
