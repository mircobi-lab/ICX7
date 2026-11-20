#include "PluginEditor.h"

namespace ui = mbstudio::ui;
namespace C = mbstudio::ui::colours;
using namespace icx7;

namespace
{
constexpr int leftX = 14, leftW = 292, rightX = 322;
constexpr int opTop = 214, opRowH = 88;
constexpr int knobPitch = 40;

juce::String upper (const juce::String& s) { return s.toUpperCase(); }
}

// ================================================================================================ manopola
KnobCell::KnobCell (ICX7Processor& p, const juce::String& id, const juce::String& l, bool show)
    : paramId (id), label (l), proc (p), showLabel (show)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setMouseDragSensitivity (160);
    slider.setVelocityBasedMode (false);
    slider.setScrollWheelEnabled (true);
    slider.setColour (juce::Slider::thumbColourId, C::print);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.state, id, slider);
    if (auto* prm = proc.state.getParameter (id))
        slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); if (onTouched) onTouched (*this); };
    slider.onDragStart = [this] { if (onTouched) onTouched (*this); };
    addAndMakeVisible (slider);
}

KnobCell::~KnobCell() { att.reset(); }

juce::String KnobCell::valueText() const
{
    if (auto* prm = proc.state.getParameter (paramId))
        return upper (prm->getCurrentValueAsText());
    return {};
}

void KnobCell::resized()
{
    auto r = getLocalBounds();
    if (showLabel) r.removeFromTop (14);
    r.removeFromBottom (14);
    slider.setBounds (r);
}

void KnobCell::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (showLabel)
        ui::paintPrinted (g, r.removeFromTop (14.f), label, 11.5f, juce::Justification::centred, C::print);
    ui::paintPrinted (g, r.removeFromBottom (14.f), valueText(), 11.5f, juce::Justification::centred, C::printDim);
}

