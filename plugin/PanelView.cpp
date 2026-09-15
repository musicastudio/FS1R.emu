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

// The hardware puts the volume knob hard against the left edge of the panel; here it is moved right
// to sit midway between that edge and the display, which is easier on the eye than the spacing the
// drawing has. buildArt() moves the drawn knob and its label by the same amount, so the drawing and
// the live control stay together. This is the one place the panel is deliberately not the hardware.
constexpr float volumeShiftX = 8.03f;
constexpr float volumeRegion[4] = {111.5f, 215.5f, 19.5f, 23.5f};   // knob, tick marks and the label
constexpr juce::uint32 faceColour = 0xff95b5b7;                     // the drawing's own panel colour

// The single white arc each cap carries, by the id it has in docs/FS1R-front-panel-p14-ny.svg. It is
// the drawing's way of saying the cap is a cylinder lit from the upper left, so it must not turn with
// the cap; liftKnobGloss() takes each one out of the drawing and hands it to its knob to paint.
constexpr const char* knobGloss[4] = {"path10126", "path10502", "path10878", "path11254"};
constexpr const char* volumeGloss = "path9822";

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

void LcdView::invert(int x, int y, int w, int h) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            if ((unsigned)(x + i) < (unsigned)kCols && (unsigned)(y + j) < (unsigned)kRows)
                fb[y + j][x + i] ^= 1;
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

