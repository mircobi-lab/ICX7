/*  Applicazione Standalone di ICX7 con lo stile MBStudio anche per finestra, menu e dialoghi.
    - al primo avvio abilita tutti gli ingressi MIDI collegati (tastiere e controller);
    - argomenti di prova: --program N  --shot FILE.png  (salva un'immagine del pannello ed esce)        */
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

class ICX7StandaloneApp final : public juce::JUCEApplication
{
public:
    ICX7StandaloneApp()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = juce::CharPointer_UTF8 (JucePlugin_Name);
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        o.folderName = "~/.config";
       #endif
        appProperties.setStorageParameters (o);
    }

    const juce::String getApplicationName() override     { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override  { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override           { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String& commandLine) override
    {
        auto* settings = appProperties.getUserSettings();
        const bool firstRun = ! settings->containsKey ("icx7FirstRunDone");
        settings->setValue ("icx7FirstRunDone", true);

        juce::LookAndFeel::setDefaultLookAndFeel (&hardwareLook);
        mainWindow = std::make_unique<juce::StandaloneFilterWindow> (getApplicationName(), mbstudio::ui::colours::chassisBottom,
                                                                     settings, false);
        if (firstRun)
            for (auto& dev : juce::MidiInput::getAvailableDevices())
                mainWindow->pluginHolder->deviceManager.setMidiInputDeviceEnabled (dev.identifier, true);
        mainWindow->setVisible (true);
        // profilo del controller scelto dal nome della porta MIDI (solo se non ci sono gia' assegnazioni)
        if (auto* proc = dynamic_cast<ICX7Processor*> (mainWindow->getAudioProcessor()))
        {
            juce::StringArray ports;
            for (auto& dev : juce::MidiInput::getAvailableDevices())
                if (mainWindow->pluginHolder->deviceManager.isMidiInputDeviceEnabled (dev.identifier)) ports.add (dev.name);
            proc->midiMap.autoSelectFromPorts (ports);
        }
        applyCommandLine (commandLine);
    }

    void applyCommandLine (const juce::String& cmd)
    {
        auto args = juce::StringArray::fromTokens (cmd, true);
        auto* proc = dynamic_cast<ICX7Processor*> (mainWindow->getAudioProcessor());
        if (proc == nullptr) return;
        juce::String shot;
        for (int i = 0; i + 1 < args.size(); ++i)
        {
            const auto a = args[i], v = args[i + 1].unquoted();
            if (a == "--program") proc->setCurrentProgram (v.getIntValue() - 1);
            else if (a == "--shot") shot = v;
            else if (a == "--profile") proc->midiMap.applyProfile (v.getIntValue());
            else if (a == "--learn") proc->midiMap.setLearnMode (true);
            else if (a == "--selftest") { selfTest (*proc); }
            else if (a == "--set" && i + 2 < args.size()) { proc->setParamInt (v, args[i + 2].getIntValue()); ++i; }
        }
        if (shot.isNotEmpty())
            juce::Timer::callAfterDelay (1500, [this, shot]
            {
                if (auto* ed = mainWindow->getAudioProcessor()->getActiveEditor())
                {
                    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.f);
                    juce::File f (shot); f.deleteFile();
                    if (auto os = f.createOutputStream()) juce::PNGImageFormat().writeImageToStream (img, *os);
                }
                systemRequestedQuit();
            });
    }

    // prova del MIDI Learn e dei profili: invia CC al processore e controlla i parametri
    static void selfTest (ICX7Processor& p)
    {
        auto& mm = p.midiMap;
        auto run = [&p] (std::initializer_list<juce::MidiMessage> msgs)
        {
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer mb;
            int t = 0;
            for (auto& m : msgs) mb.addEvent (m, t++);
            p.processBlock (buf, mb);
            return mb.getNumEvents();
        };
        p.prepareToPlay (48000, 256);
        mm.clearAll();
        mm.startLearn ("op1_ol");
        run ({ juce::MidiMessage::controllerEvent (1, 74, 10) });
        juce::Timer::callAfterDelay (300, [&p, run] () mutable { selfTest2 (p, run); });
    }

    template <typename Run>
    static void selfTest2 (ICX7Processor& p, Run run)
    {
        auto& mm = p.midiMap;
        std::printf ("learn: %s\n", mm.describe ("op1_ol").toRawUTF8());
        // soft takeover: op1_ol = 99 -> CC 74 a 0 non deve agganciarsi
        run ({ juce::MidiMessage::controllerEvent (1, 74, 0) });
        std::printf ("takeover non agganciato: OL=%d (atteso 99)\n", p.paramInt ("op1_ol"));
        run ({ juce::MidiMessage::controllerEvent (1, 74, 126), juce::MidiMessage::controllerEvent (1, 74, 127) });
        run ({ juce::MidiMessage::controllerEvent (1, 74, 64) });
        std::printf ("agganciato e mosso a 64/127: OL=%d (atteso ~50)\n", p.paramInt ("op1_ol"));
        const int left = run ({ juce::MidiMessage::controllerEvent (1, 74, 60), juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) });
        std::printf ("eventi rimasti per il motore: %d (atteso 1, la nota)\n", left);
        // profilo Push 2: encoder relativi CC 71
        int push = -1;
        for (int i = 0; i < (int) mbstudio::midi::profiles().size(); ++i) if (mbstudio::midi::profiles()[(size_t) i].name.contains ("PUSH")) push = i;
        mm.applyProfile (push);
        const int before = p.paramInt ("op1_ol");
        run ({ juce::MidiMessage::controllerEvent (1, 71, 3) });
        const int up = p.paramInt ("op1_ol");
        run ({ juce::MidiMessage::controllerEvent (1, 71, 126) });
        std::printf ("push2 relativo: %d -> %d -> %d (attesi +3 poi -2)\n", before, up, p.paramInt ("op1_ol"));
        // profilo APC40: fader canale 3 CC 7 -> op3_r1
        int apc = -1;
        for (int i = 0; i < (int) mbstudio::midi::profiles().size(); ++i) if (mbstudio::midi::profiles()[(size_t) i].name.contains ("APC40")) apc = i;
        mm.applyProfile (apc);
        run ({ juce::MidiMessage::controllerEvent (3, 7, 127) });
        run ({ juce::MidiMessage::controllerEvent (3, 7, 0) });
        std::printf ("apc40 fader 3 (soft takeover da 99): r1=%d\n", p.paramInt ("op3_r1"));
        auto tree = mm.toValueTree();
        std::printf ("stato: %d assegnazioni, profilo '%s'\n", tree.getNumChildren(), tree["profile"].toString().toRawUTF8());
        std::fflush (stdout);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr) mainWindow->pluginHolder->savePluginState();
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            juce::Timer::callAfterDelay (100, [] { if (auto app = JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); });
        else
            quit();
    }

private:
    juce::ApplicationProperties appProperties;
    mbstudio::ui::HardwareLookAndFeel hardwareLook;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};

JUCE_CREATE_APPLICATION_DEFINE (ICX7StandaloneApp)
