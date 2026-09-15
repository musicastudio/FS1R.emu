#include "PanelView.h"
#include "PluginProcessor.h"
#include "BinaryData.h"

namespace fs1rplug {

// ------------------------------------------------------------------------------------ the artwork
// Coordinates in docs/FS1R-front-panel-p14-ny.svg's own user units. tools/check_panel.py reads the
// same circles back out of the drawing and fails if these ever drift apart.
namespace art {
constexpr float panelX = 109.45f, panelY = 203.84f, panelW = 394.11f, panelH = 47.64f;
constexpr float lcdX = 147.49f, lcdY = 212.91f, lcdW = 96.16f, lcdH = 29.71f;

constexpr float volumeX = 120.44f, volumeY = 225.49f, volumeR = 6.14f;
constexpr float knobY = 228.03f, knobR = 8.62f;
constexpr float knobX[4] = {384.54f, 416.53f, 448.51f, 480.50f};

// The six lit buttons: PLAY / UTIL / SEARCH down the left column, EDIT PERFORM / EFFECT / VOICE down
// the right one.
constexpr float ledR = 2.70f;
constexpr float ledX[2] = {276.06f, 286.33f};
constexpr float ledY[3] = {219.39f, 230.74f, 242.08f};

// The nine plain buttons: MUTE/SOLO, ENTER, EXIT down the first column, then PART, CURSOR and VALUE
// in their minus and plus columns.
constexpr float btnR = 2.90f;
constexpr float btnX[3] = {303.31f, 314.10f, 324.90f};
constexpr float btnY[3] = {219.37f, 230.73f, 242.11f};

// The two knob mode buttons, each a red LED at the left end of its label strip.
constexpr float knobModeX = 363.04f, knobModeR = 2.16f;
constexpr float knobModeY[2] = {217.23f, 238.84f};
}  // namespace art

// The panel's LED colours. The drawing paints them dark gold, which is what an unlit LED looks like.
static const juce::Colour kGreenLed(0xff7de84a), kRedLed(0xffff3b2a);

// ------------------------------------------------------------------------------------------- font
// A 5x7 dot matrix for the printable ASCII range, one byte per column, bit 0 at the top.
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
// Codes 1, 2 and 3 are the display's own marks: the solid pointer that sits against the selected half
// of a bank / program pair, the hollow one against the other half, and the performance edit mark.
static const uint8_t kMarks[3][5] = {
    {0x7F,0x3E,0x1C,8,0}, {0x7F,0x22,0x14,8,0}, {0x7F,0x41,0x55,0x55,0x7F},
};

// -------------------------------------------------------------------------------------------- LCD
void LcdView::clear() { std::memset(fb, 0, sizeof(fb)); }

void LcdView::dot(int x, int y, bool on) {
    if ((unsigned)x < (unsigned)kCols && (unsigned)y < (unsigned)kRows) fb[y][x] = on ? 1 : 0;
}

void LcdView::fill(int x, int y, int w, int h, bool on) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) dot(x + i, y + j, on);
}

void LcdView::frame(int x, int y, int w, int h) {
    fill(x, y, w, 1);
    fill(x, y + h - 1, w, 1);
    fill(x, y, 1, h);
    fill(x + w - 1, y, 1, h);
}

int LcdView::text(int col, int row, const juce::String& s) {
    for (auto c : s) {
        const uint8_t* glyph = (c >= 1 && c <= 3)     ? kMarks[c - 1]
                             : (c >= 32 && c <= 127)  ? kFont[c - 32]
                                                      : kFont[0];
        for (int gx = 0; gx < 5; ++gx)
            for (int gy = 0; gy < 7; ++gy)
                if ((glyph[gx] >> gy) & 1) dot(col + gx, row + gy);
        col += 6;
    }
    return col;
}

void LcdView::textRight(int rightCol, int row, const juce::String& s) {
    text(rightCol - s.length() * 6, row, s);
}