void PanelKnob::setGloss(const juce::Path& p, juce::Colour c, float strokeWidth,
                         juce::Rectangle<float> capArea) {
    gloss = p;
    glossColour = c;
    glossStroke = strokeWidth;
    glossArea = capArea;
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

    if (gloss.isEmpty() || glossArea.isEmpty()) return;
    const float sx = r.getWidth() / glossArea.getWidth(), sy = r.getHeight() / glossArea.getHeight();
    auto gt = juce::AffineTransform::translation(-glossArea.getX(), -glossArea.getY())
                  .scaled(sx, sy)
                  .translated(r.getX(), r.getY());
    g.setColour(glossColour);
    if (glossStroke > 0.0f) g.strokePath(gloss, juce::PathStrokeType(glossStroke * sx), gt);
    else g.fillPath(gloss, gt);
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
// The PART ASSIGN cursor stops, in the EPROM's own order. The screen templates at 0x39328C are a run
// of 40-byte records, one per stop, each holding the two display lines; their names and their order
// are what this table is, and the owner's manual describes the same twelve in the same order on pages
// 25 and 26. docs/interface_from_firmware.md has the dump.
//
// Two of them share one place on the icon strip, the way the hardware prints them: Rcv Ch with Rcv
// Max, and Bank with Pgm#. Dry Lvl has no icon at all - the silkscreen has ten labels for twelve
// stops - so it is named on the top line and draws nothing on the strip.
//
// x is the field's centre in display dots, or kNoIcon. midi is the message the hardware's MIDI View
// shows for the stop (EPROM 0x3934BC), blank where it shows none.
enum FieldId { fRcvCh, fRcvMax, fBank, fPgm, fVolume, fPan, fRev, fVar, fIns, fDry, fFilter, fNote,
               kNumFields };
constexpr float kNoIcon = -1.0f;

struct Field {
    const char* label;
    float x;
    const char* group;
    const char* param;
    const char* midi;
};
const Field kFields[kNumFields] = {
    {"Rcv Ch",   24.6f,   "Part", "Rcv CHANNEL A1~A16, pfm, off",   ""},
    {"Rcv Max",  24.6f,   "Part", "Rcv CHANNEL MAX A1~A16, off",    ""},
    {"Bank",     44.2f,   "Part", "BANK NUMBER off, Int, PrA~PrK",  "Bn 20"},
    {"Pgm#",     44.2f,   "Part", "PROGRAM NUMBER",                 "Cn"},
    {"Volume",   58.4f,   "Part", "VOLUME",                         "Bn 07"},
    {"Pan",      75.6f,   "Part", "PAN Rnd,",                       "Bn 0A"},
    {"RevSend",  84.7f,   "Part", "REVERB SEND",                    "Bn 5B"},
    {"VarSend",  93.2f,   "Part", "VARIATION SEND",                 "Bn 5D"},
    {"InsEfSw",  101.9f,  "Part", "INSERTION SW",                   ""},
    {"Dry Lvl",  kNoIcon, "Part", "DRY LEVEL",                      ""},
    {"Filter",   67.1f,   "Part", "FILTER CUTOFF FREQ",             "Bn 4A"},
    {"NoteSft",  111.2f,  "Part", "NOTE SHIFT",                     ""},
};
constexpr float kPartIconX = 8.5f;      // the PART icon: the part number, never a cursor stop

// The PLAY screen's own stops, EPROM 0x392FEC, the same eight the manual lists on page 22. With PART =
// ALL the strip belongs to the performance, not to a part: there is no FLT or INS icon, and BANK/PGM#
// carries the performance's category rather than a number. Bank and Pgm# have no parameter of their
// own - they are the performance selection, so VALUE steps the performance itself.
enum PlayFieldId { pPfmCh, pBank, pPgm, pVol, pPan, pRev, pVar, pNote, kNumPlayFields };
const Field kPlayFields[kNumPlayFields] = {
    {"Pfm Ch",  24.6f,  "System",      "performance channel",    ""},
    {"Bank",    44.2f,  nullptr,       nullptr,                  "Bn 20"},
    {"Pgm#",    44.2f,  nullptr,       nullptr,                  "Cn"},
    {"Pfm Vol", 58.4f,  "Performance", "performance volume",     "Bn 07"},
    {"Pfm Pan", 75.6f,  "Performance", "performance pan",        "Bn 0A"},
    {"Rev Rtn", 84.7f,  "Effects",     "Reverb Return",          ""},
    {"Var Rtn", 93.2f,  "Effects",     "Variation Return",       ""},
    {"PfmNSft", 111.2f, "Performance", "performance note shift", ""},
};

const Field* fieldTable(bool all) { return all ? kPlayFields : kFields; }
int fieldCount(bool all) { return all ? kNumPlayFields : kNumFields; }

// "Please note that the Rcv Max parameter is only available for parts 1 and 2" - owner's manual page
// 25, so the cursor steps over it on parts 3 and 4.
bool fieldAvailable(int f, int part, bool all) { return all || f != fRcvMax || part < 2; }

int stepFieldIndex(int from, int by, int part, bool all) {
    const int n = fieldCount(all);
    for (int i = 0; i < n; ++i) {
        from = (from + by + n) % n;
        if (fieldAvailable(from, part, all)) break;
    }
    return from;
}

// Finds a parameter by group and name. Everything the display shows goes through the parameter list,
// so the panel never reaches into the engine either.
int describeIndex(Processor& p, const char* group, const char* name) {
    if (group == nullptr) return -1;
    const auto& d = p.descriptions();
    for (size_t i = 0; i < d.size(); ++i)
        if (d[i].group == group && d[i].name == name) return (int)i;
    return -1;
}

// The part's BANK NUMBER as the hardware prints it. The generated parameter table carries the range
// but not the names, so they come from the same list the patch browser offers.
juce::String bankName(int v) {
    static const juce::StringArray banks = PatchManager::voiceBanks();
    return banks[juce::jlimit(0, banks.size() - 1, v)];
}

// Walks the drawing for the element that carried this id in the SVG; JUCE's parser keeps it as the
// drawable's name.
juce::Drawable* findDrawable(juce::Drawable& d, const char* id) {
    if (d.getName() == id) return &d;
    if (auto* c = dynamic_cast<juce::DrawableComposite*>(&d))
        for (int i = 0; i < c->getNumChildren(); ++i)
            if (auto* f = findDrawable(c->getChild(i), id)) return f;
    return nullptr;
}

// Takes one gloss arc out of the drawing: its path and colour come back, and it is blanked where it
// stood so the cap rendered from the drawing no longer carries it.
bool takeGloss(juce::Drawable& art, const char* id, juce::Path& path, juce::Colour& colour, float& stroke) {
    auto* p = dynamic_cast<juce::DrawablePath*>(findDrawable(art, id));
    if (p == nullptr) return false;
    stroke = p->getStrokeType().getStrokeThickness();
    const bool stroked = stroke > 0.0f && !p->getStrokeFill().colour.isTransparent();
    colour = stroked ? p->getStrokeFill().colour : p->getFill().colour;
    if (colour.isTransparent()) return false;
    if (!stroked) stroke = 0.0f;
    path = p->getPath();
    p->replaceColour(colour, juce::Colours::transparentBlack);
    return true;
}
}  // namespace

