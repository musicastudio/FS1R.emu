#include "EditorPages.h"
#include "../PluginProcessor.h"
#include "fs1r/firmware/algorithms.h"

namespace fs1rplug {

// Reads a parameter's current value out of the processor by group and name. The pages need a handful
// of engine values to draw with; going through the parameters keeps them on the same side of the wall
// as everything else, so a drawn view never reads engine internals either.
static int valueOf(Processor& p, const juce::String& group, const juce::String& name, int fallback = 0) {
    const auto& d = p.descriptions();
    for (size_t i = 0; i < d.size(); ++i)
        if (d[i].group == group && d[i].name == name) {
            auto* q = p.parameterFor((int)i);
            return (int)std::lround(q->convertFrom0to1(q->getValue()));
        }
    return fallback;
}

// ------------------------------------------------------------------------------------------ algorithm
AlgorithmView::AlgorithmView(Processor& p) : proc(p) {}

void AlgorithmView::paint(juce::Graphics& g) {
    auto r = getLocalBounds().reduced(6);
    g.setColour(juce::Colour(0xff1b1e21));
    g.fillRoundedRectangle(r.toFloat(), 3.0f);
    int a = juce::jlimit(0, 87, alg);
    const unsigned char* t = FS1R_ALG[a];

    g.setColour(juce::Colour(0xff8f959b));
    g.setFont(11.0f);
    g.drawText("algorithm " + juce::String(a + 1), r.removeFromTop(16).reduced(6, 0),
               juce::Justification::centredLeft);

    // Operators run left to right in the order the chip evaluates them. Each box says where its
    // modulation comes from; a box that reaches the mix is drawn filled.
    const int n = 8;
    int bw = juce::jmax(34, (r.getWidth() - 20) / n);
    int bh = juce::jmin(46, r.getHeight() - 26);
    int y = r.getY() + 8;
    juce::Rectangle<int> box[8];
    for (int o = 0; o < n; ++o) box[o] = {r.getX() + 10 + o * bw, y, bw - 10, bh};

    auto centreOf = [&](int o) { return box[o].getCentre().toFloat(); };
    static const char* srcName[8] = {"-", "-", "-", "fb", "chain", "held", "sum", "-"};

    for (int o = 0; o < n; ++o) {
        int t0 = t[2 * o], t1 = t[2 * o + 1];
        int src = (t0 >> 3) & 7;
        bool carrier = (t1 & 1) != 0;
        auto b = box[o].toFloat();
        g.setColour(carrier ? juce::Colour(0xff2f4f6b) : juce::Colour(0xff262a2e));
        g.fillRoundedRectangle(b, 3.0f);
        g.setColour(carrier ? juce::Colour(0xff7fb4de) : juce::Colour(0xff4a4f54));
        g.drawRoundedRectangle(b.reduced(0.5f), 3.0f, 1.0f);
        g.setColour(juce::Colour(0xffd6dade));
        g.setFont(12.0f);
        g.drawText(juce::String(o + 1), box[o].removeFromTop(bh / 2), juce::Justification::centred);
        g.setFont(8.5f);
        juce::String tag = srcName[src];
        if (t0 & 4) tag += " >fb";
        if (t0 & 2) tag += " >held";
        if (t1 & 4) tag += " >sum";
        g.setColour(juce::Colour(0xff9aa0a6));
        g.drawText(tag, box[o], juce::Justification::centred);

        // the chain link: this operator modulates the next one that reads "chain"
        if (o + 1 < n && (((t[2 * (o + 1)] >> 3) & 7) == 4)) {
            g.setColour(juce::Colour(0xff7fb4de));
            auto p1 = centreOf(o), p2 = centreOf(o + 1);
            g.drawLine(p1.x + bw / 2 - 5, p1.y, p2.x - bw / 2 + 5, p2.y, 1.4f);
        }
        if (t0 & 4) {                                  // writes the feedback bus
            g.setColour(juce::Colour(0xffe8b84b));
            auto b2 = box[o].toFloat();
            g.drawLine(b2.getCentreX(), b2.getY() - 6, b2.getCentreX(), b2.getY() - 2, 1.2f);
            g.fillEllipse(b2.getCentreX() - 2, b2.getY() - 8, 4, 4);
        }
    }
    g.setColour(juce::Colour(0xff6f757b));
    g.setFont(8.5f);
    g.drawText("filled = carrier   fb = feedback bus   chain = previous operator   held / sum = buses",
               getLocalBounds().removeFromBottom(14).reduced(10, 0), juce::Justification::centredLeft);
}

// ------------------------------------------------------------------------------------------ spectrum
SpectrumView::SpectrumView(Processor& p) : proc(p) {}

void SpectrumView::paint(juce::Graphics& g) {
    auto r = getLocalBounds().reduced(6);
    g.setColour(juce::Colour(0xff1b1e21));
    g.fillRoundedRectangle(r.toFloat(), 3.0f);
    juce::String grp = "Op " + juce::String(which + 1);
    int form = valueOf(proc, grp, "Spectral Form", 0);
    int skirt = valueOf(proc, grp, "Spectral Skirt", 0);
    int bw = valueOf(proc, grp, "oscillator freq. ratio of band spectrum", 0);

    g.setColour(juce::Colour(0xff8f959b));
    g.setFont(11.0f);
    static const char* forms[8] = {"sine", "all 1", "all 2", "odd 1", "odd 2", "res 1", "res 2", "frmt"};
    g.drawText(grp + "  form " + juce::String(forms[juce::jlimit(0, 7, form)]) +
                   "   skirt " + juce::String(skirt) + "   bandwidth " + juce::String(bw),
               r.removeFromTop(16).reduced(6, 0), juce::Justification::centredLeft);

    // One period of the window the engine builds: sin^(2 * (skirt + 1)) over the window length, with
    // the carrier inside it for the formant forms. This is the model in src/fs1r_lib.cpp, drawn.
    auto plot = r.reduced(8, 6).toFloat();
    g.setColour(juce::Colour(0xff2a2e32));
    g.drawLine(plot.getX(), plot.getCentreY(), plot.getRight(), plot.getCentreY(), 1.0f);
    juce::Path p;
    const int N = 240;
    double power = 2.0 * (skirt + 1);
    double carrier = form == 7 ? 1.0 + bw * 0.12 : form >= 5 ? 1.0 + bw * 31.0 / 99.0 : 0.0;
    for (int i = 0; i <= N; ++i) {
        double x = i / (double)N;
        double win = std::pow(std::sin(juce::MathConstants<double>::pi * x), power);
        double y = form == 0 ? std::sin(2 * juce::MathConstants<double>::pi * x)
                             : carrier > 0 ? win * std::sin(2 * juce::MathConstants<double>::pi * carrier * x)
                                           : win * ((form >= 3 && i > N / 2) ? -1.0 : 1.0);
        float px = plot.getX() + (float)x * plot.getWidth();
        float py = plot.getCentreY() - (float)y * plot.getHeight() * 0.45f;
        if (i == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
    }
    g.setColour(juce::Colour(0xff7fb4de));
    g.strokePath(p, juce::PathStrokeType(1.4f));
}

// ------------------------------------------------------------------------------------------ envelopes
EnvelopeView::EnvelopeView(Processor& p, Kind k) : proc(p), kind(k) {}

void EnvelopeView::paint(juce::Graphics& g) {
    auto r = getLocalBounds().reduced(6);
    g.setColour(juce::Colour(0xff1b1e21));
    g.fillRoundedRectangle(r.toFloat(), 3.0f);

    int L[4], T[4];
    juce::String title;
    if (kind == Amplitude) {
        juce::String grp = "Op " + juce::String(which + 1);
        title = grp + " amplitude EG";
        for (int i = 0; i < 4; ++i) {
            L[i] = valueOf(proc, grp, "EG - level" + juce::String(i + 1), 99);
            T[i] = valueOf(proc, grp, "EG - time" + juce::String(i + 1), 50);
        }
    } else if (kind == Pitch) {
        title = "pitch EG";
        static const char* ln[4] = {"Pitch EG - level 1", "Pitch EG - level 2", "Pitch EG - level 3",
                                    "Pitch EG - level 4"};
        for (int i = 0; i < 4; ++i) {
            L[i] = valueOf(proc, "Voice", ln[i], 50);
            T[i] = valueOf(proc, "Voice", "Pitch EG - time " + juce::String(i + 1), 50);
        }
    } else {
        title = "filter EG";
        static const char* ln[4] = {"Filter EG - level1", "Filter EG - level2", "Filter EG - level3",
                                    "Filter EG - level4"};
        for (int i = 0; i < 4; ++i) {
            L[i] = valueOf(proc, "Voice", ln[i], 50);
            T[i] = valueOf(proc, "Voice", "Filter EG - time" + juce::String(i + 1), 50);
        }
    }
    g.setColour(juce::Colour(0xff8f959b));
    g.setFont(11.0f);
    g.drawText(title + "   L4 is the start level, key off runs back to it",
               r.removeFromTop(16).reduced(6, 0), juce::Justification::centredLeft);

    auto plot = r.reduced(8, 6).toFloat();
    int span = 0;
    for (int i = 0; i < 4; ++i) span += (99 - juce::jlimit(0, 99, T[i])) + 10;
    if (span <= 0) span = 1;
    float x = plot.getX();
    auto lvl = [&](int v) {
        float f = kind == Amplitude ? juce::jlimit(0, 99, v) / 99.0f : juce::jlimit(0, 100, v) / 100.0f;
        return plot.getBottom() - f * plot.getHeight();
    };
    juce::Path p;
    p.startNewSubPath(x, lvl(L[3]));                       // starts at L4
    for (int i = 0; i < 3; ++i) {                          // L1, L2, L3
        float w = plot.getWidth() * (((99 - juce::jlimit(0, 99, T[i])) + 10) / (float)span);
        x += w;
        p.lineTo(x, lvl(L[i]));
    }
    float hold = plot.getWidth() * 0.12f;
    x += hold;
    p.lineTo(x, lvl(L[2]));                                // L3 held until key off
    float w4 = plot.getWidth() * (((99 - juce::jlimit(0, 99, T[3])) + 10) / (float)span);
    p.lineTo(juce::jmin(plot.getRight(), x + w4), lvl(L[3]));
    g.setColour(juce::Colour(0xff8ad6a0));
    g.strokePath(p, juce::PathStrokeType(1.6f));
    g.setColour(juce::Colour(0xff4a4f54));
    g.drawVerticalLine((int)x, plot.getY(), plot.getBottom());
    g.setColour(juce::Colour(0xff6f757b));
    g.setFont(8.5f);
    g.drawText("key off", juce::Rectangle<float>(x + 2, plot.getY(), 50, 12).toNearestInt(),
               juce::Justification::centredLeft);
}

// ------------------------------------------------------------------------------------------ Fseq
FseqView::FseqView(Processor& p) : proc(p) { startTimerHz(12); }
FseqView::~FseqView() { stopTimer(); }

void FseqView::timerCallback() {
    int s = proc.device().fseqPosition(), f = proc.device().fseqFrames();
    if (s != lastStep || f != lastFrames) { lastStep = s; lastFrames = f; repaint(); }
}

void FseqView::paint(juce::Graphics& g) {
    auto r = getLocalBounds().reduced(6);
    g.setColour(juce::Colour(0xff1b1e21));
    g.fillRoundedRectangle(r.toFloat(), 3.0f);
    auto& dv = proc.device();
    int frames = dv.fseqFrames();
    g.setColour(juce::Colour(0xff8f959b));
    g.setFont(11.0f);
    if (frames <= 0) {
        g.drawText("no Fseq loaded", r, juce::Justification::centred);
        return;
    }
    g.drawText(juce::String("fseq \"") + juce::String(dv.fseqName()).trim() + "\"  " +
                   juce::String(frames) + " frames   part " +
                   (dv.fseqPart() < 0 ? juce::String("off") : juce::String(dv.fseqPart() + 1)),
               r.removeFromTop(16).reduced(6, 0), juce::Justification::centredLeft);

    // Eight voiced tracks: formant frequency as the vertical position, level as the brightness.
    auto plot = r.reduced(8, 6);
    uint8_t f[50];
    const int cols = juce::jmin(frames, juce::jmax(1, plot.getWidth()));
    for (int c = 0; c < cols; ++c) {
        int step = (int)((double)c / cols * frames);
        if (!dv.fseqFrame(step, f)) continue;
        float x = plot.getX() + (float)c * plot.getWidth() / cols;
        for (int tr = 0; tr < 8; ++tr) {
            int freq = f[2 + tr] * 256 + f[0x0A + tr] * 2;     // hi, lo, as the engine reads them
            int lvl = f[0x12 + tr];
            float y = plot.getBottom() - juce::jlimit(0.0f, 1.0f, freq / 30000.0f) * plot.getHeight();
            g.setColour(juce::Colour(0xff7fb4de).withAlpha(juce::jlimit(0.05f, 1.0f, lvl / 127.0f)));
            g.fillRect(x, y, juce::jmax(1.0f, (float)plot.getWidth() / cols), 2.0f);
        }
    }
    int pos = dv.fseqPosition();
    g.setColour(juce::Colour(0xffe8b84b));
    g.drawVerticalLine(plot.getX() + (int)((double)pos / juce::jmax(1, frames) * plot.getWidth()),
                       (float)plot.getY(), (float)plot.getBottom());
}

// ------------------------------------------------------------------------------------------ page
class ParameterPage::Cell : public juce::Component, private juce::Slider::Listener {
public:
    Cell(Processor& p, int index) : proc(p), idx(index) {
        const auto& d = p.descriptions()[(size_t)index];
        name.setText(d.name, juce::dontSendNotification);
        name.setFont(10.0f);
        name.setTooltip(d.group + " " + d.name + (d.description.isEmpty() ? "" : "  -  " + d.description));
        addAndMakeVisible(name);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange(d.min, d.max, 1.0);
        slider.addListener(this);
        addAndMakeVisible(slider);
        value.setFont(10.0f);
        value.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(value);
        refresh();
    }
    void refresh() {
        auto* p = proc.parameterFor(idx);
        const auto& d = proc.descriptions()[(size_t)idx];
        int v = (int)std::lround(p->convertFrom0to1(p->getValue()));
        if (v != last) { last = v; slider.setValue(v, juce::dontSendNotification); }
        auto t = d.textFor(v);
        int cc = proc.mappedCcFor(idx);
        if (cc >= 0) t += " [CC" + juce::String(cc) + "]";
        if (t != shown) { shown = t; value.setText(t, juce::dontSendNotification); }
    }
    void resized() override {
        auto r = getLocalBounds();
        name.setBounds(r.removeFromLeft(juce::jmax(90, r.getWidth() * 45 / 100)));
        value.setBounds(r.removeFromRight(juce::jmax(56, r.getWidth() / 3)));
        slider.setBounds(r.reduced(2, 1));
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        m.addItem(1, "MIDI learn");
        m.addItem(2, "Clear MIDI mapping", proc.mappedCcFor(idx) >= 0);
        m.addItem(3, "Reset to default");
        m.showMenuAsync(juce::PopupMenu::Options(), [this](int res) {
            if (res == 1) proc.startMidiLearn(idx);
            else if (res == 2) proc.clearMidiMapping(idx);
            else if (res == 3) { auto* p = proc.parameterFor(idx); p->setValueNotifyingHost(p->getDefaultValue()); }
        });
    }
    std::function<void(const juce::String&, const juce::String&)> onTouch;

private:
    void sliderValueChanged(juce::Slider* s) override {
        auto* p = proc.parameterFor(idx);
        p->setValueNotifyingHost(p->convertTo0to1((float)s->getValue()));
        if (onTouch) onTouch(proc.descriptions()[(size_t)idx].name,
                             proc.descriptions()[(size_t)idx].textFor((int)s->getValue()));
    }
    Processor& proc;
    const int idx;
    juce::Label name, value;
    juce::Slider slider;
    int last = -99999;
    juce::String shown;
};

ParameterPage::ParameterPage(Processor& p,
                             std::function<bool(const juce::String&, const juce::String&)> f,
                             juce::Component* top, int th, int cols)
    : proc(p), accept(std::move(f)), topView(top), topHeight(th), columns(juce::jmax(1, cols)) {
    if (topView) addAndMakeVisible(topView);
    viewport.setViewedComponent(&holder, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    rebuild();
    startTimerHz(15);
}

ParameterPage::~ParameterPage() { stopTimer(); }

void ParameterPage::setSearch(const juce::String& s) {
    if (s == search) return;
    search = s;
    rebuild();
    resized();
}

void ParameterPage::rebuild() {
    cells.clear();
    const auto& d = proc.descriptions();
    auto needle = search.trim().toLowerCase();
    for (size_t i = 0; i < d.size(); ++i) {
        if (!accept(d[i].group, d[i].name)) continue;
        if (needle.isNotEmpty() && !d[i].name.toLowerCase().contains(needle) &&
            !d[i].group.toLowerCase().contains(needle)) continue;
        auto* c = new Cell(proc, (int)i);
        c->onTouch = [this](const juce::String& n, const juce::String& v) {
            if (onParameterTouched) onParameterTouched(n, v);
        };
        holder.addAndMakeVisible(c);
        cells.add(c);
    }
}

void ParameterPage::timerCallback() { for (auto* c : cells) c->refresh(); }

void ParameterPage::resized() {
    auto r = getLocalBounds();
    if (topView && topHeight > 0) topView->setBounds(r.removeFromTop(topHeight));
    viewport.setBounds(r);
    int w = juce::jmax(200, viewport.getWidth() - 16);
    int cw = w / columns;
    int rows = (cells.size() + columns - 1) / columns;
    const int rh = 20;
    for (int i = 0; i < cells.size(); ++i) {
        int col = i / juce::jmax(1, rows), row = i % juce::jmax(1, rows);
        cells[i]->setBounds(col * cw, row * rh, cw - 6, rh);
    }
    holder.setSize(w, juce::jmax(1, rows * rh));
}

}  // namespace fs1rplug
