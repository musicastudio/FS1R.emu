#include "PanelView.h"
#include "PluginProcessor.h"

namespace fs1rplug {

// ------------------------------------------------------------------------------------------ LCD
// A 5x7 dot matrix for the printable ASCII range. Only the characters the display actually shows are
// carried: letters, digits, and the punctuation the FS1R uses in patch names and value fields.
static const uint8_t kFont[96][5] = {
    {0,0,0,0,0},{0,0,0x5F,0,0},{0,7,0,7,0},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},
    {0x23,0x13,8,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0,5,3,0,0},{0,0x1C,0x22,0x41,0},
    {0,0x41,0x22,0x1C,0},{0x14,8,0x3E,8,0x14},{8,8,0x3E,8,8},{0,0x50,0x30,0,0},{8,8,8,8,8},
    {0,0x60,0x60,0,0},{0x20,0x10,8,4,2},
    {0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1E},{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},
    {8,0x14,0x22,0x41,0},{0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,8},{2,1,0x51,9,6},
    {0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,9,9,1,1},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,8,8,8,0x7F},{0,0x41,0x7F,0x41,0},{0x20,0x40,0x41,0x3F,1},{0x7F,8,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,2,4,2,0x7F},{0x7F,4,8,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,9,9,9,6},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,9,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {1,1,0x7F,1,1},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},{0,0x7F,0x41,0x41,0},
    {2,4,8,0x10,0x20},{0,0x41,0x41,0x7F,0},{4,2,1,2,4},{0x40,0x40,0x40,0x40,0x40},
    {0,1,2,4,0},
    {0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{8,0x7E,9,1,2},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,8,4,4,0x78},{0,0x44,0x7D,0x40,0},{0x20,0x40,0x44,0x3D,0},{0x7F,0x10,0x28,0x44,0},
    {0,0x41,0x7F,0x40,0},{0x7C,4,0x18,4,0x78},{0x7C,8,4,4,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,8},{8,0x14,0x14,0x18,0x7C},{0x7C,8,4,4,8},{0x48,0x54,0x54,0x54,0x20},
    {4,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0,8,0x36,0x41,0},{0,0,0x7F,0,0},{0,0x41,0x36,8,0},
    {8,4,8,0x10,8},{0x7F,0x7F,0x7F,0x7F,0x7F},
};

void LcdView::setLine(int row, const juce::String& text) {
    if (row < 0 || row > 1) return;
    auto t = text.substring(0, 40).paddedRight(' ', 40);
    if (t == lines[row]) return;
    lines[row] = t;
    repaint();
}

void LcdView::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff0d2b12));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(juce::Colour(0xff2a4a2a));
    g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);

    // One square dot: a 5x7 glyph in a 6x9 cell, 40 cells across and 2 down, scaled to whichever of the
    // two dimensions runs out first and centred in what is left.
    const float dot = juce::jmin((r.getWidth() - 12.0f) / (40 * 6.0f), (r.getHeight() - 10.0f) / (2 * 9.0f));
    const float gridW = dot * 40 * 6, gridH = dot * 2 * 9;
    const float padX = r.getX() + (r.getWidth() - gridW) * 0.5f;
    const float padY = r.getY() + (r.getHeight() - gridH) * 0.5f;
    const auto on = juce::Colour(0xff9bf08a), off = juce::Colour(0xff163a1c);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 40; ++col) {
            int c = col < lines[row].length() ? lines[row][col] : ' ';
            if (c < 32 || c > 127) c = ' ';
            const uint8_t* glyph = kFont[c - 32];
            float x0 = padX + col * dot * 6, y0 = padY + row * dot * 9 + dot;
            for (int gx = 0; gx < 5; ++gx)
                for (int gy = 0; gy < 7; ++gy) {
                    g.setColour((glyph[gx] >> gy) & 1 ? on : off);
                    g.fillRect(x0 + gx * dot, y0 + gy * dot, dot * 0.86f, dot * 0.86f);
                }
        }
    }
}

// ------------------------------------------------------------------------------------------ knob
PanelKnob::PanelKnob() { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }

void PanelKnob::setValue(int v, bool notify) {
    v = juce::jlimit(0, 127, v);
    if (v == val) return;
    val = v;
    repaint();
    if (notify && onMove) onMove(val);
}

void PanelKnob::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    auto cap = r.removeFromTop(12);
    g.setColour(juce::Colour(0xffbfc4c8));
    g.setFont(9.0f);
    g.drawText(caption, cap, juce::Justification::centred);
    auto dial = r.toFloat().reduced(2.0f);
    float d = juce::jmin(dial.getWidth(), dial.getHeight());
    dial = juce::Rectangle<float>(dial.getCentreX() - d / 2, dial.getCentreY() - d / 2, d, d);
    g.setColour(juce::Colour(0xff26292c));
    g.fillEllipse(dial);
    g.setColour(juce::Colour(0xff45494d));
    g.drawEllipse(dial.reduced(0.5f), 1.2f);
    float a = juce::MathConstants<float>::pi * (0.75f + 1.5f * val / 127.0f);
    auto c = dial.getCentre();
    float rad = d * 0.42f;
    g.setColour(juce::Colour(0xffe8b84b));
    g.drawLine(c.x, c.y, c.x + std::sin(a) * rad, c.y - std::cos(a) * rad, 2.0f);
}

