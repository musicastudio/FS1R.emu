// EditorPages - the expanded editor pages that sit under the panel.
//
// The panel alone cannot reach 900 parameters comfortably, so the hardware is reproduced above and
// these pages carry the rest, switched with tabs. Each page is a filter over the generated parameter
// descriptions plus, where a picture says more than a number, one drawn view: the algorithm diagram
// for all 88 algorithms, the formant window for the current bandwidth and skirt, the envelopes, the
// filter response and the Fseq frame data.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace fs1rplug {

class Processor;

// The 88 algorithms drawn from the connection table in src/fs1r_algorithms.h: eight operator boxes,
// the chain, held and sum buses between them, the feedback loop and which operators reach the output.
class AlgorithmView : public juce::Component {
public:
    explicit AlgorithmView(Processor&);
    void paint(juce::Graphics&) override;
    void setAlgorithm(int a) { if (a != alg) { alg = a; repaint(); } }
    int algorithm() const { return alg; }

private:
    Processor& proc;
    int alg = 0;
};

// The spectral form of one voiced operator: the window shape for its skirt, the band spectrum for its
// bandwidth, and where the formant sits. Drawn from the same model the engine uses.
class SpectrumView : public juce::Component {
public:
    explicit SpectrumView(Processor&);
    void paint(juce::Graphics&) override;
    void setOperator(int op) { if (op != which) { which = op; repaint(); } }

private:
    Processor& proc;
    int which = 0;
};

// Amplitude, pitch and filter envelopes in the FS1R's own ordering: start at L4, run L1, L2, L3, hold,
// key off runs to L4.
class EnvelopeView : public juce::Component {
public:
    enum Kind { Amplitude, Pitch, Filter };
    EnvelopeView(Processor&, Kind);
    void paint(juce::Graphics&) override;
    void setOperator(int op) { if (op != which) { which = op; repaint(); } }

private:
    Processor& proc;
    Kind kind;
    int which = 0;
};

// The loaded Fseq: eight voiced tracks of formant frequency and level over the whole sequence, with
// the playback position marked.
class FseqView : public juce::Component, private juce::Timer {
public:
    explicit FseqView(Processor&);
    ~FseqView() override;
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    Processor& proc;
    int lastStep = -1, lastFrames = -1;
};

// A page: an optional drawn view across the top, then every parameter the filter accepts, laid out in
// labelled columns.
class ParameterPage : public juce::Component, private juce::Timer {
public:
    ParameterPage(Processor&, std::function<bool(const juce::String& group, const juce::String& name)>,
                  juce::Component* top = nullptr, int topHeight = 0, int columns = 3);
    ~ParameterPage() override;
    void resized() override;
    void setSearch(const juce::String& s);
    std::function<void(const juce::String&, const juce::String&)> onParameterTouched;

private:
    class Cell;
    void timerCallback() override;
    void rebuild();

    Processor& proc;
    std::function<bool(const juce::String&, const juce::String&)> accept;
    juce::Component* topView;
    int topHeight, columns;
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<Cell> cells;
    juce::String search;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterPage)
};

}  // namespace fs1rplug
