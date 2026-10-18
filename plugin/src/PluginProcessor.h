/* Selva1 · the plugin: the engine in a DAW. Parameters come from the engine's own list (s1_info), so every
 * knob of the panel is automatable under the same key the web panel and the presets use.
 * Copyright (C) 2026 DeAbrew. GPL-3.0-or-later. */
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <vector>

class Selva1Processor : public juce::AudioProcessor
{
public:
    Selva1Processor();
    ~Selva1Processor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Selva1"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return presetName; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    /* the engine's description (shared by every instance, parsed once; the shared tables built with it) */
    struct Param { juce::String key, name; int kind = 0; float min = 0, max = 1, def = 0; juce::StringArray opts; };
    struct Info
    {
        std::vector<Param> params;
        int nParams = 0, nAll = 0, seqBase = 0, lanes = 0, steps = 0;
        std::vector<float> laneDefaults;
        juce::StringArray sourceNames, destNames;
    };
    static const Info& info();
    const Info& inf;

    juce::AudioProcessorValueTreeState apvts;

    /* from the panel (message thread) to the audio thread */
    enum MsgType { mSet, mNoteOn, mNoteOff, mCC, mBend, mAfter, mPanic, mArm };
    struct Msg { int type; int a; float b; };
    void post(Msg m);

    /* the panel's view, written by the audio thread, read by the editor's timer */
    std::array<std::atomic<float>, 16> uiState {};
    std::atomic<bool> hostPlaying { false };
    std::atomic<double> hostBpm { 0 };

    /* message-thread state that is not a parameter: the sequencer's steps, the sound's name, the SEQ button */
    juce::CriticalSection stateLock;
    std::vector<float> steps;
    juce::String presetName { "INIT" };
    bool seqArmed = false;
    std::atomic<int> stateVersion { 0 };   /* bumped when the host loads a state: the panel reloads */

    /* values for the panel: every id (parameters, then steps) */
    juce::var allValues();
    void setValueFromUi(int id, float v);
    void gesture(int id, bool begin);
    void setArmed(bool on);

    /* the panel's own files (user sounds, favourites): a small key-value store next to the plugin's settings */
    juce::var loadStore();
    void saveStoreKey(const juce::String& key, const juce::String& value);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout(const Info&);
    void handle(const Msg& m);
    void renderSlice(float* l, float* r, int n);

    juce::HeapBlock<char> ctxMem;
    void* ctx = nullptr;
    std::vector<std::atomic<float>*> raw;
    std::vector<float> applied;
    juce::AbstractFifo fifo { 4096 };
    std::vector<Msg> fifoBuf = std::vector<Msg>(4096);
    juce::SpinLock postLock;

    bool armedAudio = false, freeRun = false, wasPlaying = false;
    double expectPpq = -1;
    float tmp[16] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Selva1Processor)
};