// ================================================================================================ pannello
ICX7Panel::ICX7Panel (ICX7Processor& p)
    : proc (p), keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // ---- operatori
    const struct { int field; int w; } cols[] = {
        { FC, 44 }, { FF, 44 }, { DET, 44 }, { OL, 52 },
        { R1, knobPitch }, { R2, knobPitch }, { R3, knobPitch }, { R4, knobPitch },
        { L1, knobPitch }, { L2, knobPitch }, { L3, knobPitch }, { L4, knobPitch },
        { BP, 44 }, { LD, knobPitch }, { RD, knobPitch }, { LC, knobPitch }, { RC, knobPitch }, { RS, knobPitch },
        { KVS, knobPitch }, { AMS, knobPitch } };
    for (auto& c : cols)
        columns.push_back ({ findParam (opParamId (1, c.field))->label, c.field, 0, c.w });

    for (int op = 1; op <= 6; ++op)
    {
        auto& row = rows[(size_t) (op - 1)];
        for (auto& c : columns)
            row.cells.push_back (addKnob (opParamId (op, c.field), c.label, false));

        row.onBtn.setButtonText ("OP " + juce::String (op));
        row.onBtn.setClickingTogglesState (true);
        row.onBtn.setTooltip ("Accende/spegne l'operatore " + juce::String (op));
        const auto onId = "op" + juce::String (op) + "_on";
        row.onBtn.setToggleState (proc.paramInt (onId) != 0, juce::dontSendNotification);
        row.onBtn.onClick = [this, onId, op]
        {
            proc.setParamInt (onId, rows[(size_t) (op - 1)].onBtn.getToggleState() ? 1 : 0);
            repaint (algArea);
        };
        addAndMakeVisible (row.onBtn);

        const auto modeId = opParamId (op, MODE);
        row.modeBtn.setTooltip ("RATIO: frequenza proporzionale alla nota.  FIXED: frequenza fissa in Hz");
        row.modeBtn.onClick = [this, modeId, op]
        {
            proc.setParamInt (modeId, proc.paramInt (modeId) ? 0 : 1);
            touched = "OP" + juce::String (op) + " MODE  =  " + formatValue (Fmt::mode, proc.paramInt (modeId));
            updateButtons();
        };
        setupButton (row.modeBtn, C::ledBlue);
        addAndMakeVisible (row.modeBtn);
    }

    // ---- algoritmo, pitch EG, LFO
    algKnobs.push_back (addKnob ("alg", "ALGORITHM", true));
    algKnobs.push_back (addKnob ("fb", "FEEDBACK", true));
    for (auto id : { "pr1", "pr2", "pr3", "pr4", "pl1", "pl2", "pl3", "pl4" })
        pegKnobs.push_back (addKnob (id, findParam (id)->label, true));
    for (auto id : { "lfs", "lfd", "lpmd", "lamd", "pms", "trnsp" })
        lfoKnobs.push_back (addKnob (id, findParam (id)->label, true));
    for (auto [id, label] : { std::pair { "volume", "VOLUME" }, { "chorus", "CHORUS" }, { "reverb", "REVERB" }, { "bend", "BEND" } })
        masterKnobs.push_back (addKnob (id, label, true));

    for (int i = 0; i < 6; ++i) waveBox.addItem (formatValue (Fmt::wave, i), i + 1);
    waveAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.state, "lfw", waveBox);
    addAndMakeVisible (waveBox);

    for (auto [btn, id] : { std::pair { &oksBtn, "oks" }, { &lfoSyncBtn, "lfks" } })
    {
        setupButton (*btn, C::ledGreen);
        const juce::String pid (id);
        btn->onClick = [this, pid] { proc.setParamInt (pid, proc.paramInt (pid) ? 0 : 1); updateButtons(); };
        addAndMakeVisible (*btn);
    }

    // ---- selezione voci (tasti a membrana del DX7)
    for (int i = 0; i < 32; ++i)
    {
        voiceBtn[i].setButtonText (juce::String (i + 1));
        setupButton (voiceBtn[i], C::ledGreen);
        voiceBtn[i].onClick = [this, i] { proc.setCurrentProgram (i); };
        addAndMakeVisible (voiceBtn[i]);
    }

    // ---- intestazione
    bankBox.onChange = [this]
    {
        const int id = bankBox.getSelectedId();
        if (id <= 0) return;
        const int bank = id == 1000 ? ICX7Processor::userBankIndex : id - 1;
        if (bank != proc.currentBankIndex()) proc.selectBank (bank);
    };
    addAndMakeVisible (bankBox);

    prevBtn.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 31) % 32); };
    nextBtn.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % 32); };
    loadBtn.setTooltip ("Carica un banco DX7 da 32 voci o una voce singola (.syx)");
    loadBtn.onClick = [this]
    {
        chooseFile (false, [this] (const juce::File& f) { juce::String m; proc.importSyx (f, m); showStatus (m); }, "Banco o voce DX7 (.syx)");
    };
    saveBankBtn.setTooltip ("Salva il banco corrente come SysEx DX7 da 32 voci");
    saveBankBtn.onClick = [this]
    {
        chooseFile (true, [this] (const juce::File& f) { juce::String m; proc.exportBank (f.withFileExtension ("syx"), m); showStatus (m); },
                    "Salva banco DX7", "ICX7 " + proc.currentBankName() + ".syx");
    };
    saveVoiceBtn.setTooltip ("Salva la voce in modifica come SysEx DX7 a voce singola");
    saveVoiceBtn.onClick = [this]
    {
        chooseFile (true, [this] (const juce::File& f) { juce::String m; proc.exportVoice (f.withFileExtension ("syx"), m); showStatus (m); },
                    "Salva voce DX7", proc.editName().trim() + ".syx");
    };
    storeBtn.setTooltip ("Memorizza la voce modificata nel banco USER, alla posizione corrente");
    storeBtn.getProperties().set ("led", true);
    storeBtn.getProperties().set ("ledColour", "ffffb020");
    storeBtn.onClick = [this]
    {
        proc.storeToCurrentSlot();
        showStatus ("MEMORIZZATA IN USER " + juce::String (proc.getCurrentProgram() + 1));
    };
    initBtn.setTooltip ("Voce iniziale: solo OP1, onda sinusoidale");
    initBtn.onClick = [this] { proc.initVoice(); showStatus ("INIT VOICE"); };
    for (auto* b : { &prevBtn, &nextBtn, &loadBtn, &saveBankBtn, &saveVoiceBtn, &storeBtn, &initBtn }) addAndMakeVisible (*b);

    nameLabel.setEditable (false, true, false);
    nameLabel.setFont (ui::Fonts::display (30.f));
    nameLabel.setColour (juce::Label::textColourId, C::print);
    nameLabel.setColour (juce::Label::backgroundWhenEditingColourId, C::recess);
    nameLabel.setColour (juce::Label::textWhenEditingColourId, C::print);
    nameLabel.setColour (juce::Label::outlineWhenEditingColourId, C::recessEdge);
    nameLabel.setTooltip ("Doppio clic per rinominare la voce (10 caratteri)");
    nameLabel.onEditorShow = [this] { if (auto* ed = nameLabel.getCurrentTextEditor()) ed->setInputRestrictions (10); };
    nameLabel.onTextChange = [this] { proc.setEditName (nameLabel.getText()); };
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (meter);

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffdcdcd8));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff0d0d0e));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff3a3c40));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, C::ledBlue.withAlpha (.55f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, C::ledBlue.withAlpha (.18f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (.5f));
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, C::key);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, C::print);
    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (36);
    keyboard.setWantsKeyboardFocus (false);
    addAndMakeVisible (keyboard);

    juce::Component::SafePointer<ICX7Panel> safeThis (this);
    proc.onPatchChanged = [safeThis]
    {
        auto fn = [safeThis] { if (safeThis != nullptr) safeThis->refresh(); };
        if (juce::MessageManager::existsAndIsCurrentThread()) fn();
        else juce::MessageManager::callAsync (fn);
    };
    setSize (baseW, baseH);
    refresh();
    startTimerHz (30);
}