// ------------------------------------------------------------------------------------------ panel
PanelView::PanelView(Processor& p) : proc(p) {
    art = juce::Drawable::createFromImageData(BinaryData::fs1r_panel_svg,
                                              (size_t)BinaryData::fs1r_panel_svgSize);
    jassert(art != nullptr);
    addAndMakeVisible(lcd);

    // The four knobs edit the selected part, so they are the part's own parameters rather than raw
    // sysex: the pages, the host and the panel then all read and move the same value.
    static const char* toneParams[4] = {"EG ATTACK TIME", "EG RELEASE TIME", "FORMANT", "FM"};
    for (int i = 0; i < 4; ++i) toneParam[i] = describeIndex(proc, "Part", toneParams[i]);

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
    // "The PART ASSIGN mode can be selected from the PERFORMANCE PLAY mode by pressing either the PART
    // button" (page 24), and [EXIT] goes back (page 22), so the two buttons walk ALL, 01, 02, 03, 04.
    partDown->onClick = [this] {
        if (allParts) setAllParts(false);
        else if (proc.selectedPart() == 0) setAllParts(true);
        else proc.selectPart(proc.selectedPart() - 1);
    };
    partUp->onClick = [this] {
        if (allParts) setAllParts(false);
        else proc.selectPart(juce::jmin(3, proc.selectedPart() + 1));
    };
    cursorL->onClick = [this] { field = stepFieldIndex(field, -1, proc.selectedPart(), allParts); };
    cursorR->onClick = [this] { field = stepFieldIndex(field, +1, proc.selectedPart(), allParts); };
    valueDown->onClick = [this] { stepField(-1); };
    valueUp->onClick = [this] { stepField(1); };
    exitBtn->onClick = [this] {
        midiView = false;
        setAllParts(true);
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
    liftKnobGloss();
    setKnobMode(Tone);

    startTimerHz(15);
}

PanelView::~PanelView() { stopTimer(); }

// The drawing's knob caps each carry one white arc across their upper left. It says the cap is a
// cylinder lit from that side, so it belongs to the panel rather than to the cap: each one is taken
// out of the drawing and handed to its knob, which paints it over the turned cap without turning it.
void PanelView::liftKnobGloss() {
    if (art == nullptr) return;
    juce::Path p;
    juce::Colour c;
    float stroke = 0.0f;
    for (int i = 0; i < 4; ++i)
        if (takeGloss(*art, art::knobGloss[i], p, c, stroke))
            knobs[i].setGloss(p, c, stroke,
                              {art::knobX[i] - art::knobR, art::knobY - art::knobR,
                               2 * art::knobR, 2 * art::knobR});
    if (takeGloss(*art, art::volumeGloss, p, c, stroke))
        volume.setGloss(p, c, stroke,
                        {art::volumeX - art::volumeR, art::volumeY - art::volumeR,
                         2 * art::volumeR, 2 * art::volumeR});
}

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
        // Move the drawn volume knob, its tick marks and its label across to where the live control
        // sits: paint the face colour over where they were, then draw the whole panel again shifted
        // and clipped to where they are going.
        const auto& v = art::volumeRegion;
        g.setColour(juce::Colour(art::faceColour));
        g.fillRect(place(v[0], v[1], v[2], v[3]));
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(place(v[0] + art::volumeShiftX, v[1], v[2], v[3]));
        art->draw(g, 1.0f, juce::AffineTransform::translation(art::volumeShiftX, 0.0f).followedBy(fit));
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
    volume.setBounds(placeCircle(art::volumeX + art::volumeShiftX, art::volumeY, art::volumeR));
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
    refreshKnobs();
}

