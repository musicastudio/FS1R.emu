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
#include <vector>
#include "fs1r_lib.h"
#include "ParameterDescriptions.h"
#include "PatchManager.h"

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
    const juce::String getName() const override { return "FS1R.emu"; }
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
    void applyIncomingParameter(int high, int mid, int low, int value);
    void refreshFromEngine();                      // ask the engine for the bulk dumps and adopt them

    fs1r::Device dev;
    PatchManager patchManager{dev};
    std::vector<ParamDesc> descs;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<uint8_t> shadow;                   // last byte we saw at each address, for packed fields
    std::vector<int> shadowIndex;                  // address -> shadow slot
    std::vector<std::atomic<bool>> dirty;
    std::atomic<int> part{0};
    std::atomic<bool> suppressSend{false};
    std::atomic<bool> needRefresh{true};
    int learnTarget = -1;
    std::vector<int> ccForParam;                   // -1 when unmapped
    std::vector<uint8_t> stateCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

}  // namespace fs1rplug
