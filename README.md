# RS Roar Ripper

Feedback-Suppressor · VST3 & Standalone · JUCE 9 · Windows  
Zero-latency adaptive feedback suppression for live vocals and speech.  
Null-Latenz, adaptiver Feedback-Suppressor für Live-Gesang und Sprache.

[Deutsch](#deutsch) · [English](#english)

---

## Deutsch

### Kurzbeschreibung
RS Roar Ripper erkennt akustische Rückkopplungen in Echtzeit und setzt
automatisch schmale Notch-Filter exakt auf die Störfrequenz – ohne messbare
Latenz und ohne das Nutzsignal breitbandig zu verfärben.

### Hauptmerkmale
- **Zero Latency** – reine IIR-Notch-Bank, kein Overlap-Add, kein Lookahead
- **Zweistufige Erkennung**: schneller 2048er-Screen plus Haupt-FFT (bis 32768),
  PNPR/PAPR-Analyse, Oberton-Test, spektraler Fluss, adaptive Bestätigung
- **24 Notch-Slots**: 6 feste (eingebrannt) + 18 dynamische (selbsttätig kommend/gehend)
  - fest: Q ≈ 14,4 (0,1 Oktaven)
  - dynamisch: Q 20, nach unten begrenzt auf 12 Hz absolute Bandbreite
  - maximale Gesamtabsenkung −42 dB pro Frequenz
- **Einpfeifen-Modus** (Ring-Out): Messung per Kippschalter; erkannte Notches
  werden fest eingebrannt und überleben Session und Preset
- **Goertzel-Tracker** (Hann-Fenster, 2048 Samples): hält Notches zwischen den
  FFT-Frames stabil
- **Analoge VU-Meter** (0 VU = −18 dBFS) mit echter Nadelballistik
- **RTA** −75…+5 dB mit kombinierter Filterkurve und Notch-Markern
- **HPF 36 dB/Okt, LPF 24 dB/Okt** mit klickfreiem Crossfade; Ein/Aus direkt
  im Drehregler integriert
- **Safari-Retro-GUI**: 19″-3HE-Panel mit Gebrauchsspuren, Knöpfe nach
  Fotovorlage, Tier-Piktogramme für die Analyserate, DE/EN-Umschalter
- **Standalone-Ansicht**: kompaktes Half-Rack mit symmetrischen Rändern
- **Bugreport-Overlay**: Snapshot plus Log-Auszug per Mailto und Zwischenablage
- XML-Presets inklusive fester Notches; klickfreies Mute und Bypass

### Betriebsarten
| Modus | Verhalten |
|---|---|
| Live | dynamische Notches, automatisches Kommen und Gehen |
| Einpfeifen | Messung ein/aus; erkannte Notches werden fest eingebrannt |
| Standalone | Half-Rack ohne RTA und ohne HPF/LPF; Speed bleibt sichtbar |

### Standardwerte
Sensitivity 70 % · Hold 150 ms · Release 1 s · Speed Cheetah · Modus Einpfeifen

### Bauen (Windows)
1. [JUCE 9.0.6](https://github.com/juce-framework/JUCE/releases/tag/9.0.6)
   entpacken und im Projucer als globalen Modulpfad hinterlegen.
2. `RoarRipper.jucer` öffnen → **Save Project**.
   Erzeugt `JuceLibraryCode/` und `Builds/` – beides liegt bewusst nicht im Repo.
3. Solution in Visual Studio 2022/2026 öffnen, Konfiguration **Release | x64**,
   **Neu erstellen**.
4. Grafik-Assets liegen in `Source/` und müssen im Projucer als Binary
   Resources eingebunden sein:
   `rs_logo_gelb_A4.jpg`, `knob.png`, `turtle.png`, `elephant.png`,
   `lion.png`, `cheetah.png`.
   Ohne Assets baut das Plugin trotzdem: in `PluginEditor.cpp` die Schalter
   `RS_USE_LOGO`, `RS_USE_KNOB_IMAGE`, `RS_USE_SPEED_ICONS` auf `0` setzen.

### Projektstruktur

Source/
PluginProcessor.h/.cpp Parameter, Busrouting, Tonfilter, State und Presets
PluginEditor.h/.cpp komplettes GUI: Panel, Meter, Regler, Overlays
dsp/
Constants.h Slot-Budgets, Bandbreiten, Rampen
Fft.h komplexe FFT und RealFft
HowlDetector.h Erkennung: Screen, Hauptanalyse, Track-Verwaltung
GoertzelTracker.h Inter-Frame-Hold-Detektion
BellBank.h latenzfreie Notch-Bank
FeedbackEngine.h Verkabelung, Slot-Verwaltung, Headroom-Begrenzung

### Lizenz
Dieses Projekt steht unter der **GNU Affero General Public License version 3
(AGPL-3.0-only)**.

Der vollständige Lizenztext liegt in der Datei `LICENSE`.

Du darfst die Software nutzen, verändern und weitergeben, solange abgeleitete
Werke ebenfalls unter den Bedingungen der AGPLv3 veröffentlicht werden.

Dieses Projekt verwendet das JUCE-Framework (<https://juce.com>) unter der
GNU Affero General Public License version 3.

Freiwillige Spenden, z. B. über den Kaffee-Button im Plugin, schalten keine
zusätzlichen Funktionen frei und ändern nichts an den AGPL-Rechten.

### Kontakt
Mail: info@roarsound.de · Kaffee: <https://paypal.me/RoarSound>

---

## English

### Overview
RS Roar Ripper detects acoustic feedback in real time and places narrow notch
filters exactly on the offending frequency – zero latency, without broadly
colouring the useful signal.

### Key features
- **Zero latency** – pure IIR notch bank, no overlap-add, no lookahead
- **Two-stage detection**: fast 2048-point screen plus main FFT (up to 32768),
  PNPR/PAPR analysis, harmonic test, spectral flux, adaptive confirmation
- **24 notch slots**: 6 fixed (burned-in) + 18 dynamic (self-releasing)
  - fixed: Q ≈ 14.4 (0.1 octave)
  - dynamic: Q 20, floored at 12 Hz absolute bandwidth
  - maximum total cut −42 dB per frequency
- **Ring-Out mode**: measurement toggle; detected notches are burned in and
  survive sessions and presets
- **Goertzel tracker** (Hann window, 2048 samples): keeps notches stable
  between FFT frames
- **Analogue VU meters** (0 VU = −18 dBFS) with true needle ballistics
- **RTA** −75…+5 dB with combined filter curve and notch markers
- **HPF 36 dB/oct, LPF 24 dB/oct** with click-free crossfade; on/off integrated
  into the rotary knob
- **Safari retro GUI**: 19″ 3U panel with wear, photo-based knobs, animal
  pictograms for the analysis rate, DE/EN switch
- **Standalone view**: compact half-rack with symmetric margins
- **Bug-report overlay**: snapshot plus log excerpt via mailto and clipboard
- XML presets including fixed notches; click-free mute and bypass

### Modes
| Mode | Behaviour |
|---|---|
| Live | dynamic notches, automatic attack and release |
| Ring Out | measurement on/off; detected notches are burned in |
| Standalone | half-rack without RTA and without HPF/LPF; speed stays visible |

### Defaults
Sensitivity 70 % · Hold 150 ms · Release 1 s · Speed Cheetah · Mode Ring Out

### Building (Windows)
1. Unpack [JUCE 9.0.6](https://github.com/juce-framework/JUCE/releases/tag/9.0.6)
   and set it as the global module path in the Projucer.
2. Open `RoarRipper.jucer` → **Save Project**.
   This generates `JuceLibraryCode/` and `Builds/` – deliberately not in the repo.
3. Open the solution in Visual Studio 2022/2026, configuration **Release | x64**,
   **Rebuild**.
4. Graphics assets live in `Source/` and must be added as binary resources in
   the Projucer:
   `rs_logo_gelb_A4.jpg`, `knob.png`, `turtle.png`, `elephant.png`,
   `lion.png`, `cheetah.png`.
   The plugin builds without them: set `RS_USE_LOGO`, `RS_USE_KNOB_IMAGE`,
   `RS_USE_SPEED_ICONS` to `0` at the top of `PluginEditor.cpp`.

### Layout

Source/
PluginProcessor.h/.cpp parameters, bus routing, tone filters, state/presets
PluginEditor.h/.cpp entire GUI: panel, meters, knobs, overlays
dsp/
Constants.h slot budgets, bandwidths, ramps
Fft.h complex FFT and RealFft
HowlDetector.h detection: screen, main analysis, track management
GoertzelTracker.h inter-frame hold detection
BellBank.h zero-latency notch bank
FeedbackEngine.h wiring, slot management, headroom limiting

### License
This project is licensed under the **GNU Affero General Public License version 3
(AGPL-3.0-only)**.

The full license text is included in the file `LICENSE`.

You may use, modify and redistribute this software under the terms of the AGPLv3.
Derived works must also be released under the AGPLv3.

This project uses the JUCE framework (<https://juce.com>) under the GNU Affero
General Public License version 3.

Voluntary donations, e.g. via the coffee button inside the plugin, do not unlock
additional functionality and do not change the AGPL rights.

### Contact
Mail: info@roarsound.de · Coffee: <https://paypal.me/RoarSound>

---

## Versionshistorie / Version history
| Version | Highlights |
|---|---|
| 1.4.9 | Safari-Retro-GUI, Half-Rack-Standalone, 6+18 Notches, Q 20 dynamisch, Hann-Goertzel, Sensitivity 70 % |
| 1.5.0 | Messung auch im Live-Modus (nur feste Notches aktiv), Modus "Statisch" (vorher "Einpfeifen"), Modusnamen DE/EN |
| 1.5.0 | AGPL-3.0-only release, Messung auch im Live-Modus, Modus "Statisch"/"Static", Schmalband-Gate, Q18 feste Notches |