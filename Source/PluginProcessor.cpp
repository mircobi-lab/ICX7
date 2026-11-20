#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace icx7;

// ================================================================================================ coro
void StereoChorus::prepare (double sr)
{
    sampleRate = sr;
    buf.assign ((size_t) (sr * 0.03) + 4, 0.f);
    pos = 0;
    phase = 0;
    inc = juce::MathConstants<double>::twoPi * 0.62 / sr;
}

void StereoChorus::process (float* l, float* r, int n, float mix)
{
    if (mix <= 0.0001f || buf.empty()) return;
    const int size = (int) buf.size();
    const double base = sampleRate * 0.0072, depth = sampleRate * 0.0028;
    auto tap = [&] (double delay)
    {
        double rp = pos - delay;
        while (rp < 0) rp += size;
        const int i0 = (int) rp;
        const float f = (float) (rp - i0);
        const float a = buf[(size_t) i0], b = buf[(size_t) ((i0 + 1) % size)];
        return a + (b - a) * f;
    };
    for (int i = 0; i < n; ++i)
    {
        const float in = 0.5f * (l[i] + r[i]);
        buf[(size_t) pos] = in;
        const float wl = tap (base + depth * std::sin (phase));
        const float wr = tap (base + depth * std::sin (phase + juce::MathConstants<double>::halfPi));
        phase += inc;
        if (phase > juce::MathConstants<double>::twoPi) phase -= juce::MathConstants<double>::twoPi;
        l[i] = l[i] * (1.f - 0.5f * mix) + wl * mix * 0.8f;
        r[i] = r[i] * (1.f - 0.5f * mix) + wr * mix * 0.8f;
        if (++pos >= size) pos = 0;
    }
}

// ================================================================================================ parametri
namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& d : voiceParams())
    {
        const auto fmt = d.fmt;
        const int minV = d.minV, maxV = d.maxV;
        auto attrs = juce::AudioParameterIntAttributes()
            .withStringFromValueFunction ([fmt] (int v, int) { return formatValue (fmt, v); })
            .withValueFromStringFunction ([fmt, minV, maxV] (const juce::String& s)
            {
                for (int v = minV; v <= maxV; ++v)
                    if (formatValue (fmt, v).equalsIgnoreCase (s.trim())) return v;
                return juce::jlimit (minV, maxV, s.getIntValue());
            });
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { d.id, 1 }, d.longName, d.minV, d.maxV, d.defV, attrs));
    }
    for (int op = 1; op <= 6; ++op)
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "op" + juce::String (op) + "_on", 1 },
                                                                  "OP" + juce::String (op) + " On", juce::StringArray { "OFF", "ON" }, 1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "volume", 1 }, "Volume",
                    juce::NormalisableRange<float> (-48.f, 6.f, 0.1f), 0.f,
                    juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "chorus", 1 }, "Chorus",
                    juce::NormalisableRange<float> (0.f, 100.f, 1.f), 0.f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "reverb", 1 }, "Reverb",
                    juce::NormalisableRange<float> (0.f, 100.f, 1.f), 0.f, juce::AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "bend", 1 }, "Pitch Bend Range", 0, 12, 2,
                    juce::AudioParameterIntAttributes().withLabel ("st")));
    return layout;
}
}

// ================================================================================================ processore
ICX7Processor::ICX7Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "ICX7", createLayout())
{
    for (auto& d : voiceParams())
    {
        byteParam[(size_t) d.byte] = state.getRawParameterValue (d.id);
        state.addParameterListener (d.id, this);
    }
    for (int op = 1; op <= 6; ++op)
    {
        const auto id = "op" + juce::String (op) + "_on";
        opOn[(size_t) (op - 1)] = state.getRawParameterValue (id);
        state.addParameterListener (id, this);
    }
    pVolume = state.getRawParameterValue ("volume");
    pChorus = state.getRawParameterValue ("chorus");
    pReverb = state.getRawParameterValue ("reverb");
    pBend = state.getRawParameterValue ("bend");

    for (auto& v : userBank) v = icx7::initVoice();
    setCurrentProgram (currentProgram);
}