ICX7Panel::~ICX7Panel()
{
    stopTimer();
    proc.onPatchChanged = nullptr;
    waveAtt.reset();
}

KnobCell* ICX7Panel::addKnob (const juce::String& id, const juce::String& label, bool showLabel)
{
    auto k = std::make_unique<KnobCell> (proc, id, label, showLabel);
    k->onTouched = [this] (KnobCell& c)
    {
        juce::String name = c.label;
        if (c.paramId.startsWith ("op")) name = "OP" + c.paramId.substring (2, 3) + " " + name;
        else if (c.paramId.startsWith ("p") && c.paramId.length() == 3) name = "PITCH EG " + name;
        touched = name + "  =  " + c.valueText();
        repaint (displayArea);
    };
    addAndMakeVisible (*k);
    knobs.push_back (std::move (k));
    return knobs.back().get();
}

void ICX7Panel::setupButton (juce::TextButton& b, juce::Colour led)
{
    b.getProperties().set ("led", true);
    b.getProperties().set ("ledColour", led.toString());
}

void ICX7Panel::buildBankBox()
{
    bankBox.clear (juce::dontSendNotification);
    bankBox.addSectionHeading ("FACTORY");
    for (int i = 0; i < proc.numBanks(); ++i) bankBox.addItem (proc.bankName (i), i + 1);
    if (proc.hasUserBank())
    {
        bankBox.addSectionHeading ("UTENTE");
        bankBox.addItem ("USER", 1000);
    }
    const int b = proc.currentBankIndex();
    bankBox.setSelectedId (b == ICX7Processor::userBankIndex ? 1000 : b + 1, juce::dontSendNotification);
}

void ICX7Panel::refresh()
{
    buildBankBox();
    nameLabel.setText (proc.editName(), juce::dontSendNotification);
    for (int i = 0; i < 32; ++i)
        voiceBtn[i].setTooltip (proc.getProgramName (i));
    touched.clear();
    updateButtons();
    repaint();
}

void ICX7Panel::updateButtons()
{
    const int cur = proc.getCurrentProgram();
    for (int i = 0; i < 32; ++i) voiceBtn[i].setToggleState (i == cur, juce::dontSendNotification);
    for (int op = 1; op <= 6; ++op)
    {
        auto& row = rows[(size_t) (op - 1)];
        const bool fixed = proc.paramInt (opParamId (op, MODE)) != 0;
        row.modeBtn.setButtonText (fixed ? "FIXED" : "RATIO");
        row.modeBtn.setToggleState (fixed, juce::dontSendNotification);
        row.onBtn.setToggleState (proc.paramInt ("op" + juce::String (op) + "_on") != 0, juce::dontSendNotification);
    }
    oksBtn.setToggleState (proc.paramInt ("oks") != 0, juce::dontSendNotification);
    lfoSyncBtn.setToggleState (proc.paramInt ("lfks") != 0, juce::dontSendNotification);
    storeBtn.setToggleState (proc.isEdited(), juce::dontSendNotification);
}

void ICX7Panel::showStatus (const juce::String& s)
{
    status = s;
    statusTicks = 120;
    refresh();
}

