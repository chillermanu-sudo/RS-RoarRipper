# RS Roar Ripper

Feedback-Suppressor · VST3 & Standalone · JUCE 9 · Windows  
Zero-latency adaptive feedback suppression for live vocals and speech.  
Null-Latenz, adaptiver Feedback-Suppressor für Live-Gesang und Sprache.

[Deutsch](#deutsch) · [English](#english)

---

## Deutsch

### Kurzbeschreibung

RS Roar Ripper erkennt akustische Rückkopplungen in Echtzeit und setzt automatisch schmale Notch-Filter exakt auf die Störfrequenz – ohne messbare Latenz und ohne das Nutzsignal breitbandig zu verfärben.

### Hauptmerkmale

- **Zero Latency** – reine IIR-Notch-Bank, kein Overlap-Add, kein Lookahead
- **Zweistufige Erkennung**
  - schneller 2048er-Screen liefert Verdachtsfrequenzen und verkürzt damit die Bestätigung
  - Haupt-FFT (bis 32768) mit PNPR-/PAPR-Analyse, Schmalband-Gate, Oberton-Test, spektralem Fluss und adaptiver Bestätigung
- **24 Notch-Slots**: 6 feste (eingebrannt) + 18 dynamische (kommen und gehen selbsttätig)
  - fest: Breite 0,0795 Oktaven (Q ≈ 18)
  - dynamisch: Q 20, nach unten begrenzt auf 12 Hz absolute Bandbreite
  - maximale Gesamtabsenkung −42 dB pro Frequenz
- **Messung per Kippschalter** (Live und Statisch): Während der Messung sind die dynamischen Notches abgeschaltet, damit sie die Messung nicht verfälschen. Erkannte Notches werden fest eingebrannt und überleben Session und Preset. Nach dem Ende der Messung arbeiten die dynamischen Notches wieder normal.
- **Goertzel-Tracker** (Hann-Fenster, 2048 Samples): hält dynamische Notches zwischen den FFT-Frames stabil, solange am Eingang noch Energie an der Notch-Frequenz liegt
- **Analoge VU-Meter** (0 VU = −18 dBFS) mit echter Nadelballistik
- **RTA** −75…+5 dB mit kombinierter Filterkurve und Notch-Markern
- **HPF 36 dB/Okt, LPF 24 dB/Okt** mit klickfreiem Crossfade; Ein/Aus direkt im Drehregler integriert
- **Safari-Retro-GUI**: 19″-3HE-Panel mit Gebrauchsspuren, Knöpfe nach Fotovorlage, Tier-Piktogramme für die Analyserate, DE/EN-Umschalter
- **Standalone-Ansicht**: kompaktes Half-Rack mit symmetrischen Rändern
- **Bugreport-Overlay**: Snapshot plus Log-Auszug per Mailto und Zwischenablage
- **Update-Hinweis**: zeigt an, wenn auf GitHub eine neuere Version vorliegt (siehe unten)
- XML-Presets inklusive fester Notches (Import ersetzt die bisherigen festen Notches); klickfreies Mute und Bypass

### Betriebsarten

| Modus      | Verhalten                                                                                   |
| ---------- | ------------------------------------------------------------------------------------------- |
| Live       | dynamische Notches, automatisches Kommen und Gehen; Messung per Kippschalter möglich        |
| Statisch   | nur Messung: erkannte Notches werden fest eingebrannt, ohne Messung entstehen keine neuen   |
| Standalone | Half-Rack ohne RTA und ohne HPF/LPF; Speed bleibt sichtbar                                  |

### Standardwerte

Sensitivity 70 % · Hold 150 ms · Release 1 s · Speed Cheetah · Modus Statisch · HPF 130 Hz (an) · LPF 20 kHz (aus)

### Update-Hinweis

Beim Öffnen des Plugin-Fensters fragt ein Hintergrund-Thread bei GitHub (`api.github.com`) die neueste Version dieses Repositories ab. Gibt es eine neuere Version als die installierte, erscheint einmal pro DAW-Sitzung eine Meldung, und in der Kopfzeile wird ein grüner Update-Knopf angezeigt, der die Download-Seite öffnet.

- Es wird nichts installiert und nichts automatisch heruntergeladen.
- Es werden keine Nutzer- oder Projektdaten übertragen. Wie bei jeder Internetanfrage sieht GitHub die IP-Adresse des Rechners.
- Der Check blockiert die Oberfläche nie; ohne Internetverbindung passiert einfach nichts.
- Abschalten beim Bauen: im Projucer unter *Preprocessor Definitions* `RS_ENABLE_UPDATE_CHECK=0` eintragen.

### Bauen (Windows)

1. [JUCE 9.0.6](https://github.com/juce-framework/JUCE/releases/tag/9.0.6) entpacken und im Projucer als globalen Modulpfad hinterlegen.
2. `RoarRipper.jucer` öffnen → **Save Project**. Erzeugt `JuceLibraryCode/` und `Builds/` – beides liegt bewusst nicht im Repo.
3. Solution in Visual Studio 2022/2026 öffnen, Konfiguration **Release | x64**, **Neu erstellen**.
4. Grafik-Assets liegen in `Source/` und müssen im Projucer als Binary Resources eingebunden sein: `rs_logo_gelb_A4.jpg`, `knob.png`, `turtle.png`, `elephant.png`, `lion.png`, `cheetah.png`. Ohne Assets baut das Plugin trotzdem: in `PluginEditor.cpp` die Schalter `RS_USE_LOGO`, `RS_USE_KNOB_IMAGE`, `RS_USE_SPEED_ICONS` auf `0` setzen.

### Projektstruktur

```
Source/
  PluginProcessor.h/.cpp   Parameter, Busrouting, Tonfilter, State und Presets
  PluginEditor.h/.cpp      komplettes GUI: Panel, Meter, Regler, Overlays
  UpdateChecker.h          Hintergrund-Abfrage der neuesten Version auf GitHub
  UpdateVersion.h          Versionsvergleich (reines C++)
  dsp/
    Constants.h            Slot-Budgets, Bandbreiten, Rampen
    Fft.h                  komplexe FFT und RealFft
    HowlDetector.h         Erkennung: Screen, Hauptanalyse, Track-Verwaltung
    GoertzelTracker.h      Inter-Frame-Hold-Detektion
    BellBank.h             latenzfreie Notch-Bank
    FeedbackEngine.h       Verkabelung, Slot-Verwaltung, Headroom-Begrenzung
```

### Lizenz

Dieses Projekt steht unter der **GNU Affero General Public License version 3 (AGPL-3.0-only)**.

Der vollständige Lizenztext liegt in der Datei `LICENSE`.

Du darfst die Software nutzen, verändern und weitergeben, solange abgeleitete Werke ebenfalls unter den Bedingungen der AGPLv3 veröffentlicht werden.

Dieses Projekt verwendet das JUCE-Framework ([https://juce.com](https://juce.com)) unter der GNU Affero General Public License version 3.

Freiwillige Spenden, z. B. über den Kaffee-Button im Plugin, schalten keine zusätzlichen Funktionen frei und ändern nichts an den AGPL-Rechten.

### Kontakt

Mail: [info@roarsound.de](mailto:info@roarsound.de) · Kaffee: [https://paypal.me/RoarSound](https://paypal.me/RoarSound)

---

## English

### Overview

RS Roar Ripper detects acoustic feedback in real time and places narrow notch filters exactly on the offending frequency – zero latency, without broadly colouring the useful signal.

### Key features

- **Zero latency** – pure IIR notch bank, no overlap-add, no lookahead
- **Two-stage detection**
  - a fast 2048-point screen delivers suspect frequencies and thereby shortens the confirmation
  - main FFT (up to 32768) with PNPR/PAPR analysis, narrow-band gate, harmonic test, spectral flux and adaptive confirmation
- **24 notch slots**: 6 fixed (burned-in) + 18 dynamic (come and go on their own)
  - fixed: width 0.0795 octave (Q ≈ 18)
  - dynamic: Q 20, floored at 12 Hz absolute bandwidth
  - maximum total cut −42 dB per frequency
- **Measurement toggle** (Live and Static): while measuring, the dynamic notches are switched off so they cannot distort the measurement. Detected notches are burned in and survive sessions and presets. When the measurement ends, the dynamic notches resume normal operation.
- **Goertzel tracker** (Hann window, 2048 samples): keeps dynamic notches stable between FFT frames as long as there is still energy at the notch frequency at the input
- **Analogue VU meters** (0 VU = −18 dBFS) with true needle ballistics
- **RTA** −75…+5 dB with combined filter curve and notch markers
- **HPF 36 dB/oct, LPF 24 dB/oct** with click-free crossfade; on/off integrated into the rotary knob
- **Safari retro GUI**: 19″ 3U panel with wear, photo-based knobs, animal pictograms for the analysis rate, DE/EN switch
- **Standalone view**: compact half-rack with symmetric margins
- **Bug-report overlay**: snapshot plus log excerpt via mailto and clipboard
- **Update notice**: tells you when a newer version is available on GitHub (see below)
- XML presets including fixed notches (import replaces the current fixed notches); click-free mute and bypass

### Modes

| Mode       | Behaviour                                                                                       |
| ---------- | ----------------------------------------------------------------------------------------------- |
| Live       | dynamic notches, automatic attack and release; measurement toggle available                     |
| Static     | measurement only: detected notches are burned in, no new notches appear without a measurement   |
| Standalone | half-rack without RTA and without HPF/LPF; speed stays visible                                  |

### Defaults

Sensitivity 70 % · Hold 150 ms · Release 1 s · Speed Cheetah · Mode Static · HPF 130 Hz (on) · LPF 20 kHz (off)

### Update notice

When the plug-in window opens, a background thread asks GitHub (`api.github.com`) for the latest version of this repository. If a newer version than the installed one exists, a message appears once per DAW session and a green update button shows up in the top bar, which opens the download page.

- Nothing is installed and nothing is downloaded automatically.
- No user or project data is transmitted. As with any internet request, GitHub sees the computer's IP address.
- The check never blocks the interface; without an internet connection nothing happens.
- To disable it at build time: add `RS_ENABLE_UPDATE_CHECK=0` to the Projucer *Preprocessor Definitions*.

### Building (Windows)

1. Unpack [JUCE 9.0.6](https://github.com/juce-framework/JUCE/releases/tag/9.0.6) and set it as the global module path in the Projucer.
2. Open `RoarRipper.jucer` → **Save Project**. This generates `JuceLibraryCode/` and `Builds/` – deliberately not in the repo.
3. Open the solution in Visual Studio 2022/2026, configuration **Release | x64**, **Rebuild**.
4. Graphics assets live in `Source/` and must be added as binary resources in the Projucer: `rs_logo_gelb_A4.jpg`, `knob.png`, `turtle.png`, `elephant.png`, `lion.png`, `cheetah.png`. The plug-in builds without them: set `RS_USE_LOGO`, `RS_USE_KNOB_IMAGE`, `RS_USE_SPEED_ICONS` to `0` at the top of `PluginEditor.cpp`.

### Layout

```
Source/
  PluginProcessor.h/.cpp   parameters, bus routing, tone filters, state/presets
  PluginEditor.h/.cpp      entire GUI: panel, meters, knobs, overlays
  UpdateChecker.h          background query for the latest version on GitHub
  UpdateVersion.h          version comparison (plain C++)
  dsp/
    Constants.h            slot budgets, bandwidths, ramps
    Fft.h                  complex FFT and RealFft
    HowlDetector.h         detection: screen, main analysis, track management
    GoertzelTracker.h      inter-frame hold detection
    BellBank.h             zero-latency notch bank
    FeedbackEngine.h       wiring, slot management, headroom limiting
```

### License

This project is licensed under the **GNU Affero General Public License version 3 (AGPL-3.0-only)**.

The full license text is included in the file `LICENSE`.

You may use, modify and redistribute this software under the terms of the AGPLv3. Derived works must also be released under the AGPLv3.

This project uses the JUCE framework ([https://juce.com](https://juce.com)) under the GNU Affero General Public License version 3.

Voluntary donations, e.g. via the coffee button inside the plug-in, do not unlock additional functionality and do not change the AGPL rights.

### Contact

Mail: [info@roarsound.de](mailto:info@roarsound.de) · Coffee: [https://paypal.me/RoarSound](https://paypal.me/RoarSound)

---

## Versionshistorie / Version history

| Version | Highlights                                                                                                                                      |
| ------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| 1.4.9   | Safari-Retro-GUI, Half-Rack-Standalone, 6+18 Notches, Q 20 dynamisch, Hann-Goertzel, Sensitivity 70 %                                           |
| 1.5.0   | AGPL-3.0-only, Messung auch im Live-Modus (nur feste Notches aktiv), Modus „Statisch“/„Static“, Schmalband-Gate, Q 18 feste Notches            |
| 1.5.1   | Update-Hinweis mit Update-Knopf, Schmalband-Gate-Fix, Import ersetzt feste Notches, dynamische Notches während der Messung vollständig aus     |