// In the tone mode the caps show the selected part's own values, so loading a patch or switching part
// moves them, the way the hardware's knobs are read against the part they edit.
void PanelView::refreshKnobs() {
    if (knobMode != Tone) return;
    for (int i = 0; i < 4; ++i)
        if (toneParam[i] >= 0 && !knobs[i].isMouseButtonDown())
            knobs[i].setValue(proc.parameterValue(toneParam[i]), false);
}

void PanelView::knobMoved(int knob, int v, int delta) {
    static const char* toneNames[4] = {"Attack Time", "Release Time", "Formant", "FM"};
    switch (knobMode) {
        case Tone: {
            // Straight into the selected part, which is what the lit upper button means.
            const int idx = toneParam[knob];
            if (idx < 0) break;
            auto* q = proc.parameterFor(idx);
            q->setValueNotifyingHost(q->convertTo0to1((float)v));
            showParameterOnLcd(toneNames[knob], proc.descriptions()[(size_t)idx].textFor(v));
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
            else if (knob == 2) field = stepFieldIndex(field, delta > 0 ? 1 : -1, proc.selectedPart(), allParts);
            else if (knob == 3) stepField(delta);
            break;
    }
}

void PanelView::setAllParts(bool all) {
    if (all == allParts) return;
    allParts = all;
    // The PLAY screen opens on the program number, which is what VALUE steps there; the PART ASSIGN
    // one opens on its first stop.
    field = all ? pPgm : fRcvCh;
}

void PanelView::stepField(int by) {
    const Field& f = fieldTable(allParts)[field];
    // With PART = ALL the bank and program pair is the performance itself: VALUE walks the 384 of them,
    // a whole bank of 128 at a time on the bank half. This is the one the demos use, and the reason
    // VALUE changes patches with nothing else selected.
    if (allParts && (field == pBank || field == pPgm)) {
        const int n = PatchManager::kNumPerformances;
        const int now = juce::jmax(0, proc.currentPerformance());
        const int step = (field == pBank ? 128 : 1) * (by > 0 ? 1 : -1);
        proc.selectPerformance(juce::jlimit(0, n - 1, now + step));
        return;
    }
    const int idx = describeIndex(proc, f.group, f.param);
    if (idx < 0) return;
    const auto& d = proc.descriptions()[(size_t)idx];
    auto* q = proc.parameterFor(idx);
    const int v = juce::jlimit(d.min, d.max,
                               (int)std::lround(q->convertFrom0to1(q->getValue())) + (by > 0 ? 1 : -1));
    q->setValueNotifyingHost(q->convertTo0to1((float)v));
    showParameterOnLcd(f.label, (!allParts && field == fBank) ? bankName(v) : d.textFor(v));
}

// The MIDI View, [ENTER] twice: the channel message that sets the selected parameter, exactly as the
// hardware's own MIDI View screens spell it (EPROM 0x3934BC, " MIDI     Bank    =" / "Bn 20     = ").
// Seven of the twelve stops have one; the rest show nothing, which is what those screens are too.
void PanelView::showMidiView() {
    const Field& f = fieldTable(allParts)[field];
    const juce::String msg(f.midi);
    int v = 0;
    if (allParts && (field == pBank || field == pPgm)) {
        v = juce::jmax(0, proc.currentPerformance()) % 128 + 1;
    } else {
        const int idx = describeIndex(proc, f.group, f.param);
        if (idx < 0) return;
        auto* q = proc.parameterFor(idx);
        v = (int)std::lround(q->convertFrom0to1(q->getValue())) + (field == fPgm && !allParts ? 1 : 0);
    }
    pendingName = "MIDI  " + juce::String(f.label);
    pendingValue = msg.isEmpty() ? juce::String("-") : msg + " = " + juce::String(v);
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
    lcd.clear();
    if (pendingCountdown > 0 || midiView) {
        if (pendingCountdown > 0) --pendingCountdown;
        lcd.text(3, LcdView::kLine1, pendingName.substring(0, 19));
        lcd.text(3, LcdView::kLine2, pendingValue.substring(0, 19));
    } else if (allParts) {
        refreshPlayScreen();
    } else {
        refreshPartScreen();
    }

    // ------------------------------------------------------------------------------ the icon strip
    const Field* table = fieldTable(allParts);
    const int n = fieldCount(allParts);
    const int top = LcdView::kFieldTop;
    const int part = proc.selectedPart();
    // The PART icon: "ALL" on the play screen, the part number otherwise. It is never a cursor stop -
    // the part moves with the PART buttons.
    lcd.centred((int)std::lround(kPartIconX), top + 3, allParts ? juce::String("ALL")
                                                                : juce::String(part + 1));

    for (int f = 0; f < n; ++f) {
        // The pairs that share one place on the strip: only the half the cursor is on is drawn, and
        // the first of the pair is what shows when the cursor is elsewhere.
        if (!allParts) {
            if (f == fRcvMax && field != fRcvMax) continue;
            if (f == fRcvCh && field == fRcvMax) continue;
            if (f == fBank && field != fBank) continue;
            if (f == fPgm && field == fBank) continue;
        } else if (f == pPgm) {
            continue;                       // BANK/PGM# shows the category for both halves
        }
        if (table[f].x == kNoIcon) continue;                // Dry Lvl is named on the top line only

        const int cx = (int)std::lround(table[f].x);
        int value = 0;
        juce::String txt;
        const int idx = describeIndex(proc, table[f].group, table[f].param);
        if (idx >= 0) {
            auto* q = proc.parameterFor(idx);
            value = (int)std::lround(q->convertFrom0to1(q->getValue()));
            const auto& d = proc.descriptions()[(size_t)idx];
            if (!allParts && (f == fRcvCh || f == fRcvMax)) txt = d.textFor(value).substring(0, 3);
            else if (!allParts && f == fBank) txt = bankName(value);
            else if (!allParts && f == fPgm) txt = juce::String(value + 1).paddedLeft('0', 3);
            else if (allParts && f == pPfmCh) txt = juce::String(value + 1).paddedLeft('0', 2);
            else if ((allParts && f == pNote) || (!allParts && f == fNote))
                txt = d.textFor(value).upToFirstOccurrenceOf(" ", false, false);
        }
        // With PART = ALL the bank and program place carries the performance's category, which is what
        // the hardware shows there: "En", "Br", "Vo".
        if (allParts && f == pBank) {
            static const juce::StringArray cats = PatchManager::categories();
            const int cat = describeIndex(proc, "Performance", "CATEGORY");
            txt = cats[cat < 0 ? 0 : juce::jlimit(0, cats.size() - 1, proc.parameterValue(cat))];
        }

        int width = 9;
        if (txt.isNotEmpty()) {
            lcd.centred(cx, top + 3, txt);
            width = txt.length() * 6 + 1;
        } else if ((allParts && f == pVol) || (!allParts && f == fVolume)) drawLevel(lcd, cx, top, value);
        else if (!allParts && f == fFilter) {
            // The cutoff offset, drawn as a corner whose knee moves with it. This stop is the part's
            // FILTER CUTOFF FREQ, which is what MIDI View calls "Bn 4A" - not the filter switch.
            const int knee = cx - 3 + value * 5 / 127;
            lcd.fill(cx - 3, top + 3, knee - (cx - 3) + 1, 1);
            lcd.fill(knee, top + 3, 1, 7);
        } else if ((allParts && f == pPan) || (!allParts && f == fPan)) {
            drawPan(lcd, cx, top, value);
            width = 11;
        } else if ((allParts && (f == pRev || f == pVar)) || (!allParts && (f == fRev || f == fVar)))
            drawSend(lcd, cx, top, value);
        else if (!allParts && f == fIns) { if (value) lcd.fill(cx - 2, top + 4, 5, 5); }

        // The selected stop is drawn in reverse video. The owner's manual calls it "a small triangular
        // pointer above the icon" (pages 22 and 25), but a v1.20 unit shows a filled box with the
        // glyphs knocked out of it and no pointer at all - see docs/interface_from_firmware.md.
        const bool selected = f == field || (allParts && f == pBank && field == pPgm);
        if (selected) lcd.invert(cx - width / 2 - 1, top + 1, width + 2, LcdView::kFieldBottom - top);
    }
    lcd.commit();
}

// PART = ALL. "C056 Hit" across the top and "Perform<PrC<056" along the bottom right, the pair carrying
// the solid pointer against whichever half the cursor is on and the hollow one against the other.
void PanelView::refreshPlayScreen() {
    const int pf = proc.currentPerformance();
    const auto name = juce::String(proc.device().performanceName()).trimEnd();
    // The stop's name shares the top line with the performance's, so the performance gives way when
    // one is showing. On the bank and program pair the hardware shows no name there at all.
    const bool onPair = field == pBank || field == pPgm;
    const int room = onPair ? 19 : 12;
    if (pf >= 0) {
        const auto& e = proc.patches().performances()[(size_t)pf];
        lcd.text(3, LcdView::kLine1, (e.code + " " + e.name).substring(0, room));
        // The solid pointer sits against the half the cursor is on, the hollow one against the other
        // (owner's manual page 22). With the cursor elsewhere the program number keeps the solid one,
        // which is where a unit sits at power-up.
        const juce::String solid = juce::String::charToString(1), hollow = juce::String::charToString(2);
        lcd.textRight(117, LcdView::kLine2,
                      "Perform" + (field == pBank ? solid : hollow) + "Pr" + e.code.substring(0, 1) +
                          (field == pBank ? hollow : solid) + e.code.substring(1));
    } else {
        lcd.text(3, LcdView::kLine1, name.substring(0, room));
        lcd.textRight(117, LcdView::kLine2, "Perform");
    }
    if (!onPair)
        lcd.textRight(117, LcdView::kLine1, juce::String(kPlayFields[field].label).substring(0, 7));
}

// PART = 01..04. The performance still names the top line, the part's own voice the second.
void PanelView::refreshPartScreen() {
    auto& dv = proc.device();
    const int part = proc.selectedPart();
    const int pf = proc.currentPerformance();
    const auto name = juce::String(dv.performanceName()).trimEnd();
    lcd.text(3, LcdView::kLine1,
             pf >= 0 ? (proc.patches().performances()[(size_t)pf].code + " " + name).substring(0, 12)
                     : name.substring(0, 12));
    lcd.textRight(117, LcdView::kLine1, juce::String(kFields[field].label).substring(0, 7));
    lcd.text(3, LcdView::kLine2,
             ("P" + juce::String(part + 1) + " " +
              juce::String(dv.voiceName(part)).trimEnd()).substring(0, 12));
    lcd.textRight(117, LcdView::kLine2,
                  dv.fseqFrames() ? juce::String("FSEQ") : "ALG " + juce::String(dv.algorithm(part) + 1));
}

void PanelView::timerCallback() {
    refreshKnobs();
    refreshLcd();
}

}  // namespace fs1rplug