void LcdView::centred(int centreCol, int row, const juce::String& s) {
    text(centreCol - (s.length() * 6 - 1) / 2, row, s);
}

void LcdView::commit() {
    if (std::memcmp(fb, shown, sizeof(fb)) == 0) return;
    std::memcpy(shown, fb, sizeof(fb));
    repaint();
}

void LcdView::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    const float dx = r.getWidth() / kCols, dy = r.getHeight() / kRows;
    g.setColour(juce::Colour(0xff82ca9c));                 // the backlight, the drawing's own green
    g.fillRect(r);
    g.setColour(juce::Colour(0xff17301f));
    for (int y = 0; y < kRows; ++y)
        for (int x = 0; x < kCols; ++x)
            if (shown[y][x])
                g.fillRect(r.getX() + x * dx, r.getY() + y * dy, dx * 0.92f, dy * 0.92f);
}

// ------------------------------------------------------------------------------------------- knob
void PanelKnob::setCap(const juce::Image& i, float restDegrees, float sweepDegrees) {
    cap = i;
    rest = restDegrees;
    sweep = sweepDegrees;
    repaint();
}

void PanelKnob::setValue(int v, bool notify) {
    v = juce::jlimit(0, top, v);
    if (v == val) return;
    val = v;
    repaint();
    if (notify && onMove) {
        onMove(val, val - lastNotified);
        lastNotified = val;
    }
}

void PanelKnob::paint(juce::Graphics& g) {
    if (cap.isNull()) return;
    auto r = getLocalBounds().toFloat();
    const float deg = rest + sweep * (top > 0 ? (float)val / (float)top : 0.0f);
    auto t = juce::AffineTransform::scale(r.getWidth() / (float)cap.getWidth(),
                                          r.getHeight() / (float)cap.getHeight())
                 .translated(r.getX(), r.getY())
                 .rotated(juce::degreesToRadians(deg), r.getCentreX(), r.getCentreY());
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImageTransformed(cap, t);
}

void PanelKnob::mouseDown(const juce::MouseEvent& e) {
    dragStart = val;
    lastNotified = val;
    dragFrom = e.getPosition();
}

void PanelKnob::mouseDrag(const juce::MouseEvent& e) {
    // A pot, not an encoder: the cap follows the drag from wherever it was grabbed, finer with shift.
    const float step = e.mods.isShiftDown() ? 0.25f : 1.0f;
    setValue(dragStart + (int)((dragFrom.y - e.getPosition().y) * step), true);
}

void PanelKnob::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    setValue(val + (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0), true);
}

// ----------------------------------------------------------------------------------------- button
PanelButton::PanelButton(const juce::String& name, juce::Colour ledColour)
    : juce::Button(name), led(ledColour) {
    setTooltip(name);
}

void PanelButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto r = getLocalBounds().toFloat();
    if (!led.isTransparent() && lit) {
        // The drawing paints an unlit LED, so lighting one means painting over its well from the
        // middle out, with a little spill onto the face around it.
        g.setColour(led.withAlpha(0.30f));
        g.fillEllipse(r.expanded(r.getWidth() * 0.35f));
        g.setColour(led);
        g.fillEllipse(r);
        g.setColour(led.brighter(0.7f));
        g.fillEllipse(r.reduced(r.getWidth() * 0.28f));
    }
    // MUTE/SOLO has no LED on the hardware, so a mute that is still on reads as a cap still held in.
    if (down || (lit && led.isTransparent())) {
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillEllipse(r);
    } else if (over) {
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.fillEllipse(r);
    }
}