ICX7Processor::~ICX7Processor()
{
    cancelPendingUpdate();
    for (auto& d : voiceParams()) state.removeParameterListener (d.id, this);
    for (int op = 1; op <= 6; ++op) state.removeParameterListener ("op" + juce::String (op) + "_on", this);
}

bool ICX7Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ICX7Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    engine.allNotesOff();
    chorus.prepare (sampleRate);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    gain.reset (sampleRate, 0.03);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pVolume->load(), -48.f));
    mono.assign ((size_t) juce::jmax (512, samplesPerBlock), 0.f);
    voiceDirty = true;
    lastBend = -1;
}

VoiceData ICX7Processor::voiceFromAtomics() const
{
    VoiceData v {};
    for (size_t i = 0; i < byteParam.size(); ++i)
        if (byteParam[i] != nullptr) v[i] = (uint8_t) juce::roundToInt (byteParam[i]->load());
    for (int op = 1; op <= 6; ++op)
        if (opOn[(size_t) (op - 1)]->load() < 0.5f) v[(size_t) opByte (op, OL)] = 0;
    for (int i = 145; i < 155; ++i) v[(size_t) i] = ' ';
    return v;
}

void ICX7Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    if (voiceDirty.exchange (false))
        engine.loadVoice (voiceFromAtomics());
    const int bend = juce::roundToInt (pBend->load());
    if (bend != lastBend) { engine.setBendRange (bend); lastBend = bend; }

    if ((int) mono.size() < numSamples) mono.resize ((size_t) numSamples);   // host fuori specifica
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        if (at > pos) { engine.render (mono.data() + pos, at - pos); pos = at; }
        const auto m = meta.getMessage();
        if (m.isProgramChange())
        {
            pendingProgram = m.getProgramChangeNumber() % 32;
            triggerAsyncUpdate();
        }
        else if (m.isSysEx())
        {
            const int sz = m.getSysExDataSize() + 2;
            if (sz <= (int) sysexBuf.size() && ! sysexPending.load())
            {
                std::memcpy (sysexBuf.data(), m.getRawData(), (size_t) sz);
                sysexSize = sz;
                sysexPending = true;
                triggerAsyncUpdate();
            }
        }
        else
            engine.handleMidi (m);
    }
    if (pos < numSamples) engine.render (mono.data() + pos, numSamples - pos);

    gain.setTargetValue (juce::Decibels::decibelsToGain (pVolume->load(), -48.f));
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    float peak = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float s = mono[(size_t) i] * gain.getNextValue() * 2.f;   // +6 dB: il motore lascia margine per 16 voci
        l[i] = s;
        if (r != nullptr) r[i] = s;
    }
    if (r != nullptr)
    {
        chorus.process (l, r, numSamples, pChorus->load() * 0.01f);
        const float rv = pReverb->load() * 0.01f;
        if (rv > 0.0001f)
        {
            juce::Reverb::Parameters p;
            p.roomSize = 0.78f; p.damping = 0.45f; p.width = 1.f;
            p.wetLevel = 0.42f * rv; p.dryLevel = 1.f - 0.35f * rv;
            reverb.setParameters (p);
            reverb.processStereo (l, r, numSamples);
        }
    }
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
    outLevel = juce::jmax (peak, outLevel.load() * 0.85f);
    keys = engine.activeKeys();
}

// ------------------------------------------------------------------------------------------------ programmi e banchi
const VoiceData& ICX7Processor::bankVoice (int program) const
{
    program = juce::jlimit (0, 31, program);
    if (currentBank == userBankIndex) return userBank[(size_t) program];
    return factoryBanks()[(size_t) juce::jlimit (0, numBanks() - 1, currentBank)].voices[(size_t) program];
}

juce::String ICX7Processor::bankName (int index) const
{
    if (index == userBankIndex) return "USER";
    if (index >= 0 && index < numBanks()) return factoryBanks()[(size_t) index].name;
    return {};
}