void ICX7Panel::chooseFile (bool save, std::function<void (const juce::File&)> fn, const juce::String& title, const juce::String& defName)
{
    const auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> (title, defName.isNotEmpty() ? dir.getChildFile (defName) : dir, "*.syx;*.SYX");
    const int fcFlags = (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                            : juce::FileBrowserComponent::openMode) | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (fcFlags, [fn] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f != juce::File()) fn (f);
    });
}

void ICX7Panel::timerCallback()
{
    meter.setLevel (std::pow (juce::jmin (1.f, proc.outputLevel()), .5f));
    if (statusTicks > 0 && --statusTicks == 0) { status.clear(); repaint (displayArea); }

    // ridisegna schemi e inviluppi solo quando la voce cambia
    VoiceData v {};
    for (auto& d : voiceParams()) v[(size_t) d.byte] = (uint8_t) proc.paramInt (d.id);
    for (int op = 1; op <= 6; ++op) v[(size_t) (145 + op)] = (uint8_t) proc.paramInt ("op" + juce::String (op) + "_on");
    if (v != shown)
    {
        shown = v;
        updateButtons();
        repaint();
    }
    else
        repaint (displayArea);
}

// ------------------------------------------------------------------------------------------------ layout
void ICX7Panel::resized()
{
    // intestazione
    displayArea = { 330, 14, 520, 88 };
    nameLabel.setBounds (displayArea.getX() + 150, displayArea.getY() + 6, 250, 34);
    bankBox.setBounds (866, 16, 120, 28);
    prevBtn.setBounds (992, 16, 40, 28);
    nextBtn.setBounds (1036, 16, 40, 28);
    initBtn.setBounds (1082, 16, 70, 28);
    loadBtn.setBounds (866, 58, 92, 30);
    saveBankBtn.setBounds (962, 58, 96, 30);
    saveVoiceBtn.setBounds (1062, 58, 104, 30);
    storeBtn.setBounds (1170, 58, 86, 30);
    {
        int x = 1262;
        for (auto* k : masterKnobs) { k->setBounds (x, 12, 50, 74); x += 50; }
    }
    meter.setBounds (baseW - 22, 18, 10, 80);

    // tasti voce
    {
        const float w = (float) (baseW - 28 - 31 * 4) / 32.f;
        for (int i = 0; i < 32; ++i)
            voiceBtn[i].setBounds ((int) (14 + i * (w + 4)), 124, (int) w, 30);
    }

    // colonna sinistra
    algArea = { leftX, 196, leftW, 196 };
    algKnobs[0]->setBounds (leftX, 398, 70, 66);
    algKnobs[1]->setBounds (leftX + 72, 398, 70, 66);
    oksBtn.setBounds (leftX + 156, 418, 120, 28);

    pegArea = { leftX, 498, leftW, 56 };
    for (size_t i = 0; i < pegKnobs.size(); ++i)
        pegKnobs[i]->setBounds (leftX + (int) i * 36 + 2, 558, 36, 62);

    for (size_t i = 0; i < lfoKnobs.size(); ++i)
        lfoKnobs[i]->setBounds (leftX + (int) i * 48 + 2, 656, 48, 64);
    waveBox.setBounds (leftX, 726, 146, 26);
    lfoSyncBtn.setBounds (leftX + 156, 726, 120, 26);

    // operatori
    int x = rightX + 140;
    for (size_t c = 0; c < columns.size(); ++c)
    {
        const int field = columns[c].field;
        if (field == OL || field == BP || field == KVS) x += 10;
        if (field == R1) x += 128;                         // spazio per il grafico dell'inviluppo
        columns[c].x = x;
        x += columns[c].w;
    }
    opHeaderArea = { rightX, 176, x - rightX, 36 };
    for (int op = 1; op <= 6; ++op)
    {
        auto& row = rows[(size_t) (op - 1)];
        const int y = opTop + (op - 1) * opRowH;
        row.area = { rightX, y, x - rightX, opRowH };
        row.onBtn.setBounds (rightX, y + 12, 62, 28);
        row.modeBtn.setBounds (rightX + 70, y + 12, 64, 28);
        row.freqArea = { rightX + 70, y + 46, 64, 22 };
        for (size_t c = 0; c < columns.size(); ++c)
            row.cells[c]->setBounds (columns[c].x, y + 6, columns[c].w, 64);
        const int egX = columns[4].x - 124;
        row.egArea = { egX, y + 8, 116, 60 };
    }

    keyboard.setBounds (14, 800, baseW - 28, 66);
    keyboard.setKeyWidth ((float) (baseW - 28) / 49.f);
}

