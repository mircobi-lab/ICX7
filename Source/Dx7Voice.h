#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

/*  Dati di voce in formato DX7 "unpacked" (VCED, 155 byte):
      byte  0..125  6 operatori x 21 byte, OP6 per primo
      byte 126..133 pitch EG (R1..R4, L1..L4)
      134 algoritmo (0..31)   135 feedback   136 osc key sync
      137..142 LFO (speed, delay, PMD, AMD, sync, onda)   143 pitch mod sens   144 transpose
      145..154 nome (10 caratteri ASCII)                                                         */
namespace icx7
{
constexpr int voiceBytes = 155;
using VoiceData = std::array<uint8_t, voiceBytes>;

enum class Fmt { number, curve, mode, onOff, wave, detune, transpose, algorithm, breakpoint };

struct ParamDef
{
    juce::String id;          // "op1_r1", "alg", ...
    juce::String label;       // etichetta sul pannello
    juce::String longName;    // nome completo per l'host
    int byte;                 // posizione nel blocco di 155 byte
    int minV, maxV, defV;
    Fmt fmt;
};

// campi dell'operatore (offset dentro i 21 byte)
enum OpField { R1, R2, R3, R4, L1, L2, L3, L4, BP, LD, RD, LC, RC, RS, AMS, KVS, OL, MODE, FC, FF, DET, numOpFields };

constexpr int opByte (int op /* 1..6 */, int field) { return (6 - op) * 21 + field; }
juce::String opParamId (int op, int field);
const char* opFieldKey (int field);

const std::vector<ParamDef>& voiceParams();       // 145 parametri numerici (tutti tranne il nome)
const ParamDef* findParam (const juce::String& id);
juce::String formatValue (Fmt, int value);
juce::String formatValue (const juce::String& paramId, int value);

VoiceData initVoice();
juce::String voiceName (const VoiceData&);
void setVoiceName (VoiceData&, const juce::String&);
void sanitize (VoiceData&);

// frequenza dell'operatore per il display ("X 1.00" o "100.0 HZ")
juce::String frequencyText (int mode, int coarse, int fine, int detune);

// ---- algoritmi: grafo ricavato dalla tabella a bus dell'MSFA (la stessa del motore)
struct Algorithm
{
    std::array<uint8_t, 7> modulators {};    // [op] = maschera dei modulatori (bit n = OPn), op 1..6
    uint8_t carriers = 0;                    // bit n = OPn è un portante
    int fbFrom = 0, fbTo = 0;                // anello di feedback (OP), fbFrom == fbTo se su sé stesso
};
const Algorithm& algorithm (int index /* 0..31 */);

// ---- banchi
struct Bank
{
    juce::String name;
    std::array<VoiceData, 32> voices;
};
const std::vector<Bank>& factoryBanks();       // ROM1A .. ROM4B
int parseSysex (const void* data, size_t size, std::array<VoiceData, 32>& out);   // 32, 1 o 0
juce::MemoryBlock buildBulkDump (const std::array<VoiceData, 32>&);
juce::MemoryBlock buildSingleVoiceDump (const VoiceData&);
}
