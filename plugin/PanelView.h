// PanelView - the FS1R front panel, drawn from the owner's manual's own artwork.
//
// docs/FS1R-front-panel-p14-ny.svg is page 14 of the owner's manual converted to vectors and cleaned
// up. Everything the panel looks like - the face, the silkscreen, the knob caps, the LED wells, the
// LCD surround - is that drawing. This file only places live controls over it, lights the LEDs and
// fills the display; nothing here repaints the panel's own graphics.
//
// Every coordinate below is in the drawing's own user units. They were read out of the file rather
// than measured by eye, and tools/check_panel.py reads them again and fails the build's test step if
// the drawing and these numbers ever drift apart.
//
// The LCD is the hardware's, not the firmware's: the engine has no display, so the plugin composes
// what the FS1R would have shown - two lines of text and the icon strip along the bottom that tracks
// PART, MIDI, BANK/PGM#, VOL, FLT, PAN, REV, VAR, INS and KEY (owner's manual page 22).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstring>
#include <cmath>
#include <functional>
#include <memory>

namespace fs1rplug {

class Processor;

// The panel's graphic LCD, as dots. 18 characters across on a 6x8 cell, two text lines, then the icon
// strip; the strip's ten fields line up with the silkscreen labels printed under the glass.
class LcdView : public juce::Component {
public:
    static constexpr int kCols = 120, kRows = 37;
    static constexpr int kLine1 = 1, kLine2 = 10, kPointerRow = 19, kFieldTop = 23, kFieldBottom = 35;

    void clear();
    void dot(int x, int y, bool on = true);
    void frame(int x, int y, int w, int h);               // one-dot outline
    void fill(int x, int y, int w, int h, bool on = true);
    int text(int col, int row, const juce::String&);      // returns the column after the last glyph
    void textRight(int rightCol, int row, const juce::String& s);
    void centred(int centreCol, int row, const juce::String& s);
    void commit();                                        // repaint only if anything moved

    void paint(juce::Graphics&) override;

private:
    uint8_t fb[kRows][kCols] = {}, shown[kRows][kCols] = {};
};

// One panel control: a pot for the four knobs and the volume, drawn as the artwork's own knob cap
// rotated about its centre.
class PanelKnob : public juce::Component {
public:
    void setCap(const juce::Image& i, float restDegrees, float sweepDegrees);
    void setValue(int v, bool notify);
    int value() const { return val; }
    std::function<void(int, int)> onMove;                 // (value, delta since the last callback)

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Image cap;
    float rest = 0.0f, sweep = 300.0f;
    int val = 64, top = 127, dragStart = 0, lastNotified = 64;
    juce::Point<int> dragFrom;
};

// A panel button. The artwork already draws the cap and the LED well; this only darkens the cap while
// it is held and lights the LED when the mode it selects is the live one.
class PanelButton : public juce::Button {
public:
    PanelButton(const juce::String& name, juce::Colour ledColour);
    void setLit(bool l) { if (l != lit) { lit = l; repaint(); } }
    bool isLit() const { return lit; }
    void paintButton(juce::Graphics&, bool over, bool down) override;

private:
    juce::Colour led;
    bool lit = false;
};

class PanelView : public juce::Component, private juce::Timer {
public:
    // The artwork's own proportions. The editor sizes the panel from this so the drawing never stretches.
    static constexpr float kAspect = 394.11f / 47.64f;

    explicit PanelView(Processor&);
    ~PanelView() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    // The panel's mode buttons drive the page tabs below; the editor supplies this.
    std::function<void(const juce::String& page)> onModeButton;
    void showParameterOnLcd(const juce::String& name, const juce::String& value);

private:
    // The six lit buttons down the left of the button block, in the artwork's order.
    enum Mode { Play, Util, Search, EditPerf, EditEffect, EditVoice, kNumModes };
    // The two knob mode buttons: upper lit, lower lit, or neither (owner's manual page 15).
    enum KnobMode { Tone, Kn1to4, Navigate };

    void timerCallback() override;
    void buildArt();                                      // the cached panel bitmap and knob caps
    void refreshLcd();
    void knobMoved(int knob, int value, int delta);
    void setMode(Mode m);
    void setKnobMode(KnobMode m);
    void stepField(int by);                               // VALUE -/+ on the selected icon field
    void showMidiView();
    juce::Rectangle<int> place(float x, float y, float w, float h) const;
    juce::Rectangle<int> placeCircle(float cx, float cy, float r) const;

    Processor& proc;
    std::unique_ptr<juce::Drawable> art;
    juce::Image panelImage, knobCap, volumeCap;
    juce::AffineTransform fit;                            // artwork units -> this component
    float scale = 1.0f;

    LcdView lcd;
    PanelKnob knobs[4], volume;
    std::unique_ptr<PanelButton> modeButtons[kNumModes];
    std::unique_ptr<PanelButton> knobModeButtons[2];
    std::unique_ptr<PanelButton> muteSolo, enterBtn, exitBtn;
    std::unique_ptr<PanelButton> partDown, partUp, cursorL, cursorR, valueDown, valueUp;

    KnobMode knobMode = Tone;
    int field = 0;                                        // which icon strip field the cursor is on
    bool muted = false;
    double gain = 0.25;
    juce::String pendingName, pendingValue;
    int pendingCountdown = 0;
    juce::uint32 lastEnterClick = 0;
    bool midiView = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelView)
};

}  // namespace fs1rplug