// -------------------------------------------------------------------------------------- the strip
// The ten fields along the bottom of the display, in the order of the silkscreen printed under the
// glass: PART MIDI BANK/PGM# VOL FLT PAN REV VAR INS KEY. x is the field's centre in display dots,
// taken from where each label sits on the panel. All but PART are part parameters, which is what the
// CURSOR and VALUE buttons walk over.
namespace {
struct Field {
    const char* label;
    float x;
    const char* group;
    const char* param;
};
const Field kFields[10] = {
    {"Part",     8.5f,   nullptr, nullptr},
    {"Rcv Ch",   24.6f,  "Part",  "Rcv CHANNEL A1~A16, pfm, off"},
    {"Pgm#",     44.2f,  "Part",  "PROGRAM NUMBER"},
    {"Volume",   58.4f,  "Part",  "VOLUME"},
    {"Filter",   67.1f,  "Part",  "FilterSw"},
    {"Pan",      75.6f,  "Part",  "PAN Rnd,"},
    {"RevSend",  84.7f,  "Part",  "REVERB SEND"},
    {"VarSend",  93.2f,  "Part",  "VARIATION SEND"},
    {"Ins Sw",   101.9f, "Part",  "INSERTION SW"},
    {"NoteSft",  111.2f, "Part",  "NOTE SHIFT"},
};

// Finds a parameter by group and name. Everything the display shows goes through the parameter list,
// so the panel never reaches into the engine either.
int describeIndex(Processor& p, const char* group, const char* name) {
    if (group == nullptr) return -1;
    const auto& d = p.descriptions();
    for (size_t i = 0; i < d.size(); ++i)
        if (d[i].group == group && d[i].name == name) return (int)i;
    return -1;
}
}  // namespace

// ------------------------------------------------------------------------------------------ panel
PanelView::PanelView(Processor& p) : proc(p) {
    art = juce::Drawable::createFromImageData(BinaryData::fs1r_panel_svg,
                                              (size_t)BinaryData::fs1r_panel_svgSize);
    jassert(art != nullptr);
    addAndMakeVisible(lcd);

    struct { Mode m; const char* name; const char* page; } modes[] = {
        {Play, "PLAY", "All parameters"}, {Util, "UTIL", "System"}, {Search, "SEARCH", "__search"},
        {EditPerf, "EDIT PERFORM", "Performance"}, {EditEffect, "EDIT EFFECT", "Effects"},
        {EditVoice, "EDIT VOICE", "Operators"},
    };
    for (auto& m : modes) {
        auto b = std::make_unique<PanelButton>(m.name, kGreenLed);
        const char* page = m.page;
        const Mode which = m.m;
        b->onClick = [this, which, page] {
            setMode(which);
            if (onModeButton) onModeButton(page);
        };
        addAndMakeVisible(*b);
        modeButtons[m.m] = std::move(b);
    }
    modeButtons[Play]->setLit(true);

    muteSolo = std::make_unique<PanelButton>("MUTE/SOLO", juce::Colour());
    enterBtn = std::make_unique<PanelButton>("ENTER", juce::Colour());
    exitBtn = std::make_unique<PanelButton>("EXIT", juce::Colour());
    partDown = std::make_unique<PanelButton>("PART -", juce::Colour());
    partUp = std::make_unique<PanelButton>("PART +", juce::Colour());
    cursorL = std::make_unique<PanelButton>("CURSOR left", juce::Colour());
    cursorR = std::make_unique<PanelButton>("CURSOR right", juce::Colour());
    valueDown = std::make_unique<PanelButton>("VALUE -", juce::Colour());
    valueUp = std::make_unique<PanelButton>("VALUE +", juce::Colour());
    for (auto* b : {muteSolo.get(), enterBtn.get(), exitBtn.get(), partDown.get(), partUp.get(),
                    cursorL.get(), cursorR.get(), valueDown.get(), valueUp.get()})
        addAndMakeVisible(*b);

    // In the PLAY mode [MUTE/SOLO] mutes the whole performance rather than one part (owner's manual
    // page 22), so it silences the output instead of editing the patch.
    // ponytail: no per-part solo, which belongs to the edit modes and wants a part mute the engine
    // has not got; add it when the engine can mute a part without moving its volume.
    muteSolo->onClick = [this] {
        muted = !muted;
        muteSolo->setLit(muted);
        proc.device().setGain(muted ? 0.0 : gain);
        showParameterOnLcd("Performance", muted ? "MUTE" : "on");
    };
    // ponytail: no PART ALL; selectPart is 0-3 and every editor page follows it.
    partDown->onClick = [this] { proc.selectPart(juce::jmax(0, proc.selectedPart() - 1)); };
    partUp->onClick = [this] { proc.selectPart(juce::jmin(3, proc.selectedPart() + 1)); };
    cursorL->onClick = [this] { field = (field + 9) % 10; };
    cursorR->onClick = [this] { field = (field + 1) % 10; };
    valueDown->onClick = [this] { stepField(-1); };
    valueUp->onClick = [this] { stepField(1); };
    exitBtn->onClick = [this] {
        midiView = false;
        setMode(Play);
        if (onModeButton) onModeButton("All parameters");
    };
    // Double-clicking [ENTER] is the MIDI View: the sysex string that would set the selected
    // parameter from an external device (owner's manual page 15).
    enterBtn->onClick = [this] {
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastEnterClick < 400) showMidiView();
        lastEnterClick = now;
    };

    for (int i = 0; i < 2; ++i) {
        auto b = std::make_unique<PanelButton>(i == 0 ? "KNOB MODE upper" : "KNOB MODE lower", kRedLed);
        b->onClick = [this, i] {
            const KnobMode want = i == 0 ? Tone : Kn1to4;
            setKnobMode(knobMode == want ? Navigate : want);
        };
        addAndMakeVisible(*b);
        knobModeButtons[i] = std::move(b);
    }

    for (int i = 0; i < 4; ++i) {
        knobs[i].onMove = [this, i](int v, int d) { knobMoved(i, v, d); };
        addAndMakeVisible(knobs[i]);
    }
    volume.setValue((int)std::lround(gain * 127.0), false);
    volume.onMove = [this](int v, int) {
        gain = v / 127.0;
        if (!muted) proc.device().setGain(gain);
        showParameterOnLcd("Volume", juce::String(v));
    };
    addAndMakeVisible(volume);
    setKnobMode(Tone);

    startTimerHz(15);
}