// ------------------------------------------------------------------------------------------------ disegno
void ICX7Panel::paint (juce::Graphics& g)
{
    ui::paintChassis (g, getLocalBounds());

    // marchio
    ui::paintBrand (g, { 18.f, 16.f, 300.f, 36.f }, "ICX7");
    ui::paintPrinted (g, { 18.f, 58.f, 300.f, 16.f }, "6-OPERATOR FM SYNTHESIZER", 15.f, juce::Justification::centredLeft, C::printDim);
    ui::paintPrinted (g, { 18.f, 78.f, 300.f, 14.f }, "32 ALGORITHMS  -  DX7 SYSEX COMPATIBLE", 11.5f, juce::Justification::centredLeft, C::printDim.withAlpha (.75f));

    // display
    {
        const auto d = displayArea.toFloat();
        ui::paintRecess (g, d);
        const auto in = d.reduced (12.f, 8.f);
        const int cur = proc.getCurrentProgram();
        g.setColour (C::print);
        g.setFont (ui::Fonts::display (30.f));
        g.drawText (proc.currentBankName() + " " + juce::String (cur + 1).paddedLeft ('0', 2), in.withWidth (140.f).withHeight (34.f),
                    juce::Justification::centredLeft, false);
        if (proc.isEdited())
        {
            ui::paintLed (g, { d.getRight() - 64.f, in.getY() + 17.f }, 3.5f, C::ledAmber, 1.f);
            ui::paintPrinted (g, { d.getRight() - 56.f, in.getY() + 9.f, 44.f, 16.f }, "EDIT", 12.f, juce::Justification::centredLeft, C::print);
        }
        g.setFont (ui::Fonts::label (14.f));
        g.setColour (C::printDim);
        const int alg = proc.paramInt ("alg");
        g.drawText ("ALGORITHM " + juce::String (alg + 1) + "    FEEDBACK " + juce::String (proc.paramInt ("fb"))
                    + "    TRANSPOSE " + formatValue (Fmt::transpose, proc.paramInt ("trnsp"))
                    + "    KEYS " + juce::String (proc.keysDown()),
                    in.withY (in.getY() + 38.f).withHeight (18.f), juce::Justification::centredLeft, true);
        const juce::String line3 = status.isNotEmpty() ? status : touched.isNotEmpty() ? touched : "DOPPIO CLIC SUL NOME PER RINOMINARE LA VOCE";
        g.setColour (status.isNotEmpty() ? C::ledAmber : touched.isNotEmpty() ? C::print : C::printDim.withAlpha (.6f));
        g.drawText (upper (line3), in.withY (in.getY() + 56.f).withHeight (18.f), juce::Justification::centredLeft, true);
        ui::paintLed (g, { d.getRight() - 14.f, d.getY() + 12.f }, 3.5f, C::ledGreen, juce::jmin (1.f, proc.outputLevel() * 3.f));
    }
    ui::paintPrinted (g, { 866.f, 46.f, 300.f, 12.f }, "BANK  /  VOICE", 11.f, juce::Justification::centredLeft, C::printDim);
    ui::paintPrinted (g, { (float) baseW - 32.f, 100.f, 28.f, 12.f }, "OUT", 11.f, juce::Justification::centredRight, C::printDim);

    ui::paintEngravedLine (g, { 14.f, 114.f }, { (float) baseW - 14.f, 114.f });
    ui::paintEngravedLine (g, { 14.f, 164.f }, { (float) baseW - 14.f, 164.f });

    // colonna sinistra
    auto chipAt = [&] (float x, float y, const juce::String& t) { ui::paintChip (g, { x, y, ui::chipWidth (t, 16.f), 16.f }, t, 14.f); };
    chipAt ((float) leftX, 174.f, "ALGORITHM");
    ui::paintRecess (g, algArea.toFloat());
    paintAlgorithm (g, algArea.toFloat().reduced (8.f));

    ui::paintEngravedLine (g, { (float) leftX, 474.f }, { (float) (leftX + leftW), 474.f });
    chipAt ((float) leftX, 478.f, "PITCH EG");
    ui::paintRecess (g, pegArea.toFloat());
    {
        int r[4], l[4];
        for (int i = 0; i < 4; ++i) { r[i] = proc.paramInt ("pr" + juce::String (i + 1)); l[i] = proc.paramInt ("pl" + juce::String (i + 1)); }
        paintEnvelope (g, pegArea.toFloat().reduced (6.f, 5.f), r, l, true, false);
    }
    ui::paintEngravedLine (g, { (float) leftX, 630.f }, { (float) (leftX + leftW), 630.f });
    chipAt ((float) leftX, 634.f, "LFO  /  GLOBAL");

    // intestazioni delle colonne degli operatori
    {
        const float y = (float) opHeaderArea.getY();
        auto colX = [&] (int field) { for (auto& c : columns) if (c.field == field) return (float) c.x; return 0.f; };
        auto colR = [&] (int field) { for (auto& c : columns) if (c.field == field) return (float) (c.x + c.w); return 0.f; };
        chipAt ((float) rightX, y - 2.f, "OPERATOR");
        chipAt ((float) rightX + 70.f, y - 2.f, "OSCILLATOR");
        chipAt (colX (OL), y - 2.f, "OUT");
        chipAt (colX (R1) - 124.f, y - 2.f, "ENVELOPE GENERATOR");
        chipAt (colX (BP), y - 2.f, "KEYBOARD LEVEL SCALING");
        chipAt (colX (KVS), y - 2.f, "SENS");
        for (auto& c : columns)
            ui::paintPrinted (g, { (float) c.x, y + 18.f, (float) c.w, 14.f }, c.label, 11.5f, juce::Justification::centred, C::print);
        juce::ignoreUnused (colR);
    }

    // righe degli operatori
    const auto& A = algorithm (proc.paramInt ("alg"));
    for (int op = 1; op <= 6; ++op)
    {
        const auto& row = rows[(size_t) (op - 1)];
        const float y = (float) row.area.getY();
        ui::paintEngravedLine (g, { (float) rightX, y }, { (float) row.area.getRight(), y });
        const bool on = proc.paramInt ("op" + juce::String (op) + "_on") != 0;
        const bool carrier = (A.carriers >> op) & 1;
        ui::paintLed (g, { (float) rightX + 8.f, y + 56.f }, 3.5f, C::ledBlue, carrier && on ? 1.f : 0.f);
        ui::paintPrinted (g, { (float) rightX + 16.f, y + 49.f, 60.f, 14.f }, carrier ? "CARRIER" : "MODULATOR", 11.f,
                          juce::Justification::centredLeft, carrier ? C::print : C::printDim);

        ui::paintRecess (g, row.freqArea.toFloat(), 2.f);
        g.setColour (on ? C::print : C::printDim);
        g.setFont (ui::Fonts::label (12.5f));
        g.drawText (frequencyText (proc.paramInt (opParamId (op, MODE)), proc.paramInt (opParamId (op, FC)),
                                   proc.paramInt (opParamId (op, FF)), proc.paramInt (opParamId (op, DET))),
                    row.freqArea, juce::Justification::centred, false);

        ui::paintRecess (g, row.egArea.toFloat(), 2.f);
        int r[4], l[4];
        for (int i = 0; i < 4; ++i) { r[i] = proc.paramInt (opParamId (op, R1 + i)); l[i] = proc.paramInt (opParamId (op, L1 + i)); }
        paintEnvelope (g, row.egArea.toFloat().reduced (4.f, 4.f), r, l, false, ! on || proc.paramInt (opParamId (op, OL)) == 0);
    }
    ui::paintEngravedLine (g, { (float) rightX, (float) (opTop + 6 * opRowH) }, { (float) rows[0].area.getRight(), (float) (opTop + 6 * opRowH) });

    // piede
    ui::paintEngravedLine (g, { 14.f, 774.f }, { (float) baseW - 14.f, 774.f });
    ui::paintPrinted (g, { 14.f, 778.f, 900.f, 16.f }, "FM ENGINE: DX7-CORE BY KENJI NOGUCHI (APACHE 2.0)  -  MSFA ENVELOPES AND ROUTING", 11.f,
                      juce::Justification::centredLeft, C::printDim.withAlpha (.7f));
    ui::paintLogo (g, { (float) baseW - 260.f, 776.f, 246.f, 20.f }, false, ui::Imprint::debossed, juce::Justification::centredRight);
}