const juce::String ICX7Processor::getProgramName (int index)
{
    return juce::String (index + 1).paddedLeft ('0', 2) + " " + voiceName (bankVoice (index));
}

void ICX7Processor::applyVoiceToParams (const VoiceData& v)
{
    const juce::ScopedValueSetter<bool> svs (loadingVoice, true);
    for (auto& d : voiceParams())
        if (auto* p = state.getParameter (d.id))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (d.minV, d.maxV, (int) v[(size_t) d.byte])));
    for (int op = 1; op <= 6; ++op)
        if (auto* p = state.getParameter ("op" + juce::String (op) + "_on"))
            p->setValueNotifyingHost (p->convertTo0to1 (1.f));
    voiceNameStr = voiceName (v);
    edited = false;
    voiceDirty = true;
}

void ICX7Processor::setCurrentProgram (int index)
{
    currentProgram = juce::jlimit (0, 31, index);
    applyVoiceToParams (bankVoice (currentProgram));
    notifyEditor();
}

void ICX7Processor::selectBank (int index)
{
    if (index == userBankIndex && ! userBankValid) return;
    currentBank = index == userBankIndex ? userBankIndex : juce::jlimit (0, numBanks() - 1, index);
    setCurrentProgram (currentProgram);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

void ICX7Processor::initVoice()
{
    applyVoiceToParams (icx7::initVoice());
    edited = true;
    notifyEditor();
}

VoiceData ICX7Processor::editVoice() const
{
    VoiceData v {};
    for (auto& d : voiceParams()) v[(size_t) d.byte] = (uint8_t) paramInt (d.id);
    setVoiceName (v, voiceNameStr);
    return v;
}

void ICX7Processor::setEditName (const juce::String& n)
{
    VoiceData v {};
    setVoiceName (v, n);
    voiceNameStr = voiceName (v);
    edited = true;
    notifyEditor();
}

void ICX7Processor::storeToCurrentSlot()
{
    if (currentBank != userBankIndex)
    {
        userBank = factoryBanks()[(size_t) currentBank].voices;
        userBankValid = true;
        currentBank = userBankIndex;
    }
    userBank[(size_t) currentProgram] = editVoice();
    edited = false;
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    notifyEditor();
}

bool ICX7Processor::importSyx (const juce::File& f, juce::String& message)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) { message = "IMPOSSIBILE LEGGERE IL FILE"; return false; }
    std::array<VoiceData, 32> voices;
    for (auto& v : voices) v = icx7::initVoice();
    const int n = parseSysex (mb.getData(), mb.getSize(), voices);
    if (n == 32)
    {
        userBank = voices;
        userBankValid = true;
        currentBank = userBankIndex;
        setCurrentProgram (0);
        updateHostDisplay (ChangeDetails().withProgramChanged (true));
        message = "BANCO CARICATO: " + f.getFileNameWithoutExtension().toUpperCase();
        return true;
    }
    if (n == 1)
    {
        applyVoiceToParams (voices[0]);
        edited = true;
        notifyEditor();
        message = "VOCE CARICATA: " + voiceName (voices[0]);
        return true;
    }
    message = "NON E' UN FILE SYSEX DX7 (32 VOCI O VOCE SINGOLA)";
    return false;
}

bool ICX7Processor::exportBank (const juce::File& f, juce::String& message)
{
    std::array<VoiceData, 32> voices;
    for (int i = 0; i < 32; ++i) voices[(size_t) i] = bankVoice (i);
    if (edited) voices[(size_t) currentProgram] = editVoice();
    const auto mb = buildBulkDump (voices);
    if (mb.getSize() != 4104 || ! f.replaceWithData (mb.getData(), mb.getSize())) { message = "SALVATAGGIO NON RIUSCITO"; return false; }
    message = "BANCO SALVATO: " + f.getFileName().toUpperCase();
    return true;
}