PanelView::~PanelView() { stopTimer(); }

// ---------------------------------------------------------------------------------------- layout
juce::Rectangle<int> PanelView::place(float x, float y, float w, float h) const {
    return juce::Rectangle<float>(x, y, w, h).transformedBy(fit).toNearestInt();
}

juce::Rectangle<int> PanelView::placeCircle(float cx, float cy, float r) const {
    return place(cx - r, cy - r, 2 * r, 2 * r);
}

// Renders one circular piece of the drawing - a knob cap - into an image big enough that rotating it
// stays sharp, with the circle's own edge as its alpha so none of the face comes with it.
static juce::Image renderCap(juce::Drawable& d, float cx, float cy, float r, int px) {
    juce::Image img(juce::Image::ARGB, px, px, true);
    {
        juce::Graphics g(img);
        const float s = px / (2.0f * r);
        d.draw(g, 1.0f, juce::AffineTransform::translation(r - cx, r - cy).scaled(s, s));
    }
    juce::Image::BitmapData bits(img, juce::Image::BitmapData::readWrite);
    const float centre = px * 0.5f;
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x) {
            const float dx = x + 0.5f - centre, dy = y + 0.5f - centre;
            const float a = juce::jlimit(0.0f, 1.0f, centre - std::sqrt(dx * dx + dy * dy));
            if (a < 1.0f) ((juce::PixelARGB*)bits.getPixelPointer(x, y))->multiplyAlpha(a);
        }
    return img;
}

