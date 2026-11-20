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
