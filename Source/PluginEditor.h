#pragma once
#include "PluginProcessor.h"
#include "MBStudioLookAndFeel.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Manopola con il valore stampato sotto (formato DX7: note, curve, rapporti...).
class KnobCell : public juce::Component
{
public:
    KnobCell (ICX7Processor&, const juce::String& paramId, const juce::String& label, bool showLabel);
    ~KnobCell() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Slider slider;
    juce::String paramId, label;
    std::function<void (KnobCell&)> onTouched;
    juce::String valueText() const;
private:
    ICX7Processor& proc;
    bool showLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class ICX7Panel : public juce::Component, private juce::Timer
{
public:
    explicit ICX7Panel (ICX7Processor&);
    ~ICX7Panel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

    static constexpr int baseW = 1480, baseH = 880;

private:
    struct OpRow
    {
        juce::TextButton onBtn, modeBtn;
        std::vector<KnobCell*> cells;          // nell'ordine delle colonne
        juce::Rectangle<int> area, egArea, freqArea;
    };
    struct Column { juce::String label; int field; int x; int w; };

    ICX7Processor& proc;
    std::vector<std::unique_ptr<KnobCell>> knobs;
    std::array<OpRow, 6> rows;                 // rows[0] = OP1
    std::vector<Column> columns;
    std::vector<KnobCell*> pegKnobs, lfoKnobs, algKnobs, masterKnobs;

    juce::TextButton voiceBtn[32];
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, loadBtn { "LOAD SYX" }, saveBankBtn { "SAVE BANK" },
                     saveVoiceBtn { "SAVE VOICE" }, storeBtn { "STORE" }, initBtn { "INIT" }, panicBtn { "PANIC" },
                     oksBtn { "KEY SYNC" }, lfoSyncBtn { "LFO SYNC" };
    juce::ComboBox bankBox, waveBox;
    juce::Label nameLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveAtt;
    mbstudio::ui::LedMeter meter { 12 };
    juce::MidiKeyboardComponent keyboard;
    mbstudio::midi::MidiControls midiButtons;
    mbstudio::midi::MidiLearnOverlay midiOverlay;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> displayArea, algArea, pegArea, opHeaderArea;
    juce::String touched, status;
    int statusTicks = 0, panicFlash = 0;
    icx7::VoiceData shown {};

    KnobCell* addKnob (const juce::String& id, const juce::String& label, bool showLabel);
    void setupButton (juce::TextButton&, juce::Colour led);
    void buildBankBox();
    void updateButtons();
    void showStatus (const juce::String&);
    void chooseFile (bool save, std::function<void (const juce::File&)>, const juce::String& title, const juce::String& defName = {});

    void paintAlgorithm (juce::Graphics&, juce::Rectangle<float>);
    void paintEnvelope (juce::Graphics&, juce::Rectangle<float>, const int rates[4], const int levels[4], bool pitch, bool dim);
    void timerCallback() override;
};

class ICX7Editor : public juce::AudioProcessorEditor
{
public:
    explicit ICX7Editor (ICX7Processor&);
    ~ICX7Editor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    mbstudio::ui::HardwareLookAndFeel lnf;
    ICX7Panel panel;
};
