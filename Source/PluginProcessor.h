#pragma once
#include "EngineHost.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "MBStudioMidi.h"

// Coro stereo semplice (due linee di ritardo modulate in quadratura), stile ensemble anni '80.
class StereoChorus
{
public:
    void prepare (double sr);
    void process (float* l, float* r, int n, float mix);
    void reset() { std::fill (buf.begin(), buf.end(), 0.f); }
private:
    std::vector<float> buf;
    int pos = 0;
    double phase = 0, inc = 0, sampleRate = 48000;
};

class ICX7Processor : public juce::AudioProcessor,
                      private juce::AsyncUpdater,
                      private juce::AudioProcessorValueTreeState::Listener
{
public:
    ICX7Processor();
    ~ICX7Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 32; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- banchi e voce in modifica (thread dei messaggi)
    static constexpr int userBankIndex = -1;
    int numBanks() const { return (int) icx7::factoryBanks().size(); }
    int currentBankIndex() const { return currentBank; }
    juce::String bankName (int index) const;
    juce::String currentBankName() const { return bankName (currentBank); }
    bool hasUserBank() const { return userBankValid; }
    void selectBank (int index);                       // carica anche il programma corrente
    bool importSyx (const juce::File&, juce::String& message);
    bool exportBank (const juce::File&, juce::String& message);
    bool exportVoice (const juce::File&, juce::String& message);
    void storeToCurrentSlot();                         // scrive la voce in modifica nel banco USER
    void initVoice();

    icx7::VoiceData editVoice() const;                 // voce costruita dai parametri attuali
    juce::String editName() const { return voiceNameStr; }
    void setEditName (const juce::String&);
    bool isEdited() const { return edited; }

    int paramInt (const juce::String& id) const;
    void setParamInt (const juce::String& id, int v);

    // MIDI PANIC: chiamabile da qualsiasi thread, eseguito all'inizio del blocco audio successivo
    void requestPanic() noexcept { panicRequested = true; }
    int panicCount() const noexcept { return panicsDone.load(); }

    float outputLevel() const noexcept { return outLevel.load(); }
    int keysDown() const noexcept { return keys.load(); }

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;
    mbstudio::midi::MidiMapper midiMap;               // MIDI Learn e profili controller
    std::function<void()> onPatchChanged;              // per l'editor (thread dei messaggi)

private:
    icx7::EngineHost engine;
    StereoChorus chorus;
    juce::Reverb reverb;
    juce::SmoothedValue<float> gain;
    std::vector<float> mono;

    std::array<std::atomic<float>*, icx7::voiceBytes> byteParam {};
    std::array<std::atomic<float>*, 6> opOn {};
    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pChorus = nullptr;
    std::atomic<float>* pReverb = nullptr;
    std::atomic<float>* pBend = nullptr;
    std::atomic<bool> voiceDirty { true };
    int lastBend = -1;

    int currentBank = 0, currentProgram = 10;          // ROM1A 11: E.PIANO 1
    std::array<icx7::VoiceData, 32> userBank {};
    bool userBankValid = false;
    juce::String voiceNameStr { "E.PIANO 1" };
    bool edited = false, loadingVoice = false;

    std::atomic<int> pendingProgram { -1 };
    std::atomic<bool> sysexPending { false };
    std::array<uint8_t, 4200> sysexBuf {};
    std::atomic<int> sysexSize { 0 };

    std::atomic<bool> panicRequested { false };
    std::atomic<int> panicsDone { 0 };
    std::atomic<float> outLevel { 0 };
    std::atomic<int> keys { 0 };

    const icx7::VoiceData& bankVoice (int program) const;
    void applyVoiceToParams (const icx7::VoiceData&);
    icx7::VoiceData voiceFromAtomics() const;
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void notifyEditor();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ICX7Processor)
};
