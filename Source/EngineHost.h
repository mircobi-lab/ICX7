#pragma once
#include "Dx7Voice.h"
#include <juce_audio_basics/juce_audio_basics.h>

struct Icx7Engine;

namespace icx7
{
/*  Contenitore del motore Rust: lo fa girare sempre a 48 kHz (le tabelle del motore esistono solo
    per 44.1/48 kHz e sono globali) e ricampiona alla frequenza dell'host con un sinc a finestra.
    Tutti i metodi vanno chiamati dal thread audio.                                                  */
class EngineHost
{
public:
    static constexpr int polyphony = 16;

    EngineHost();
    ~EngineHost();

    void prepare (double hostRate, int maxBlock);
    void loadVoice (const VoiceData&);
    void handleMidi (const juce::MidiMessage&);
    void allNotesOff();
    void setBendRange (int semitones);
    void render (float* out, int numSamples);          // mono, alla frequenza dell'host

    int activeKeys() const noexcept { return keysDown; }

private:
    Icx7Engine* engine = nullptr;
    double ratio = 1.0;                                // campioni del motore per campione dell'host
    bool passthrough = true;
    juce::WindowedSincInterpolator interp;
    std::vector<float> fifo;
    int fifoCount = 0;
    int keysDown = 0;
    bool held[128] {};

    void renderChunk (float* out, int n);

    JUCE_DECLARE_NON_COPYABLE (EngineHost)
};
}
