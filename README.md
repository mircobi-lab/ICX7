# MBStudio91 ICX7

Sintetizzatore FM a 6 operatori in stile MBStudio91 (VST3, AU e applicazione autonoma, Mac e Windows).
Il suono viene dal motore **dx7-core** di Kenji Noguchi ([knoguchi/dx7](https://github.com/knoguchi/dx7)),
scritto in Rust, che emula il Yamaha DX7 con operatori in dominio logaritmico, inviluppi e LFO dell'MSFA
e i 32 algoritmi originali. ICX7 lo collega a JUCE tramite un piccolo ponte in C.

## Scaricare

Nella pagina **Releases** del repository trovi i file già compilati da GitHub:

| File | Cosa contiene | Dove va |
|---|---|---|
| ICX7-VST3-mac.zip | plugin VST3 (Apple Silicon + Intel) | ~/Library/Audio/Plug-Ins/VST3/ |
| ICX7-AU-mac.zip | plugin Audio Unit | ~/Library/Audio/Plug-Ins/Components/ |
| ICX7-App-mac.zip | applicazione autonoma | Applicazioni |
| ICX7-VST3-win.zip | plugin VST3 64 bit | C:\Program Files\Common Files\VST3\ |
| ICX7-App-win.zip | applicazione autonoma | dove vuoi |

Su Mac il plugin non è firmato da Apple; dopo la copia, nel Terminale:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"MBStudio91 ICX7.vst3" ~/Library/Audio/Plug-Ins/Components/"MBStudio91 ICX7.component" /Applications/"MBStudio91 ICX7.app"
```

## Cosa c'è

- 6 operatori con tutti i 155 parametri di una voce DX7, automatizzabili dal DAW (145 numerici + interruttori OP1-OP6)
- schema dell'algoritmo disegnato in tempo reale, con portanti, modulatori e anello di feedback
- grafico dell'inviluppo per ogni operatore e per il pitch EG
- 8 banchi di fabbrica (ROM1A … ROM4B, 256 voci) e 32 tasti di selezione come la membrana del DX7
- LOAD SYX: banchi da 32 voci o voci singole DX7; SAVE BANK e SAVE VOICE scrivono SysEx compatibili DX7/Dexed
- STORE: memorizza la voce modificata nel banco USER (salvato insieme al progetto del DAW)
- PANIC: spegne subito tutte le note (senza coda), sustain, pitch bend, modulazione, chorus e riverbero; lo fa anche il CC 120 All Sound Off
- ricezione MIDI di Program Change e di SysEx DX7 (banco o voce singola)
- mod wheel, breath (CC2), foot (CC4) e aftertouch sulla modulazione LFO; sustain; pitch bend (BEND 0-12 semitoni, anche via RPN 0)
- uscita con VOLUME, CHORUS stereo e REVERB (spenti per default: suono "puro" del motore)

Nota: come sul DX7, le modifiche alla voce valgono per le note suonate dopo la modifica.

## Come funziona dentro

```
engine/dx7-core    motore originale (Rust, Apache 2.0, copia del commit 72f0d5a; unica modifica: Synth::panic)
engine/icx7-ffi    ponte C (libreria statica icx7_engine)  ->  engine/include/icx7_engine.h
Source/            plugin JUCE: parametri, banchi, interfaccia
MBStudioUI/        kit grafico hardware MBStudio91
```

Il motore ha tabelle solo per 44,1 e 48 kHz, globali al processo: ICX7 lo fa girare sempre a 48 kHz
e ricampiona alla frequenza dell'host con un interpolatore sinc a finestra (verificato a 44,1 / 48 / 96 kHz).

## Compilare

La compilazione avviene su GitHub Actions a ogni push (vedi `.github/workflows/build.yml`), con controllo pluginval
a severità 10. Per compilare in locale servono CMake 3.22+, un compilatore C++17 e Rust (rustup):

```
cmake -S . -B build
cmake --build build --config Release
```

## Licenze

- ICX7 (codice in Source/): AGPL v3, vedi LICENSE.md (JUCE 9 è distribuito con AGPL v3).
- engine/dx7-core: Apache 2.0, © 2026 Kenji Noguchi, con parti derivate dall'MSFA di Google (Apache 2.0)
  e Freeverb (pubblico dominio). Vedi engine/dx7-core/LICENSE e NOTICE.
- I banchi ROM1A-ROM4B sono dati di parametri per il DX7, distribuiti dal progetto originale per interoperabilità.
  Yamaha e DX7 sono marchi di Yamaha Corporation; ICX7 non è affiliato né approvato da Yamaha.
- Font Bebas Neue e Antonio: SIL Open Font License 1.1 (MBStudioUI/fonts).
