#include "EngineHost.h"
#include "icx7_engine.h"
#include <cmath>

namespace icx7
{
namespace { constexpr int chunk = 256; }

EngineHost::EngineHost() : engine (icx7_create ((uint32_t) polyphony))
{
    const auto v = initVoice();
    icx7_load_voice (engine, v.data());
}

EngineHost::~EngineHost() { icx7_destroy (engine); }

void EngineHost::prepare (double hostRate, int)
{
    const double engineRate = (double) icx7_engine_rate();
    ratio = engineRate / juce::jmax (8000.0, hostRate);
    passthrough = std::abs (ratio - 1.0) < 1.0e-9;
    interp.reset();
    // spazio per un blocco di "chunk" campioni dell'host più il margine dell'interpolatore
    fifo.assign ((size_t) (std::ceil (chunk * ratio) + 16), 0.f);
    fifoCount = 0;
    if (! passthrough)   // riempie la latenza dell'interpolatore con silenzio
    {
        std::vector<float> silence ((size_t) std::ceil (interp.getBaseLatency()) + 1, 0.f);
        std::vector<float> dump (silence.size());
        interp.process (1.0, silence.data(), dump.data(), (int) silence.size());
    }
}

void EngineHost::loadVoice (const VoiceData& v) { icx7_load_voice (engine, v.data()); }

void EngineHost::setBendRange (int semitones) { icx7_set_bend_range (engine, (uint8_t) juce::jlimit (0, 24, semitones)); }

void EngineHost::allNotesOff()
{
    icx7_all_notes_off (engine);
    std::fill (std::begin (held), std::end (held), false);
    keysDown = 0;
}

void EngineHost::panic()
{
    icx7_panic (engine);
    std::fill (std::begin (held), std::end (held), false);
    keysDown = 0;
    // svuota anche il buffer del ricampionatore: niente code di suono già calcolato
    std::fill (fifo.begin(), fifo.end(), 0.f);
    fifoCount = 0;
    interp.reset();
}

void EngineHost::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const int n = m.getNoteNumber();
        icx7_note_on (engine, (uint8_t) n, (uint8_t) juce::jlimit (1, 127, (int) m.getVelocity()));
        if (! held[n]) { held[n] = true; ++keysDown; }
    }
    else if (m.isNoteOff())
    {
        const int n = m.getNoteNumber();
        icx7_note_off (engine, (uint8_t) n);
        if (held[n]) { held[n] = false; --keysDown; }
    }
    else if (m.isPitchWheel())
        icx7_pitch_bend (engine, (int16_t) (m.getPitchWheelValue() - 8192));
    else if (m.isAllSoundOff())       // CC 120: taglio immediato
        panic();
    else if (m.isAllNotesOff() || m.isResetAllControllers())
    {
        if (m.isResetAllControllers()) { icx7_control_change (engine, 1, 0); icx7_pitch_bend (engine, 0); }
        allNotesOff();
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        if (cc == 2 || cc == 4)       // breath / foot: come sul DX7, aggiungono modulazione LFO
            icx7_control_change (engine, 1, (uint8_t) m.getControllerValue());
        else
            icx7_control_change (engine, (uint8_t) cc, (uint8_t) m.getControllerValue());
    }
    else if (m.isChannelPressure())   // aftertouch -> modulazione
        icx7_control_change (engine, 1, (uint8_t) m.getChannelPressureValue());
}

void EngineHost::render (float* out, int numSamples)
{
    while (numSamples > 0)
    {
        const int n = juce::jmin (numSamples, chunk);
        renderChunk (out, n);
        out += n;
        numSamples -= n;
    }
}

void EngineHost::renderChunk (float* out, int n)
{
    if (passthrough)
    {
        icx7_render (engine, out, (uint32_t) n);
        return;
    }
    const int needed = (int) std::ceil (n * ratio) + 2;
    if (fifoCount < needed)
    {
        const int add = juce::jmin (needed - fifoCount, (int) fifo.size() - fifoCount);
        icx7_render (engine, fifo.data() + fifoCount, (uint32_t) add);
        fifoCount += add;
    }
    const int used = juce::jmin (fifoCount, interp.process (ratio, fifo.data(), out, n, fifoCount, 0));
    std::memmove (fifo.data(), fifo.data() + used, (size_t) (fifoCount - used) * sizeof (float));
    fifoCount -= used;
}
}