void ICX7Panel::paintEnvelope (juce::Graphics& g, juce::Rectangle<float> r, const int rates[4], const int levels[4], bool pitch, bool dim)
{
    // tempo di ciascun tratto ~ distanza di livello / velocità (scala esponenziale come sul DX7)
    auto segTime = [] (int rate, int from, int to)
    {
        const float dist = std::abs ((float) (to - from)) / 99.f;
        return (0.08f + dist) * std::pow (2.f, (99.f - (float) rate) / 14.f) * 0.05f;
    };
    float t[4];
    float total = 0;
    int prev = levels[3];
    for (int i = 0; i < 3; ++i) { t[i] = juce::jmin (3.f, segTime (rates[i], prev, levels[i])); total += t[i]; prev = levels[i]; }
    t[3] = juce::jmin (3.f, segTime (rates[3], levels[2], levels[3]));
    const float sustain = juce::jmax (0.25f, (total + t[3]) * 0.25f);
    const float span = total + sustain + t[3];
    auto px = [&] (float time) { return r.getX() + time / span * r.getWidth(); };
    auto py = [&] (int lv) { return r.getBottom() - (float) lv / 99.f * r.getHeight(); };

    if (pitch)
    {
        g.setColour (C::printDim.withAlpha (.35f));
        const float mid = py (50);
        for (float x = r.getX(); x < r.getRight(); x += 6.f) g.drawLine (x, mid, x + 3.f, mid, 1.f);
    }
    juce::Path p;
    float time = 0;
    p.startNewSubPath (px (0), py (levels[3]));
    for (int i = 0; i < 3; ++i) { time += t[i]; p.lineTo (px (time), py (levels[i])); }
    const float keyOff = time + sustain;
    p.lineTo (px (keyOff), py (levels[2]));
    p.lineTo (px (keyOff + t[3]), py (levels[3]));
    g.setColour (C::printDim.withAlpha (.4f));
    g.drawLine (px (keyOff), r.getY(), px (keyOff), r.getBottom(), 1.f);
    g.setColour (dim ? C::printDim.withAlpha (.45f) : C::print);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
}

