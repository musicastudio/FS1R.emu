# The plugin layer: a controller for fs1rLib, never a synthesiser.
#   cmake -B build/cmake -S . -DFS1R_BUILD_PLUGIN=ON
#   cmake --build build/cmake --config Release
# Needs the submodules: git submodule update --init --recursive
juce_add_plugin(fs1r_plugin
    PRODUCT_NAME "FSVR"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Musica.Studio"
    BUNDLE_ID "studio.musica.fsvr"          # said outright, or JUCE builds it from the target name
    ICON_BIG "${PROJECT_SOURCE_DIR}/icon.png"   # juceaide turns it into the exe's icon and the bundle's
    PLUGIN_MANUFACTURER_CODE Msst
    PLUGIN_CODE Fsvr
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

# The formats land in bin/Standalone, bin/VST3 and bin/CLAP (JUCE and the CLAP helper derive each
# format's folder from these two); the import libraries stay in the build tree.
set_target_properties(fs1r_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${FS1R_BIN}
    RUNTIME_OUTPUT_DIRECTORY ${FS1R_BIN})

# fs1r_panel.svg is the front panel drawing, made from docs/FS1R-front-panel-p14-ny.svg by
# tools/make_panel_svg.py. Edit the drawing in docs/ and run that script again; it is committed so the
# build needs no Python.
# The factory patches: 1408 voices with their name index, 384 performances and the 90 preset formant
# sequences, packed out of presets/ by tools/make_presets_blob.py so the plugin browses and loads all
# of them with no EPROM image. Committed, for the same reason as the panel.
juce_add_binary_data(fs1r_plugin_data SOURCES
    generated/parameterDescriptions_fs1r.json
    generated/fs1r_panel.svg
    generated/fs1r_presets.syx
    generated/fs1r_presets.csv
    generated/fs1r_performances.syx
    generated/fs1r_fseqs.syx)
set_target_properties(fs1r_plugin_data PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(fs1r_plugin PRIVATE
    PluginProcessor.cpp
    gui/PluginEditor.cpp
    gui/PanelView.cpp
    gui/EditorPages.cpp
    patch/ParameterDescriptions.cpp
    patch/PatchManager.cpp)

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
        CLAP_ID "studio.musica.fsvr"
        CLAP_FEATURES instrument synthesizer "FM" stereo)
endif()