void PanelKnob::mouseDown(const juce::MouseEvent& e) { dragStart = val; dragFrom = e.getPosition(); }

void PanelKnob::mouseDrag(const juce::MouseEvent& e) {
    int dy = dragFrom.y - e.getPosition().y;
    setValue(dragStart + dy, true);
}

// ------------------------------------------------------------------------------------------ panel
PanelView::PanelView(Processor& p) : proc(p) {
    setLookAndFeel(&look);
    addAndMakeVisible(lcd);

    struct { juce::TextButton* b; const char* text; const char* page; } buttons[] = {
        {&util, "UTIL", "System"}, {&play, "PLAY", "All parameters"},
        {&editPerf, "PERF", "Performance"}, {&editEffect, "EFFECT", "Effects"},
        {&editVoice, "VOICE", "Operators"}, {&muteSolo, "M/S", nullptr},
    };
    for (auto& b : buttons) {
        b.b->setButtonText(b.text);
        b.b->setConnectedEdges(0);
        const char* page = b.page;
        b.b->onClick = [this, page] { if (page && onModeButton) onModeButton(page); };
        addAndMakeVisible(*b.b);
    }
    partDown.setButtonText("PART <");
    partUp.setButtonText("PART >");
    partDown.onClick = [this] { proc.selectPart(juce::jmax(0, proc.selectedPart() - 1)); };
    partUp.onClick = [this] { proc.selectPart(juce::jmin(3, proc.selectedPart() + 1)); };
    addAndMakeVisible(partDown);
    addAndMakeVisible(partUp);

    // Knob mode: upper lit = ATTACK / RELEASE / FORMANT / FM straight into the part, lower lit = KN1-4
    // through the performance's control matrix, both off = nothing assigned, as on the hardware.
    knobModeA.setButtonText("TONE");
    knobModeB.setButtonText("KN1-4");
    knobModeA.setClickingTogglesState(true);
    knobModeB.setClickingTogglesState(true);
    knobModeA.setRadioGroupId(9);
    knobModeB.setRadioGroupId(9);
    knobModeA.setToggleState(true, juce::dontSendNotification);
    auto relabel = [this] {
        bool tone = knobModeA.getToggleState();
        const char* a[4] = {"ATTACK", "RELEASE", "FORMANT", "FM"};
        const char* b[4] = {"KN1", "KN2", "KN3", "KN4"};
        for (int i = 0; i < 4; ++i) knobs[i].setCaption(tone ? a[i] : b[i]);
    };
    knobModeA.onClick = relabel;
    knobModeB.onClick = relabel;
    addAndMakeVisible(knobModeA);
    addAndMakeVisible(knobModeB);
    for (int i = 0; i < 4; ++i) {
        knobs[i].onMove = [this, i](int v) { knobMoved(i, v); };
        addAndMakeVisible(knobs[i]);
    }
    relabel();

    struct { juce::TextButton* b; const char* text; } small[] = {
        {&cursorL, "<"}, {&cursorR, ">"}, {&valueDown, "-"}, {&valueUp, "+"},
        {&exitBtn, "EXIT"}, {&enterBtn, "ENTER"}, {&search, "SEARCH"},
    };
    for (auto& s : small) { s.b->setButtonText(s.text); addAndMakeVisible(*s.b); }
    valueDown.onClick = [this] { proc.selectPart(juce::jmax(0, proc.selectedPart() - 1)); };
    valueUp.onClick = [this] { proc.selectPart(juce::jmin(3, proc.selectedPart() + 1)); };
    exitBtn.onClick = [this] { if (onModeButton) onModeButton("All parameters"); };
    search.onClick = [this] { if (onModeButton) onModeButton("__search"); };

    volume.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    volume.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volume.setRange(0.0, 1.0, 0.001);
    volume.setValue(0.25, juce::dontSendNotification);
    volume.onValueChange = [this] { proc.device().setGain(volume.getValue()); };
    addAndMakeVisible(volume);

    startTimerHz(15);
}

PanelView::~PanelView() { stopTimer(); setLookAndFeel(nullptr); }

void PanelView::knobMoved(int knob, int v) {
    // TONE mode writes the part's own offsets; KN1-4 send the system's control numbers so the value
    // travels through the performance's control matrix exactly as the hardware's knobs do.
    if (knobModeA.getToggleState()) {
        static const int addr[4] = {0x1A, 0x1C, 0x1D, 0x1E};   // attack, release, formant, FM
        uint8_t m[10] = {0xF0, 0x43, 0x10, 0x5E, (uint8_t)(0x30 + proc.selectedPart()), 0x00,
                         (uint8_t)addr[knob], 0, (uint8_t)v, 0xF7};
        proc.device().sendMidi(m, 10);
        static const char* names[4] = {"Attack Time", "Release Time", "Formant", "FM"};
        showParameterOnLcd(names[knob], juce::String(v - 64));
    } else {
        static const int defaultCc[4] = {16, 17, 18, 19};      // system 0x16-0x19, the KN1-4 numbers
        uint8_t m[3] = {(uint8_t)(0xB0), (uint8_t)defaultCc[knob], (uint8_t)v};
        proc.device().sendMidi(m, 3);
        showParameterOnLcd("KN" + juce::String(knob + 1) + " (CC" + juce::String(defaultCc[knob]) + ")",
                           juce::String(v));
    }
}