void PanelView::buildArt() {
    if (art == nullptr || getWidth() <= 0 || getHeight() <= 0) return;
    // ponytail: the drawing is 2700 paths, so it is rendered once per resize and blitted after that.
    panelImage = juce::Image(juce::Image::ARGB, getWidth(), getHeight(), true);
    {
        juce::Graphics g(panelImage);
        art->draw(g, 1.0f, fit);
    }
    const int capPx = juce::jlimit(32, 512, (int)std::lround(art::knobR * 6.0f * scale));
    knobCap = renderCap(*art, art::knobX[0], art::knobY, art::knobR, capPx);
    volumeCap = renderCap(*art, art::volumeX, art::volumeY, art::volumeR, capPx);
    // The drawing's four knobs point straight up, so they read as a pot centred at 64 over a 300
    // degree sweep. Its volume knob is drawn at the bottom of its scale, so that one starts there.
    for (auto& k : knobs) k.setCap(knobCap, -150.0f, 300.0f);
    volume.setCap(volumeCap, 0.0f, 280.0f);
}

void PanelView::resized() {
    // The drawing never stretches: it is fitted to whichever of the two dimensions runs out first and
    // centred in what is left.
    scale = juce::jmin(getWidth() / art::panelW, getHeight() / art::panelH);
    fit = juce::AffineTransform::translation(-art::panelX, -art::panelY)
              .scaled(scale, scale)
              .translated((getWidth() - art::panelW * scale) * 0.5f,
                          (getHeight() - art::panelH * scale) * 0.5f);

    lcd.setBounds(place(art::lcdX, art::lcdY, art::lcdW, art::lcdH));
    volume.setBounds(placeCircle(art::volumeX, art::volumeY, art::volumeR));
    for (int i = 0; i < 4; ++i)
        knobs[i].setBounds(placeCircle(art::knobX[i], art::knobY, art::knobR));

    PanelButton* leds[6] = {modeButtons[Play].get(),   modeButtons[EditPerf].get(),
                            modeButtons[Util].get(),   modeButtons[EditEffect].get(),
                            modeButtons[Search].get(), modeButtons[EditVoice].get()};
    for (int i = 0; i < 6; ++i)
        leds[i]->setBounds(placeCircle(art::ledX[i % 2], art::ledY[i / 2], art::ledR));

    PanelButton* btns[9] = {muteSolo.get(), partDown.get(),  partUp.get(),
                            enterBtn.get(), cursorL.get(),   cursorR.get(),
                            exitBtn.get(),  valueDown.get(), valueUp.get()};
    for (int i = 0; i < 9; ++i)
        btns[i]->setBounds(placeCircle(art::btnX[i % 3], art::btnY[i / 3], art::btnR));

    for (int i = 0; i < 2; ++i)
        knobModeButtons[i]->setBounds(placeCircle(art::knobModeX, art::knobModeY[i], art::knobModeR));

    buildArt();
}

void PanelView::paint(juce::Graphics& g) {
    if (panelImage.isValid()) g.drawImageAt(panelImage, 0, 0);
}

// -------------------------------------------------------------------------------------- behaviour
void PanelView::setMode(Mode m) {
    midiView = false;
    for (int i = 0; i < kNumModes; ++i)
        if (modeButtons[i]) modeButtons[i]->setLit(i == m);
}

void PanelView::setKnobMode(KnobMode m) {
    knobMode = m;
    knobModeButtons[0]->setLit(m == Tone);
    knobModeButtons[1]->setLit(m == Kn1to4);
}

