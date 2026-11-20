#include "Dx7Voice.h"
#include "ICX7Banks.h"
#include "icx7_engine.h"
#include <cmath>

namespace icx7
{
namespace
{
struct FieldInfo { const char* key; const char* label; const char* longName; int maxV; Fmt fmt; };

const FieldInfo opFields[numOpFields] = {
    { "r1", "R1", "EG Rate 1", 99, Fmt::number },
    { "r2", "R2", "EG Rate 2", 99, Fmt::number },
    { "r3", "R3", "EG Rate 3", 99, Fmt::number },
    { "r4", "R4", "EG Rate 4", 99, Fmt::number },
    { "l1", "L1", "EG Level 1", 99, Fmt::number },
    { "l2", "L2", "EG Level 2", 99, Fmt::number },
    { "l3", "L3", "EG Level 3", 99, Fmt::number },
    { "l4", "L4", "EG Level 4", 99, Fmt::number },
    { "bp", "BRK PT", "Level Scaling Break Point", 99, Fmt::breakpoint },
    { "ld", "L DEPTH", "Level Scaling Left Depth", 99, Fmt::number },
    { "rd", "R DEPTH", "Level Scaling Right Depth", 99, Fmt::number },
    { "lc", "L CURVE", "Level Scaling Left Curve", 3, Fmt::curve },
    { "rc", "R CURVE", "Level Scaling Right Curve", 3, Fmt::curve },
    { "rs", "RATE SC", "Rate Scaling", 7, Fmt::number },
    { "ams", "AMS", "Amp Mod Sensitivity", 3, Fmt::number },
    { "kvs", "VEL", "Key Velocity Sensitivity", 7, Fmt::number },
    { "ol", "LEVEL", "Output Level", 99, Fmt::number },
    { "mode", "MODE", "Oscillator Mode", 1, Fmt::mode },
    { "fc", "COARSE", "Frequency Coarse", 31, Fmt::number },
    { "ff", "FINE", "Frequency Fine", 99, Fmt::number },
    { "det", "DETUNE", "Detune", 14, Fmt::detune },
};

// tabella degli algoritmi dell'MSFA (fm_core.cc), indice 0 = OP6 ... 5 = OP1
const int busFlags[32][6] = {
    { 0xc1, 0x11, 0x11, 0x14, 0x01, 0x14 }, { 0x01, 0x11, 0x11, 0x14, 0xc1, 0x14 },
    { 0xc1, 0x11, 0x14, 0x01, 0x11, 0x14 }, { 0xc1, 0x11, 0x94, 0x01, 0x11, 0x14 },
    { 0xc1, 0x14, 0x01, 0x14, 0x01, 0x14 }, { 0xc1, 0x94, 0x01, 0x14, 0x01, 0x14 },
    { 0xc1, 0x11, 0x05, 0x14, 0x01, 0x14 }, { 0x01, 0x11, 0xc5, 0x14, 0x01, 0x14 },
    { 0x01, 0x11, 0x05, 0x14, 0xc1, 0x14 }, { 0x01, 0x05, 0x14, 0xc1, 0x11, 0x14 },
    { 0xc1, 0x05, 0x14, 0x01, 0x11, 0x14 }, { 0x01, 0x05, 0x05, 0x14, 0xc1, 0x14 },
    { 0xc1, 0x05, 0x05, 0x14, 0x01, 0x14 }, { 0xc1, 0x05, 0x11, 0x14, 0x01, 0x14 },
    { 0x01, 0x05, 0x11, 0x14, 0xc1, 0x14 }, { 0xc1, 0x11, 0x02, 0x25, 0x05, 0x14 },
    { 0x01, 0x11, 0x02, 0x25, 0xc5, 0x14 }, { 0x01, 0x11, 0x11, 0xc5, 0x05, 0x14 },
    { 0xc1, 0x14, 0x14, 0x01, 0x11, 0x14 }, { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x14 },
    { 0x01, 0x14, 0x14, 0xc1, 0x14, 0x14 }, { 0xc1, 0x14, 0x14, 0x14, 0x01, 0x14 },
    { 0xc1, 0x14, 0x14, 0x01, 0x14, 0x04 }, { 0xc1, 0x14, 0x14, 0x14, 0x04, 0x04 },
    { 0xc1, 0x14, 0x14, 0x04, 0x04, 0x04 }, { 0xc1, 0x05, 0x14, 0x01, 0x14, 0x04 },
    { 0x01, 0x05, 0x14, 0xc1, 0x14, 0x04 }, { 0x04, 0xc1, 0x11, 0x14, 0x01, 0x14 },
    { 0xc1, 0x14, 0x01, 0x14, 0x04, 0x04 }, { 0x04, 0xc1, 0x11, 0x14, 0x04, 0x04 },
    { 0xc1, 0x14, 0x04, 0x04, 0x04, 0x04 }, { 0xc4, 0x04, 0x04, 0x04, 0x04, 0x04 },
};

std::vector<ParamDef> buildParams()
{
    std::vector<ParamDef> v;
    for (int op = 1; op <= 6; ++op)
        for (int f = 0; f < numOpFields; ++f)
        {
            const auto& fi = opFields[f];
            int def = 0;
            switch (f)
            {
                case R1: case R2: case R3: case R4: case L1: case L2: case L3: def = 99; break;
                case BP: def = 39; break;
                case OL: def = op == 1 ? 99 : 0; break;
                case FC: def = 1; break;
                case DET: def = 7; break;
                default: break;
            }
            v.push_back ({ opParamId (op, f), fi.label, "OP" + juce::String (op) + " " + fi.longName,
                           opByte (op, f), 0, fi.maxV, def, fi.fmt });
        }
    auto add = [&] (const char* id, const char* label, const char* longName, int byte, int maxV, int def, Fmt fmt)
    { v.push_back ({ id, label, longName, byte, 0, maxV, def, fmt }); };
    const char* pr[4] = { "pr1", "pr2", "pr3", "pr4" };
    const char* pl[4] = { "pl1", "pl2", "pl3", "pl4" };
    static const char* prLabel[4] = { "R1", "R2", "R3", "R4" };
    static const char* plLabel[4] = { "L1", "L2", "L3", "L4" };
    static const char* prLong[4] = { "Pitch EG Rate 1", "Pitch EG Rate 2", "Pitch EG Rate 3", "Pitch EG Rate 4" };
    static const char* plLong[4] = { "Pitch EG Level 1", "Pitch EG Level 2", "Pitch EG Level 3", "Pitch EG Level 4" };
    for (int i = 0; i < 4; ++i) add (pr[i], prLabel[i], prLong[i], 126 + i, 99, 99, Fmt::number);
    for (int i = 0; i < 4; ++i) add (pl[i], plLabel[i], plLong[i], 130 + i, 99, 50, Fmt::number);
    add ("alg", "ALGORITHM", "Algorithm", 134, 31, 0, Fmt::algorithm);
    add ("fb", "FEEDBACK", "Feedback", 135, 7, 0, Fmt::number);
    add ("oks", "KEY SYNC", "Oscillator Key Sync", 136, 1, 1, Fmt::onOff);
    add ("lfs", "SPEED", "LFO Speed", 137, 99, 35, Fmt::number);
    add ("lfd", "DELAY", "LFO Delay", 138, 99, 0, Fmt::number);
    add ("lpmd", "PITCH MD", "LFO Pitch Mod Depth", 139, 99, 0, Fmt::number);
    add ("lamd", "AMP MD", "LFO Amp Mod Depth", 140, 99, 0, Fmt::number);
    add ("lfks", "LFO SYNC", "LFO Key Sync", 141, 1, 1, Fmt::onOff);
    add ("lfw", "WAVE", "LFO Waveform", 142, 5, 0, Fmt::wave);
    add ("pms", "PITCH MS", "Pitch Mod Sensitivity", 143, 7, 3, Fmt::number);
    add ("trnsp", "TRANSPOSE", "Transpose", 144, 48, 24, Fmt::transpose);
    return v;
}

juce::String noteName (int midi)   // convenzione Yamaha: DO centrale = C3
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (n[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 2);
}

Algorithm buildAlgorithm (int a)
{
    Algorithm r;
    uint8_t bus[3] = { 0, 0, 0 };        // operatori che hanno scritto su ciascun bus
    for (int i = 0; i < 6; ++i)
    {
        const int flags = busFlags[a][i];
        const int op = 6 - i;
        const int inBus = (flags >> 4) & 3, outBus = flags & 3;
        const bool add = (flags & 0x04) != 0;
        if (inBus > 0) r.modulators[(size_t) op] |= bus[inBus];
        if (outBus == 0) r.carriers |= (uint8_t) (1 << op);
        else bus[outBus] = (uint8_t) ((add ? bus[outBus] : 0) | (1 << op));
        if (flags & 0x40) r.fbTo = op;
        if (flags & 0x80) r.fbFrom = op;
    }
    return r;
}
} // namespace

juce::String opParamId (int op, int field) { return "op" + juce::String (op) + "_" + opFields[field].key; }
const char* opFieldKey (int field) { return opFields[field].key; }

const std::vector<ParamDef>& voiceParams()
{
    static const std::vector<ParamDef> p = buildParams();
    return p;
}

const ParamDef* findParam (const juce::String& id)
{
    for (auto& d : voiceParams())
        if (d.id == id) return &d;
    return nullptr;
}

juce::String formatValue (Fmt f, int v)
{
    switch (f)
    {
        case Fmt::curve:     { static const char* c[4] = { "-LIN", "-EXP", "+EXP", "+LIN" }; return c[juce::jlimit (0, 3, v)]; }
        case Fmt::mode:      return v ? "FIXED" : "RATIO";
        case Fmt::onOff:     return v ? "ON" : "OFF";
        case Fmt::wave:      { static const char* w[6] = { "TRIANGLE", "SAW DOWN", "SAW UP", "SQUARE", "SINE", "S/HOLD" }; return w[juce::jlimit (0, 5, v)]; }
        case Fmt::detune:    return v == 7 ? "0" : (v > 7 ? "+" : "") + juce::String (v - 7);
        case Fmt::transpose: return noteName (36 + v);
        case Fmt::algorithm: return juce::String (v + 1);
        case Fmt::breakpoint: return noteName (21 + v);
        case Fmt::number:    break;
    }
    return juce::String (v);
}

juce::String formatValue (const juce::String& id, int v)
{
    if (auto* d = findParam (id)) return formatValue (d->fmt, v);
    return juce::String (v);
}

VoiceData initVoice()
{
    VoiceData v {};
    for (auto& d : voiceParams()) v[(size_t) d.byte] = (uint8_t) d.defV;
    setVoiceName (v, "INIT VOICE");
    return v;
}

juce::String voiceName (const VoiceData& v)
{
    juce::String s;
    for (int i = 145; i < 155; ++i)
    {
        const auto c = (char) v[(size_t) i];
        s += (c >= 32 && c < 127) ? c : ' ';
    }
    return s.trimEnd();
}

void setVoiceName (VoiceData& v, const juce::String& name)
{
    const auto ascii = name.toUpperCase();
    for (int i = 0; i < 10; ++i)
    {
        const auto c = i < ascii.length() ? ascii[i] : (juce::juce_wchar) ' ';
        v[(size_t) (145 + i)] = (uint8_t) ((c >= 32 && c < 127) ? c : ' ');
    }
}

void sanitize (VoiceData& v) { icx7_sanitize_voice (v.data()); }

juce::String frequencyText (int mode, int coarse, int fine, int detune)
{
    juce::ignoreUnused (detune);
    if (mode == 0)
    {
        const double ratio = (coarse == 0 ? 0.5 : (double) coarse) * (1.0 + fine / 100.0);
        return "X " + juce::String (ratio, 2);
    }
    const double hz = std::pow (10.0, coarse & 3) * std::pow (10.0, fine / 100.0);
    return juce::String (hz, hz < 10 ? 3 : hz < 100 ? 2 : hz < 1000 ? 1 : 0) + " HZ";
}

const Algorithm& algorithm (int index)
{
    static const auto table = []
    {
        std::array<Algorithm, 32> t {};
        for (int a = 0; a < 32; ++a) t[(size_t) a] = buildAlgorithm (a);
        return t;
    }();
    return table[(size_t) juce::jlimit (0, 31, index)];
}

int parseSysex (const void* data, size_t size, std::array<VoiceData, 32>& out)
{
    std::vector<uint8_t> tmp (32 * voiceBytes);
    const auto n = (int) icx7_parse_sysex (static_cast<const uint8_t*> (data), size, tmp.data());
    for (int i = 0; i < n; ++i)
        std::memcpy (out[(size_t) i].data(), tmp.data() + i * voiceBytes, voiceBytes);
    return n;
}

juce::MemoryBlock buildBulkDump (const std::array<VoiceData, 32>& voices)
{
    std::vector<uint8_t> in (32 * voiceBytes);
    for (int i = 0; i < 32; ++i) std::memcpy (in.data() + i * voiceBytes, voices[(size_t) i].data(), voiceBytes);
    juce::MemoryBlock mb (4104);
    const auto n = icx7_build_bulk_dump (in.data(), static_cast<uint8_t*> (mb.getData()));
    mb.setSize (n);
    return mb;
}

juce::MemoryBlock buildSingleVoiceDump (const VoiceData& v)
{
    juce::MemoryBlock mb;
    const uint8_t head[6] = { 0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B };
    mb.append (head, 6);
    mb.append (v.data(), voiceBytes);
    uint8_t sum = 0;
    for (auto b : v) sum = (uint8_t) (sum + b);
    const uint8_t tail[2] = { (uint8_t) ((uint8_t) (~sum + 1) & 0x7F), 0xF7 };
    mb.append (tail, 2);
    return mb;
}

const std::vector<Bank>& factoryBanks()
{
    static const std::vector<Bank> banks = []
    {
        struct Src { const char* name; const char* data; int size; };
        const Src src[] = {
            { "ROM1A", ICX7Banks::rom1a_syx, ICX7Banks::rom1a_syxSize }, { "ROM1B", ICX7Banks::rom1b_syx, ICX7Banks::rom1b_syxSize },
            { "ROM2A", ICX7Banks::rom2a_syx, ICX7Banks::rom2a_syxSize }, { "ROM2B", ICX7Banks::rom2b_syx, ICX7Banks::rom2b_syxSize },
            { "ROM3A", ICX7Banks::rom3a_syx, ICX7Banks::rom3a_syxSize }, { "ROM3B", ICX7Banks::rom3b_syx, ICX7Banks::rom3b_syxSize },
            { "ROM4A", ICX7Banks::rom4a_syx, ICX7Banks::rom4a_syxSize }, { "ROM4B", ICX7Banks::rom4b_syx, ICX7Banks::rom4b_syxSize },
        };
        std::vector<Bank> b;
        for (auto& s : src)
        {
            Bank bank;
            bank.name = s.name;
            for (auto& v : bank.voices) v = initVoice();
            if (parseSysex (s.data, (size_t) s.size, bank.voices) == 32)
                b.push_back (std::move (bank));
        }
        return b;
    }();
    return banks;
}
}
