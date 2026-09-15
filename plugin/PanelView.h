// PanelView - the FS1R front panel, reproduced 1:1 and drawn as vectors.
//
// Tier 3 of the TODO wants this skinned from photographs of a real unit: a background PNG, knob strips,
// button and LED states, an LCD font. Nobody has measured a panel yet, so the layout here follows the
// owner's manual's front panel description (phones, volume, the 2x40 display, UTIL, PLAY, the three
// EDIT buttons, MUTE/SOLO, PART, the two knob mode buttons, four controller knobs, cursor, value, exit,
// enter, search) and is drawn rather than blitted. Drawing it means it survives resizing and needs no
// assets; swapping in a skin later replaces the paint methods, not the wiring.
//
// The LCD shows our own strings in the FS1R layout. It is not the firmware's menu system: the engine
// has no display, so the plugin composes what the hardware would have shown.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace fs1rplug {

class Processor;

// A 2x40 character dot-matrix display, drawn character by character from a 5x7 font.
class LcdView : public juce::Component {
public:
    void setLine(int row, const juce::String& text);
    void paint(juce::Graphics&) override;

private:
    juce::String lines[2];
};

// One panel knob. Value 0-127, no text box; the caption sits above it as on the hardware.
class PanelKnob : public juce::Component {
public:
    PanelKnob();
    void setCaption(const juce::String& c) { caption = c; repaint(); }
    void setValue(int v, bool notify);
    int value() const { return val; }
    std::function<void(int)> onMove;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    juce::String caption;
    int val = 64, dragStart = 0;
    juce::Point<int> dragFrom;
};

// The panel's legends are silkscreen, so they stay small whatever the button size.
class PanelLook : public juce::LookAndFeel_V4 {
public:
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return juce::Font(juce::FontOptions(9.5f)); }
};

class PanelView : public juce::Component, private juce::Timer {
public:
    explicit PanelView(Processor&);
    ~PanelView() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // The panel's mode buttons drive the page tabs above; the editor supplies this.
    std::function<void(const juce::String& page)> onModeButton;
    void showParameterOnLcd(const juce::String& name, const juce::String& value);

private:
    void timerCallback() override;
    void refreshLcd();
    void knobMoved(int knob, int value);

    Processor& proc;
    PanelLook look;
    LcdView lcd;
    PanelKnob knobs[4];
    juce::TextButton util, play, editPerf, editEffect, editVoice, muteSolo;
    juce::TextButton partDown, partUp, knobModeA, knobModeB;
    juce::TextButton cursorL, cursorR, valueDown, valueUp, exitBtn, enterBtn, search;
    juce::Slider volume;
    juce::String pendingName, pendingValue;
    int pendingCountdown = 0;
    juce::String lastLine0, lastLine1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelView)
};

}  // namespace fs1rplug