void PanelView::knobMoved(int knob, int v, int delta) {
    static const char* toneNames[4] = {"Attack Time", "Release Time", "Formant", "FM"};
    switch (knobMode) {
        case Tone: {
            // Straight into the selected part, which is what the lit upper button means.
            static const int addr[4] = {0x1A, 0x1C, 0x1D, 0x1E};
            const uint8_t m[10] = {0xF0, 0x43, 0x10, 0x5E, (uint8_t)(0x30 + proc.selectedPart()), 0x00,
                                   (uint8_t)addr[knob], 0, (uint8_t)v, 0xF7};
            proc.device().sendMidi(m, 10);
            showParameterOnLcd(toneNames[knob], juce::String(v - 64));
            break;
        }
        case Kn1to4: {
            // Through the performance's control matrix, on the system's own KN1-KN4 control numbers.
            static const int defaultCc[4] = {16, 17, 18, 19};
            const uint8_t m[3] = {0xB0, (uint8_t)defaultCc[knob], (uint8_t)v};
            proc.device().sendMidi(m, 3);
            showParameterOnLcd("KN" + juce::String(knob + 1) + " (CC" +
                                   juce::String(defaultCc[knob]) + ")",
                               juce::String(v));
            break;
        }
        case Navigate:
            // Both buttons dark: part / operator, group, cursor and value, as printed under the knobs.
            if (delta == 0) break;
            if (knob == 0) proc.selectPart(juce::jlimit(0, 3, proc.selectedPart() + (delta > 0 ? 1 : -1)));
            else if (knob == 2) field = (field + (delta > 0 ? 1 : 9)) % 10;
            else if (knob == 3) stepField(delta);
            break;
    }
}

void PanelView::stepField(int by) {
    if (field == 0) {
        proc.selectPart(juce::jlimit(0, 3, proc.selectedPart() + (by > 0 ? 1 : -1)));
        return;
    }
    const int idx = describeIndex(proc, kFields[field].group, kFields[field].param);
    if (idx < 0) return;
    const auto& d = proc.descriptions()[(size_t)idx];
    auto* q = proc.parameterFor(idx);
    const int v = juce::jlimit(d.min, d.max,
                               (int)std::lround(q->convertFrom0to1(q->getValue())) + (by > 0 ? 1 : -1));
    q->setValueNotifyingHost(q->convertTo0to1((float)v));
    showParameterOnLcd(kFields[field].label, d.textFor(v));
}

void PanelView::showMidiView() {
    const int idx = describeIndex(proc, kFields[field].group, kFields[field].param);
    if (idx < 0) return;
    const auto& d = proc.descriptions()[(size_t)idx];
    auto* q = proc.parameterFor(idx);
    const int v = (int)std::lround(q->convertFrom0to1(q->getValue()));
    auto hex = [](int b) { return juce::String::toHexString(b).paddedLeft('0', 2).toUpperCase(); };
    const int high = d.partRelative ? d.addr[0] + proc.selectedPart() : d.addr[0];
    pendingName = d.name;
    pendingValue = "F0 43 10 5E " + hex(high) + " " + hex(d.addr[1]) + " " + hex(d.addr[2]) + " " +
                   hex(v) + " F7";
    pendingCountdown = 0;
    midiView = true;
}

void PanelView::showParameterOnLcd(const juce::String& name, const juce::String& value) {
    midiView = false;
    pendingName = name;
    pendingValue = value;
    pendingCountdown = 30;                                 // about two seconds at 15 Hz
}

// ----------------------------------------------------------------------------------- the display
// The icons along the bottom line: a bar stack for a level, a pointer in a circle for pan, a cone for
// a send, a corner for the filter and a block for the insertion switch, each centred on where its
// silkscreen label is printed.
// Every icon is drawn inside the nine dot rows from top + 2 to top + 10, which is what the selection
// frame closes around.
static void drawLevel(LcdView& l, int cx, int top, int level) {
    const int bars = 1 + level * 4 / 127;
    for (int i = 0; i < bars; ++i) l.fill(cx - 3, top + 10 - i * 2, 7, 1);
}

static void drawPan(LcdView& l, int cx, int top, int level) {
    static const int8_t arc[5][2] = {{1, 2}, {3, 3}, {4, 5}, {4, 7}, {3, 9}};
    for (auto& c : arc) {
        l.dot(cx + c[0], top + c[1]);
        l.dot(cx - c[0], top + c[1]);
    }
    l.fill(cx - 2, top + 10, 5, 1);
    const float a = juce::degreesToRadians(-70.0f + 140.0f * juce::jlimit(0.0f, 1.0f, (level - 1) / 126.0f));
    for (int i = 1; i <= 3; ++i)
        l.dot(cx + (int)std::lround(std::sin(a) * i), top + 6 - (int)std::lround(std::cos(a) * i));
}