void PanelView::showParameterOnLcd(const juce::String& name, const juce::String& value) {
    pendingName = name;
    pendingValue = value;
    pendingCountdown = 30;                                     // about two seconds at 15 Hz
}

void PanelView::refreshLcd() {
    auto& dv = proc.device();
    int part = proc.selectedPart();
    juce::String l0, l1;
    if (pendingCountdown > 0) {
        --pendingCountdown;
        l0 = pendingName.substring(0, 40);
        l1 = juce::String("PART ") + juce::String(part + 1) + "  " + pendingValue;
    } else {
        // The play screen: performance name on the top line, the selected part's voice below, with the
        // Fseq and algorithm where the hardware puts its status icons.
        l0 = juce::String(dv.performanceName()).trimEnd();
        if (dv.fseqFrames()) l0 = l0.paddedRight(' ', 22) + "FSEQ " + juce::String(dv.fseqName()).trim();
        l1 = "P" + juce::String(part + 1) + " " + juce::String(dv.voiceName(part)).trimEnd();
        l1 = l1.paddedRight(' ', 22) + "ALG " + juce::String(dv.algorithm(part) + 1);
        if (!dv.partActive(part)) l1 = l1.paddedRight(' ', 32) + "OFF";
    }
    lcd.setLine(0, l0);
    lcd.setLine(1, l1);
}

void PanelView::timerCallback() { refreshLcd(); }

void PanelView::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient grad(juce::Colour(0xff35393d), 0, r.getY(),
                              juce::Colour(0xff222528), 0, r.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(juce::Colour(0xff4a4f54));
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    // rack ears
    g.setColour(juce::Colour(0xff17191b));
    for (float x : {r.getX() + 10.0f, r.getRight() - 14.0f})
        for (float y : {r.getY() + 10.0f, r.getBottom() - 14.0f})
            g.fillEllipse(x, y, 5.0f, 5.0f);
    g.setColour(juce::Colour(0xff8f959b));
    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    g.drawText("FS1R", r.removeFromLeft(78).withTrimmedTop(6), juce::Justification::centredTop);
    g.setFont(9.0f);
    g.drawText("FM synthesizer", r.withTrimmedLeft(-70).withTrimmedTop(20).withWidth(78),
               juce::Justification::centredTop);
}

void PanelView::resized() {
    auto r = getLocalBounds().reduced(10, 8);
    r.removeFromLeft(78);                                      // the FS1R badge painted above

    auto left = r.removeFromLeft(52);
    volume.setBounds(left.removeFromTop(left.getHeight() * 2 / 3).withSizeKeepingCentre(38, 38));

    // 40 characters across at a legible dot size wants roughly 2.7 px per character column.
    int lcdW = juce::jlimit(320, 560, r.getWidth() * 46 / 100);
    lcd.setBounds(r.removeFromLeft(lcdW).withSizeKeepingCentre(lcdW - 6, 78));

    auto right = r;
    auto top = right.removeFromTop(right.getHeight() / 2).reduced(2);
    auto bottom = right.reduced(2);

    // cursor, value, exit and enter keep a fixed width; the mode row takes what is left
    auto tail = top.removeFromRight(juce::jmin(top.getWidth() / 2, 214));
    cursorL.setBounds(tail.removeFromLeft(26).reduced(2));
    cursorR.setBounds(tail.removeFromLeft(26).reduced(2));
    valueDown.setBounds(tail.removeFromLeft(26).reduced(2));
    valueUp.setBounds(tail.removeFromLeft(26).reduced(2));
    exitBtn.setBounds(tail.removeFromLeft(52).reduced(2));
    enterBtn.setBounds(tail.reduced(2));
    juce::TextButton* modeRow[] = {&util, &play, &editPerf, &editEffect, &editVoice, &muteSolo};
    int w = juce::jmax(36, top.getWidth() / 6);
    for (auto* b : modeRow) b->setBounds(top.removeFromLeft(w).reduced(2));

    partDown.setBounds(bottom.removeFromLeft(56).reduced(2));
    partUp.setBounds(bottom.removeFromLeft(56).reduced(2));
    knobModeA.setBounds(bottom.removeFromLeft(48).reduced(2));
    knobModeB.setBounds(bottom.removeFromLeft(48).reduced(2));
    search.setBounds(bottom.removeFromRight(58).reduced(2));
    int kw = juce::jmax(44, bottom.getWidth() / 4);
    for (auto& k : knobs) k.setBounds(bottom.removeFromLeft(kw).reduced(2));
}

}  // namespace fs1rplug