bool ICX7Processor::exportVoice (const juce::File& f, juce::String& message)
{
    const auto mb = buildSingleVoiceDump (editVoice());
    if (! f.replaceWithData (mb.getData(), mb.getSize())) { message = "SALVATAGGIO NON RIUSCITO"; return false; }
    message = "VOCE SALVATA: " + f.getFileName().toUpperCase();
    return true;
}

int ICX7Processor::paramInt (const juce::String& id) const
{
    if (auto* v = state.getRawParameterValue (id)) return juce::roundToInt (v->load());
    return 0;
}

void ICX7Processor::setParamInt (const juce::String& id, int v)
{
    if (auto* p = state.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
        p->endChangeGesture();
    }
}

void ICX7Processor::parameterChanged (const juce::String&, float)
{
    voiceDirty = true;
    if (! loadingVoice && ! edited && juce::MessageManager::existsAndIsCurrentThread())
    {
        edited = true;
        notifyEditor();
    }
}

void ICX7Processor::notifyEditor()
{
    if (onPatchChanged) onPatchChanged();
}

void ICX7Processor::handleAsyncUpdate()
{
    const int pc = pendingProgram.exchange (-1);
    if (pc >= 0) setCurrentProgram (pc);
    if (sysexPending.load())
    {
        std::array<VoiceData, 32> voices;
        for (auto& v : voices) v = icx7::initVoice();
        const int n = parseSysex (sysexBuf.data(), (size_t) sysexSize.load(), voices);
        sysexPending = false;
        if (n == 32)
        {
            userBank = voices;
            userBankValid = true;
            currentBank = userBankIndex;
            setCurrentProgram (0);
            updateHostDisplay (ChangeDetails().withProgramChanged (true));
        }
        else if (n == 1)
        {
            applyVoiceToParams (voices[0]);
            edited = true;
            notifyEditor();
        }
    }
}

// ------------------------------------------------------------------------------------------------ stato
void ICX7Processor::getStateInformation (juce::MemoryBlock& dest)
{
    auto tree = state.copyState();
    tree.setProperty ("bank", currentBank, nullptr);
    tree.setProperty ("program", currentProgram, nullptr);
    tree.setProperty ("voiceName", voiceNameStr, nullptr);
    tree.setProperty ("edited", edited, nullptr);
    if (userBankValid)
    {
        juce::MemoryBlock mb;
        for (auto& v : userBank) mb.append (v.data(), voiceBytes);
        tree.setProperty ("userBank", mb.toBase64Encoding(), nullptr);
    }
    else
        tree.removeProperty ("userBank", nullptr);
    if (auto xml = tree.createXml()) copyXmlToBinary (*xml, dest);
}

void ICX7Processor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (state.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);

    userBankValid = false;
    juce::MemoryBlock mb;
    if (tree.hasProperty ("userBank") && mb.fromBase64Encoding (tree["userBank"].toString()) && mb.getSize() == 32 * voiceBytes)
    {
        for (int i = 0; i < 32; ++i)
        {
            std::memcpy (userBank[(size_t) i].data(), static_cast<const uint8_t*> (mb.getData()) + i * voiceBytes, voiceBytes);
            sanitize (userBank[(size_t) i]);
        }
        userBankValid = true;
    }
    int bank = (int) tree.getProperty ("bank", 0);
    if (bank == userBankIndex && ! userBankValid) bank = 0;
    currentBank = bank == userBankIndex ? bank : juce::jlimit (0, numBanks() - 1, bank);
    currentProgram = juce::jlimit (0, 31, (int) tree.getProperty ("program", 0));

    {
        const juce::ScopedValueSetter<bool> svs (loadingVoice, true);
        state.replaceState (tree);
    }
    voiceNameStr = tree.getProperty ("voiceName", voiceName (bankVoice (currentProgram))).toString();
    edited = (bool) tree.getProperty ("edited", false);
    voiceDirty = true;
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    notifyEditor();
}

juce::AudioProcessorEditor* ICX7Processor::createEditor() { return new ICX7Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ICX7Processor(); }