static void drawSend(LcdView& l, int cx, int top, int level) {
    const int rings = level * 4 / 127;
    for (int i = 0; i < rings; ++i) l.fill(cx - 3 + i, top + 4 + i * 2, 7 - 2 * i, 1);
}

void PanelView::refreshLcd() {
    auto& dv = proc.device();
    const int part = proc.selectedPart();
    lcd.clear();

    if (pendingCountdown > 0 || midiView) {
        if (pendingCountdown > 0) --pendingCountdown;
        lcd.text(3, LcdView::kLine1, pendingName.substring(0, 19));
        lcd.text(3, LcdView::kLine2, pendingValue.substring(0, 19));
    } else {
        // The play screen: the performance across the top, the selected part's voice below, and the
        // field the cursor is on named on the right, where the hardware names its parameter.
        lcd.text(3, LcdView::kLine1, juce::String(dv.performanceName()).trimEnd().substring(0, 12));
        lcd.textRight(117, LcdView::kLine1, juce::String(kFields[field].label).substring(0, 7));
        lcd.text(3, LcdView::kLine2,
                 ("P" + juce::String(part + 1) + " " +
                  juce::String(dv.voiceName(part)).trimEnd()).substring(0, 12));
        lcd.textRight(117, LcdView::kLine2,
                      dv.fseqFrames() ? juce::String("FSEQ")
                                      : "ALG " + juce::String(dv.algorithm(part) + 1));
    }

    // The icon strip. Everything but PART is a part parameter, read back through the parameter list.
    for (int f = 0; f < 10; ++f) {
        const int cx = (int)std::lround(kFields[f].x);
        const int top = LcdView::kFieldTop;
        int value = 0;
        juce::String txt;
        if (f == 0) {
            txt = juce::String(part + 1);
        } else {
            const int idx = describeIndex(proc, kFields[f].group, kFields[f].param);
            if (idx >= 0) {
                auto* q = proc.parameterFor(idx);
                value = (int)std::lround(q->convertFrom0to1(q->getValue()));
                const auto& d = proc.descriptions()[(size_t)idx];
                if (f == 1) txt = d.textFor(value).substring(0, 3);
                else if (f == 2) txt = juce::String(value + 1).paddedLeft('0', 3);
                else if (f == 9) txt = d.textFor(value).upToFirstOccurrenceOf(" ", false, false);
            }
        }
        int width = 9;                                     // what the selection frame closes around
        if (txt.isNotEmpty()) {
            lcd.centred(cx, top + 3, txt);
            width = txt.length() * 6 + 1;
        } else if (f == 3) drawLevel(lcd, cx, top, value);
        else if (f == 4) {
            if (value) {                                   // the filter switch, drawn as a cutoff corner
                lcd.fill(cx - 3, top + 3, 4, 1);
                lcd.fill(cx + 1, top + 3, 1, 7);
            }
        } else if (f == 5) { drawPan(lcd, cx, top, value); width = 11; }
        else if (f == 6 || f == 7) drawSend(lcd, cx, top, value);
        else if (f == 8) { if (value) lcd.fill(cx - 2, top + 4, 5, 5); }

        // "A small triangular pointer appears above the icon corresponding to the selected
        // parameter in the bottom line of the display" - owner's manual page 22.
        if (f == field) {
            for (int i = 0; i < 3; ++i) lcd.fill(cx - 2 + i, LcdView::kPointerRow + i, 5 - 2 * i, 1);
            lcd.frame(cx - width / 2 - 1, top + 1, width + 2, LcdView::kFieldBottom - top);
        }
    }
    lcd.commit();
}

void PanelView::timerCallback() { refreshLcd(); }

}  // namespace fs1rplug
