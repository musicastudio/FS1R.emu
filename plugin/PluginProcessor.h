// PluginProcessor - a controller for fs1r::Device, nothing more.
//
// Every host-automatable parameter is a sysex parameter change into the engine; the engine echoes it
// back and that echo is what moves the controls. Host state is the engine's own bulk dump, so a session
// reload restores exactly what the hardware would have held.
//
// The part, voice and operator parameters address the selected part (0-3), the way the front panel
// edits one part at a time. Switching parts re-reads the engine instead of sending anything.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>
#include "fs1r.h"
#include "patch/ParameterDescriptions.h"
#include "patch/PatchManager.h"

namespace fs1rplug {

class Processor : public juce::AudioProcessor,
                  private juce::AudioProcessorParameter::Listener,
                  private juce::Timer {
public:
    Processor();
    ~Processor() override;

    void prepareToPlay(double sampleRate, int maxBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "FSVR"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }   // the longest reverb time
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- what the editor talks to
    fs1r::Device& device() { return dev; }
    PatchManager& patches() { return patchManager; }
    const std::vector<ParamDesc>& descriptions() const { return descs; }
    juce::RangedAudioParameter* parameterFor(int index) const { return params[(size_t)index]; }

    int selectedPart() const { return part.load(); }
    void selectPart(int p);                        // 0-3; re-reads the engine for the new part

    // Voice selection the hardware's way: the part's BANK NUMBER and PROGRAM NUMBER are the
    // selection, and moving either loads the voice they name (owner's manual page 25). Everything
    // that picks a voice - the panel, the browser, host automation - goes through those two.
    int currentVoice() const;                      // the voice number the part's bank and program name
    void selectVoice(int voiceIndex);
    bool importSyx(const juce::File&);             // a .syx file's voices, as the browsable Usr bank
    void selectPerformance(int index);             // and the Fseq it names
    // Which bundled performance is loaded, or -1 when the engine is holding something else. The
    // display names a performance by its bank and program number, so it has to know.
    int currentPerformance() const { return perfIndex.load(); }
    int parameterValue(int index) const;

    // MIDI learn: arm, then the next control change binds to this parameter.
    void startMidiLearn(int paramIndex) { learnTarget = paramIndex; }
    void cancelMidiLearn() { learnTarget = -1; }
    bool isLearning() const { return learnTarget >= 0; }
    void clearMidiMapping(int paramIndex);
    int mappedCcFor(int paramIndex) const;

    void requestFullRefresh() { needRefresh = true; }

private:
    void parameterValueChanged(int index, float newValue) override;
    void parameterGestureChanged(int, bool) override {}
    void timerCallback() override;

    void buildParameters();
    void sendParameter(int index, int value);
    void loadSelectedVoice();
    void loadSelectedFseq();
    int findParameter(const char* group, const char* name) const;
    void applyIncomingParameter(int high, int mid, int low, int value);
    void refreshFromEngine();                      // ask the engine for the bulk dumps and adopt them

    fs1r::Device dev;
    PatchManager patchManager{dev};
    std::vector<ParamDesc> descs;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<uint8_t> shadow;                   // last byte we saw at each address, for packed fields
    std::vector<int> shadowIndex;                  // address -> shadow slot
    std::vector<std::atomic<bool>> dirty;
    int bankParam = -1, progParam = -1;            // Part / BANK NUMBER and PROGRAM NUMBER
    // Performance / FSEQ PART, bank and number: which Fseq the performance plays, if any.
    int fseqPartParam = -1, fseqBankParam = -1, fseqNumParam = -1;
    // An imported voice has no bank or program to be named by, so the one in play is remembered here
    // until a bank and program pick a preset again.
    std::atomic<int> importedVoice{-1};
    std::atomic<int> perfIndex{-1};
    std::atomic<int> part{0};
    std::atomic<int> suppressSend{0};              // nested: the audio and message threads both adopt
    std::unordered_map<int, std::vector<int>> byAddress;   // sysex address -> the descriptions at it
    std::atomic<bool> needRefresh{true};
    int learnTarget = -1;
    std::vector<int> ccForParam;                   // -1 when unmapped
    std::vector<uint8_t> stateCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

}  // namespace fs1rplug