void ICX7Panel::paintAlgorithm (juce::Graphics& g, juce::Rectangle<float> area)
{
    const int algIndex = proc.paramInt ("alg");
    const auto& A = algorithm (algIndex);
    const int fbAmount = proc.paramInt ("fb");

    // livelli: 0 = portanti, i modulatori stanno sopra il più alto dei loro bersagli
    int level[7] = {};
    uint8_t targets[7] = {};
    for (int op = 1; op <= 6; ++op)
        for (int m = 1; m <= 6; ++m)
            if ((A.modulators[(size_t) op] >> m) & 1) targets[m] |= (uint8_t) (1 << op);
    for (int pass = 0; pass < 6; ++pass)
        for (int m = 1; m <= 6; ++m)
            for (int op = 1; op <= 6; ++op)
                if ((targets[m] >> op) & 1) level[m] = juce::jmax (level[m], level[op] + 1);

    // colonne: disposizione ad albero partendo dai portanti
    float col[7] = {};
    bool placed[7] = {};
    float next = 0;
    std::function<float (int)> place = [&] (int op) -> float
    {
        placed[op] = true;
        float sum = 0; int n = 0;
        for (int m = 1; m <= 6; ++m)
            if (((A.modulators[(size_t) op] >> m) & 1) && ! placed[m] && level[m] == level[op] + 1) { sum += place (m); ++n; }
        col[op] = n > 0 ? sum / (float) n : next++;
        return col[op];
    };
    for (int op = 1; op <= 6; ++op)
        if ((A.carriers >> op) & 1) place (op);
    for (int op = 1; op <= 6; ++op)
        if (! placed[op]) place (op);
    for (int m = 1; m <= 6; ++m)        // un modulatore condiviso va al centro dei suoi bersagli
    {
        if (juce::countNumberOfBits ((uint32_t) targets[m]) > 1)
        {
            float s = 0; int n = 0;
            for (int op = 1; op <= 6; ++op) if ((targets[m] >> op) & 1) { s += col[op]; ++n; }
            col[m] = s / (float) n;
        }
    }
    int maxLevel = 0;
    for (int op = 1; op <= 6; ++op) maxLevel = juce::jmax (maxLevel, level[op]);
    const float cols = juce::jmax (1.f, next);

    // intestazione numero algoritmo
    g.setColour (C::print);
    g.setFont (ui::Fonts::display (34.f));
    g.drawText (juce::String (algIndex + 1).paddedLeft ('0', 2), area.withHeight (34.f), juce::Justification::topRight, false);

    const float box = 30.f;
    auto drawArea = area.withTrimmedTop (14.f).withTrimmedBottom (20.f).withTrimmedRight (36.f).withTrimmedLeft (8.f);
    const float stepX = juce::jmin (50.f, drawArea.getWidth() / cols);
    const float stepY = juce::jmin (46.f, (drawArea.getHeight() - box) / (float) juce::jmax (1, maxLevel));
    const float originX = drawArea.getCentreX() - stepX * (cols - 1.f) * .5f;
    const float usedH = box + stepY * (float) maxLevel;
    const float baseY = drawArea.getCentreY() + usedH * .5f - box * .5f;
    auto centre = [&] (int op)
    {
        return juce::Point<float> (originX + col[op] * stepX, baseY - (float) level[op] * stepY);
    };

    // linee di modulazione
    g.setColour (C::printDim);
    for (int op = 1; op <= 6; ++op)
        for (int m = 1; m <= 6; ++m)
            if ((A.modulators[(size_t) op] >> m) & 1)
            {
                const auto a = centre (m), b = centre (op);
                g.drawLine (a.x, a.y + box * .5f, b.x, b.y - box * .5f, 1.5f);
            }
    // bus d'uscita
    float minX = 1e9f, maxX = -1e9f;
    const float busY = baseY + box * .5f + 12.f;
    for (int op = 1; op <= 6; ++op)
        if ((A.carriers >> op) & 1)
        {
            const auto c = centre (op);
            minX = juce::jmin (minX, c.x); maxX = juce::jmax (maxX, c.x);
            g.drawLine (c.x, c.y + box * .5f, c.x, busY, 1.5f);
        }
    g.drawLine (minX, busY, maxX, busY, 1.5f);
    ui::paintPrinted (g, { maxX + 6.f, busY - 7.f, 40.f, 14.f }, "OUT", 11.f, juce::Justification::centredLeft, C::printDim);

    // feedback
    if (A.fbFrom > 0 && A.fbTo > 0)
    {
        const auto f = centre (A.fbFrom), t = centre (A.fbTo);
        const float xr = juce::jmax (f.x, t.x) + box * .5f + 8.f;
        const float top = t.y - box * .5f - 7.f;
        juce::Path p;
        p.startNewSubPath (f.x + box * .5f, f.y);
        p.lineTo (xr, f.y);
        p.lineTo (xr, top);
        p.lineTo (t.x, top);
        p.lineTo (t.x, t.y - box * .5f);
        g.setColour (fbAmount > 0 ? C::print : C::printDim.withAlpha (.5f));
        g.strokePath (p, juce::PathStrokeType (1.5f));
        ui::paintLed (g, { xr, (top + f.y) * .5f }, 3.f, C::ledAmber, (float) fbAmount / 7.f);
    }

    // operatori
    for (int op = 1; op <= 6; ++op)
    {
        const auto c = centre (op);
        const auto r = juce::Rectangle<float> (box, box).withCentre (c);
        const bool carrier = (A.carriers >> op) & 1;
        const bool on = proc.paramInt ("op" + juce::String (op) + "_on") != 0;
        if (carrier && on) { g.setColour (C::chip); g.fillRoundedRectangle (r, 2.f); }
        else { g.setColour (C::recess); g.fillRoundedRectangle (r, 2.f); }
        g.setColour (on ? C::print : C::printDim.withAlpha (.5f));
        g.drawRoundedRectangle (r, 2.f, 1.2f);
        g.setColour (carrier && on ? C::chipText : on ? C::print : C::printDim.withAlpha (.5f));
        g.setFont (ui::Fonts::display (20.f));
        g.drawText (juce::String (op), r.translated (0, 1.f), juce::Justification::centred, false);
    }
}

// ================================================================================================ editor
ICX7Editor::ICX7Editor (ICX7Processor& p) : AudioProcessorEditor (p), panel (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) ICX7Panel::baseW / ICX7Panel::baseH);
    setResizeLimits (ICX7Panel::baseW / 2, ICX7Panel::baseH / 2, ICX7Panel::baseW * 2, ICX7Panel::baseH * 2);
    setSize ((int) (ICX7Panel::baseW * .85), (int) (ICX7Panel::baseH * .85));
}

ICX7Editor::~ICX7Editor() { setLookAndFeel (nullptr); }

void ICX7Editor::paint (juce::Graphics& g) { ui::paintChassis (g, getLocalBounds()); }

void ICX7Editor::resized()
{
    const float s = (float) getWidth() / (float) ICX7Panel::baseW;
    panel.setBounds (0, 0, ICX7Panel::baseW, ICX7Panel::baseH);
    panel.setTransform (juce::AffineTransform::scale (s));
}
