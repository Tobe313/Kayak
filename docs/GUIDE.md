# Kayak user guide

<!-- Keep this line in step with the release: the guide describes this version. -->
> **Applies to Kayak 1.3** (package identity `FM-1_987`) · [Releases](https://github.com/Tobe313/Kayak/releases) ·
> [Version history](#10-version-history)

## Summary

Kayak is a custom firmware for the **M-VAVE FM-1** (and builds for the M-Vave SMK-37). It turns the FM-1 into an
**8-voice granular synth** with a second, **analog-style layer** in every voice:

- **GRAIN layer**: two sources, A and B, are synthesised on the device, or are **your own samples** sent from a web
  page, and up to 16 grains read them, mixed by MORPH. The engine comes from [ChoralRoot FM-1](https://github.com/Quixotic7/ChoralRootFM1).
- **ANALOG layer**: two detuned oscillators (SAW, SQR, TRI), a sub-oscillator and noise.
- Each layer has its own **filter**, **ADSR envelope** and **LFO**.
- The **effects** (stereo phaser, delay, reverb, drive) work on the whole sound.
- The **arpeggiator** (with HOLD, ratchet and chance), the **step sequencer** (64 steps, chords, ratchet, chance; it
  played through the arp when it is on), **chords on one key** and **live transposition** all come from or build on
  [OberVave](https://github.com/zvenson/obervave).
- **HOME** shows both layers and the output, with four **performance knobs** you choose, or the waves alone.
- There are 12 factory presets and 16 user slots. Kayak plays USB-MIDI and TRS MIDI in, follows MIDI clock, and
  sends notes out over USB-MIDI.

Kayak is a fork of [Dinghy](https://github.com/zvenson/dinghy) (zvenson), the minimal FM-1 firmware built on
[Felucca](https://github.com/hugelton/Felucca)'s platform. It is free software under GPL-3.0-only.

---

## Contents

1. [Installing and leaving Kayak](#1-installing-and-leaving-kayak)
2. [The panel at a glance](#2-the-panel-at-a-glance)
3. [Screens and navigation](#3-screens-and-navigation)
   - 3.1 [HOME](#31-home)
   - 3.2 [Pages, functions and layers](#32-pages-functions-and-layers)
   - 3.3 [Buttons and LEDs](#33-buttons-and-leds)
4. [The sound engine](#4-the-sound-engine)
   - 4.1 [GRAIN layer: sources (EDIT)](#41-grain-layer-sources-edit)
   - 4.2 [GRAIN layer: grains (EDIT)](#42-grain-layer-grains-edit)
   - 4.3 [ANALOG layer: oscillators (EDIT)](#43-analog-layer-oscillators-edit)
   - 4.4 [Filter (SEL)](#44-filter-sel)
   - 4.5 [Envelope (ENV)](#45-envelope-env)
   - 4.6 [LFO (LFO)](#46-lfo-lfo)
   - 4.7 [Global (GLO)](#47-global-glo)
   - 4.8 [Performance knobs (PERFORM)](#48-performance-knobs-perform)
   - 4.9 [Your own samples (SMP)](#49-your-own-samples-smp)
   - 4.10 [Polyphony and load (VOICES)](#410-polyphony-and-load-voices)
5. [Effects (FX)](#5-effects-fx)
   - 5.1 [Phaser](#51-phaser)
   - 5.2 [Delay](#52-delay)
   - 5.3 [Reverb](#53-reverb)
   - 5.4 [Drive](#54-drive)
   - 5.5 [FX MIX](#55-fx-mix)
6. [Playing](#6-playing)
   - 6.1 [Keys, octave and velocity](#61-keys-octave-and-velocity)
   - 6.2 [Chords on one key](#62-chords-on-one-key)
   - 6.3 [Arpeggiator and HOLD (ARP)](#63-arpeggiator-and-hold-arp)
   - 6.4 [Transposing the sequence and the held arp](#64-transposing-the-sequence-and-the-held-arp)
7. [Step sequencer (SEQ)](#7-step-sequencer-seq)
   - 7.1 [The SEQ screen](#71-the-seq-screen)
   - 7.2 [PATTERN row](#72-pattern-row)
   - 7.3 [Step rows](#73-step-rows)
   - 7.4 [Second page (SEQ+): ratchet, chance, arp](#74-second-page-seq-ratchet-chance)
   - 7.5 [Recording with the keys (REC)](#75-recording-with-the-keys-rec)
   - 7.6 [Playing, MIDI clock and transport](#76-playing-midi-clock-and-transport)
8. [Presets and memory](#8-presets-and-memory)
   - 8.1 [Factory presets](#81-factory-presets)
   - 8.2 [Saving a user preset](#82-saving-a-user-preset)
   - 8.3 [What is kept after power-off](#83-what-is-kept-after-power-off)
9. [MIDI and the SMK-37](#9-midi-and-the-smk-37)
10. [Version history](#10-version-history)
11. [Sources and credits](#11-sources-and-credits)

---

## 1. Installing and leaving Kayak

| Way | How |
| --- | --- |
| **Firmware Hub** (recommended) | Open the [FM-1 Firmware Hub](https://thefirmwarehub.com) in Chrome or Edge, connect the FM-1 over USB and choose Kayak. |
| **Command line** | Download `kayak-X.Y.fwsc` from the [releases](https://github.com/Tobe313/Kayak/releases), then run `python3 tools/fm1_install.py kayak-X.Y.fwsc`. Use `--info` to see what the FM-1 runs. |
| **SMK-37** | Use `kayak-X.Y-smk37.fwsc`, `-smk37-elite` or `-smk37-mke` for your model. These builds pass the tests but have not been tried on a real SMK-37. |

- **Leaving Kayak:** install another firmware, or the official one, from the Hub. Kayak answers the Hub's updater like
  every firmware there.
- **Your own samples:** the [Kayak samples](https://tobe313.github.io/Kayak/samples/) page sends them over USB,
  no installation needed ([4.9](#49-your-own-samples-smp)).
- **If the FM-1 will not start:** hold **OCT-** while switching it on. This starts the USB rescue (only the updater,
  no sound), and you can install a working firmware from there.
- **Release packages are signed:** each one is built from source on GitHub and carries a build attestation from
  [zvenson/fm1-build](https://github.com/zvenson/fm1-build).

Installing firmware is at your own risk.

## 2. The panel at a glance

| Control | What it does |
| --- | --- |
| Keys (27) | Play notes (or a chord, or transpose: see [6](#6-playing)) |
| **ALGORITHM** | Chooses the layer the sound pages show: GRAIN (turn left) or ANALOG (turn right) |
| **EDIT** | Sources and grains, or the analog oscillators ([4.1](#41-grain-layer-sources-edit)–[4.3](#43-analog-layer-oscillators-edit)) |
| **SEL** | The layer's filter ([4.4](#44-filter-sel)) |
| **ENV** / **LFO** | The layer's envelope and LFO ([4.5](#45-envelope-env), [4.6](#46-lfo-lfo)) |
| **GLO** | Layer levels, MIDI channel, chord type ([4.7](#47-global-glo)); HOME's knobs ([4.8](#48-performance-knobs-perform)) |
| **ARP** | Short press: arpeggiator on or off. Long press: HOLD ([6.3](#63-arpeggiator-and-hold-arp)) |
| **FX** | Short press: effect pages. Long press: FX MIX ([5](#5-effects-fx)) |
| **SEQ** / **PLAY** / **REC** | Step sequencer; REC arms the keys to record ([7](#7-step-sequencer-seq)) |
| **SELECT** | The pages of the current function, the rows of the sequencer, or HOME's views |
| **KNOB 1..4** | The four values on screen; on HOME, the performance knobs |
| **HOME** | The HOME screen; pressed again, its next view ([3.1](#31-home)); cancels a save |
| **PRESETS** / **SAVE** | Choose a sound, save it ([8](#8-presets-and-memory)) |
| **OCT- / OCT+** | Octave, ±2 |
| **MASTER** | Output volume |

## 3. Screens and navigation

### 3.1 HOME

After the 1.5-second splash, Kayak starts on HOME. A short press on **HOME** also brings you here.

HOME has two views. **Press HOME again**, or **turn SELECT**, to go from one to the other:

| View | What it shows |
| --- | --- |
| **MAIN** | The sound's name, the layer, both layers' waves, the oscilloscope with the performance knobs over it, the status line (below) |
| **WAVES** | The waves alone, on the whole screen, no text: the grains' material with POS, SPRD and every grain sounding (top), the analog layer's output over the last half second (middle), the oscilloscope of the output (bottom) |

Kayak starts on MAIN. The MAIN view shows:

- the preset name, with `*` once you have changed it;
- the current layer (GRAIN or ANALOG);
- both layers' waves, the current layer in front and the other dim behind it (see [3.2](#32-pages-functions-and-layers));
- an oscilloscope of the output, with the four **performance knobs** over it: their names above, their values below;
- a status line with the arp mode, HOLD, the sequencer (PLAY or STOP), the transposition (`T+5`) and the tempo.

In both views the four knobs turn the values chosen on [PERFORM](#48-performance-knobs-perform) and stay on HOME
(WAVES does not show their values). A function button (EDIT, SEL, ENV, LFO, GLO, ARP, FX) takes you back to the
pages.

### 3.2 Pages, functions and layers

Each function button opens its own **pages**, four values per page:

- **SELECT** moves through the pages of the function you are in.
- Pressing the same function button again also moves to its next page.
- **ALGORITHM** switches EDIT, SEL, ENV and LFO between the GRAIN layer and the ANALOG layer.
- ARP, FX and GLO are the same for both layers.

| Function | GRAIN layer pages | ANALOG layer pages |
| --- | --- | --- |
| EDIT | GRN SRC A, GRN SRC B, GRN GRAIN, GRN GRAIN+ | ANA OSC, ANA OSC+ |
| SEL | GRN FILT, GRN FILT+ | ANA FILT, ANA FILT+ |
| ENV | GRN ENV | ANA ENV |
| LFO | GRN LFO | ANA LFO |
| ARP | ARP, ARP+ | ARP, ARP+ |
| FX | FX PHASER, FX DELAY, FX REVERB, FX DRIVE | (same) |
| FX (held) | FX MIX | (same) |
| GLO | GLOBAL, PERFORM, VOICES | GLOBAL, PERFORM, VOICES |

How the screens are laid out:

- Each page shows its name, a band with both layers' waves, and four cells (name, value, bar).
- The band puts the **current layer in front** and the other one dim behind it:
  - GRAIN layer: the material the grains read (A on GRN SRC A, B on GRN SRC B, the mix elsewhere), POS and SPRD, and
    every grain that sounds as a moving line; the analog layer's output dim orange behind;
  - ANALOG layer: the analog layer's **output over the last half second**, after its filter and envelope (newest on
    the right, scaled to its peak), in orange; the grains' material dim grey behind.
- The bottom lines show the octave, the MIDI channel, the transposition, MIDI input activity and the MIDI clock.
- The knobs speed up: a slow turn moves one step, a fast one up to eight steps per detent.
- Values that are lists (waves, types, modes) wrap around.

### 3.3 Buttons and LEDs

- A button that does something on the current screen **glows**.
- It lights **fully** while what it does is on.
- **ARP blinks** while HOLD is on.
- **PLAY** blinks on each beat while the sequencer runs, or with an incoming MIDI clock.
- The keys light while their notes sound.
- On the SEQ screen, **REC** glows, and lights while recording; the highest **F#** glows dimly then (it writes a
  rest).

## 4. The sound engine

Each of the 8 voices plays both layers at once. Their balance is set on [GLOBAL](#47-global-glo). The factory presets
use the GRAIN layer alone, except the last four ([8.1](#81-factory-presets)).

### 4.1 GRAIN layer: sources (EDIT)

**GRN SRC A** and **GRN SRC B** synthesise the two buffers the grains read. When you turn a source knob, the buffer is
rendered again in the background, and the new material replaces the old one once it is complete.

| Knob | Range | What it does |
| --- | --- | --- |
| TYPE | PLUCK, DRONE, BELL, BREATH, CHIRP, SWEEP, VOWEL, METAL, then SMP1 … SMP8 | The kind of material; SMP: one of your samples ([4.9](#49-your-own-samples-smp)) |
| TONE | 0–100 % | Brightness / timbre of the material |
| MOVE | 0–100 % | How much the material moves over time |
| SEED | 0–127 | A different variation of the same settings |

### 4.2 GRAIN layer: grains (EDIT)

| Page | Knob | Range | What it does |
| --- | --- | --- | --- |
| GRN GRAIN | SIZE | 10 ms – ~390 ms | Length of each grain |
| | DENS | ~1 – ~98 grains/s | How often grains start |
| | POS | 0–100 % | Where in the buffer grains start |
| | SPRD | 0–100 % | How widely start positions scatter around POS |
| GRN GRAIN+ | MORPH | 0–100 % | Mix of source A (0) and source B (100) |
| | RAND | 0–100 % | Random variation between grains |
| | PITCH | ±24 semitones | Grain pitch offset |
| | SCAN | −64 … +63 | Moves POS through the buffer over time (direction and speed) |

Up to 16 grains are shared by the 8 voices. See ChoralRoot's
[GRAIN documentation](https://github.com/Quixotic7/ChoralRootFM1/blob/main/docs/GRAIN.md) for the engine in depth.

### 4.3 ANALOG layer: oscillators (EDIT)

| Page | Knob | Range | What it does |
| --- | --- | --- | --- |
| ANA OSC | WAVE | SAW, SQR, TRI | Waveform of both oscillators |
| | DETUN | 0–30 cents | Detune between the two oscillators |
| | OCT | −3 … +2 octaves | Octave of the layer |
| | PW | 0–100 % | Pulse width (SQR) |
| ANA OSC+ | SUB | 0–100 % | Square sub-oscillator one octave down |
| | NOISE | 0–100 % | Noise level |

### 4.4 Filter (SEL)

This is OberVave's voice filter: an input stage driven into a tanh curve, then a saturating state-variable filter or
a ladder. Each layer has its own filter (GRN FILT / ANA FILT).

| Page | Knob | Range | What it does |
| --- | --- | --- | --- |
| FILT | TYPE | LP12, HP12, BP12, NOTCH, LP24 (ladder), SEM | Filter type |
| | CUT | 13 Hz – 19.9 kHz | Cutoff |
| | RES | 0–100 % | Resonance |
| | DRIVE | 0–100 % | Input drive |
| FILT+ | KTRK | 0–100 % | Key tracking: how much the cutoff follows the note |
| | SEM | 0–100 % | For the SEM type: morphs low-pass → notch → high-pass |

### 4.5 Envelope (ENV)

An ADSR envelope on the layer's level. GRN ENV is for the GRAIN layer, ANA ENV for the ANALOG layer.

| Knob | Range |
| --- | --- |
| ATK | 2 ms – ~3 s |
| DEC | 2 ms – ~3 s |
| SUS | 0–100 % |
| REL | 2 ms – ~3 s |

### 4.6 LFO (LFO)

One LFO per layer: GRN LFO and ANA LFO.

| Knob | Range | What it does |
| --- | --- | --- |
| RATE | 0.05 – 20 Hz | Speed |
| WAVE | SINE, TRI, SAW, SQR, S&H | Shape |
| DEPTH | 0–100 % | Amount |
| DEST | GRAIN: MORPH, POS, SIZE, DENS, SPRD, PITCH, CUT · ANALOG: PITCH, CUT, LEVEL, PW | Target |

### 4.7 Global (GLO)

| Knob | Range | What it does |
| --- | --- | --- |
| GRAIN | 0–100 % | Level of the GRAIN layer |
| ANLG | 0–100 % | Level of the ANALOG layer |
| MIDI | OMNI, 1–16 | MIDI channel for input and output |
| CHORD | −, MAJ, MIN, 7, MAJ7, MIN7, SUS2, SUS4, DIM, AUG, POWER, ADD9 | The chord one key plays ([6.2](#62-chords-on-one-key)) |

MIDI and CHORD belong to the device, not to the sound: they stay the same when you change preset.

### 4.8 Performance knobs (PERFORM)

PERFORM is GLO's second page (press GLO again, or turn SELECT). Its four knobs, KNOB1 to KNOB4, choose what each knob
turns on [HOME](#31-home). The choice belongs to the device and is kept after power-off; the values themselves are
part of the sound.

| Group | Choices |
| --- | --- |
| Grains | MORPH, POS, SIZE, DENS, SPRD, RAND, PITCH, SCAN |
| GRAIN layer | G.CUT, G.RES, G.ATK, G.REL, G.LFO (LFO depth), G.LEV (level) |
| ANALOG layer | A.CUT, A.RES, A.DET, A.PW, A.SUB, A.ATK, A.REL, A.LFO (LFO depth), A.LEV (level) |
| Effects | PHASE, DELAY, REVB, DRIVE (their MIX), P.FB (phaser feedback), D.FB (delay feedback), V.AMT (drive amount) |
| Arpeggiator | GATE, RATCH, CHNCE |
| Combined | BAL: the GRAIN and ANLG levels as one balance (left: the grains alone, middle: both full, right: the analog alone), shown as grains:analog (`50:50`); CUTS: both filters' cutoffs moved together, shown as their mean |

The defaults are BAL, CUTS, D.FB and DELAY: the balance of the two layers, both cutoffs, the delay's feedback and
mix.

### 4.9 Your own samples (SMP)

The FM-1 keeps **8 samples** (the SMK-37: 4) in its flash memory, each up to **0.74 s**, mono, 22 050 Hz: the size of
the buffer the grains read. They are sent from a web page over USB:

**[Kayak samples](https://tobe313.github.io/Kayak/samples/)**: Chrome or Edge on a computer, or Chrome on Android with a USB OTG cable
(Safari, iPhone / iPad and Firefox on Android cannot reach MIDI devices).

1. Connect the FM-1 over USB, open the page, press **Connect the FM-1** and allow MIDI ("system exclusive messages").
   The page reads the eight slots: their names, lengths and notes.
2. Choose one or more audio files (WAV, MP3, OGG, FLAC…). Each one is made mono at 22 050 Hz; drag on its wave to
   choose the 0.74 s that are kept (the page starts where the sound starts). **Play** listens to that part.
3. Give it a name (8 letters), its note (**ROOT**: played on that key it keeps its pitch; C4 by default) and a slot,
   then **Send**. The sound of the FM-1 stops for about half a second while the slot is written.
4. On the FM-1: EDIT, GRN SRC A (or B), turn **TYPE** past METAL to **SMP1** … **SMP8**.

With a sample, the source's other knobs change meaning:

| Knob | With a sample |
| --- | --- |
| TONE | Under 64: darker (a low-pass), from 64 up: as it is |
| MOVE | Where the buffer starts in the sample (0: its start; the sample loops to fill the buffer) |
| SEED | From 64 up: the sample backwards |

**Erase** on the page empties a slot. An empty slot is silent. A sound (preset) that uses SMP3 plays whatever is in
slot 3 at the time. With SRC B on a sample, its pitch follows SRC A's ROOT (one root for both, as for the rendered
sources).

### 4.10 Polyphony and load (VOICES)

VOICES is GLO's third page. Kayak plays up to 8 notes at once, each with both layers; a chord on a heavy sound (many
grains, both layers, the effects) can need more time than the chip has, and the sound crackles. These three cells
keep it in hand:

| Knob | Range | What it does |
| --- | --- | --- |
| VOICE | 1–8 | The most notes at once. Past it a new note **steals** a voice: the quietest one already let go, else the oldest one held |
| AUTO | ON, OFF | ON: when a block of sound takes over 85 % of its time, the quietest voice (a released one first) stops at once, at most one every 0.1 s, before the sound crackles |
| LOAD | (read only) | How much of its time the sound takes, the worst of the last half second. Under ~70 % is comfortable |

VOICE and AUTO belong to the device (kept after power-off, the same for every sound). If a sound crackles: watch
LOAD while you play it, lower VOICE (6 or 4), or make the sound lighter (fewer grains: DENS, SIZE; the analog layer
off with ANLG at 0 when it is not used).

## 5. Effects (FX)

The phaser is Kayak's; the delay, reverb and drive are OberVave's. They work on the whole output in this order:
drive (in parallel), phaser, delay, reverb.

- **Short press on FX:** opens the effect pages. Press again to go back to the sound pages.
- **Long press (½ s):** opens FX MIX.

A soft limiter at the output keeps loud chords from clipping.

### 5.1 Phaser

| RATE | DEPTH | FDBK | MIX |
| --- | --- | --- | --- |
| Speed of the sweep, 0.05 – 4 Hz | Width of the sweep, up to 6 octaves above 160 Hz | Feedback: sharper, more resonant notches | Amount: at 100 % the deepest notches |

The phaser is stereo: two chains of six allpass stages, swept by the same triangle LFO a quarter turn apart on the
left and the right. It replaced OberVave's chorus in 1.1; a sound saved before keeps its RATE, DEPTH and MIX values,
now on the phaser.

### 5.2 Delay

| TIME | FDBK | TONE | MIX |
| --- | --- | --- | --- |
| 20 – 550 ms | Feedback | Brightness of the repeats | Wet amount |

The delay is ping-pong.

### 5.3 Reverb

| SIZE | DAMP | PRE | MIX |
| --- | --- | --- | --- |
| Room size | High-frequency damping | Pre-delay | Wet amount |

This is a Dattorro plate reverb.

### 5.4 Drive

| TYPE | AMNT | TONE | MIX |
| --- | --- | --- | --- |
| OVER, DIST, FUZZ, CRUSH | Amount | Tone | Wet amount |

The drive runs in parallel with the dry sound.

### 5.5 FX MIX

Hold **FX** to put the four effects' MIX on the four knobs: PHASE, DELAY, REVB, DRIVE.

## 6. Playing

### 6.1 Keys, octave and velocity

- The 27 keys go from F3 to G5 at octave 0. **OCT- / OCT+** shift them by ±2 octaves.
- The FM-1's keys play at velocity 100. The SMK-37's keys are velocity-sensitive.
- The notes also go out over USB-MIDI on the MIDI channel (channel 1 when MIDI is set to OMNI).

### 6.2 Chords on one key

Set **CHORD** on [GLOBAL](#47-global-glo). Each key then plays the whole chord built on it. This also works for the
SMK-37's pads and outer keys, and the chord notes go out over MIDI too.

| CHORD | Intervals (semitones) |
| --- | --- |
| − | the single note |
| MAJ | 0 4 7 |
| MIN | 0 3 7 |
| 7 | 0 4 7 10 |
| MAJ7 | 0 4 7 11 |
| MIN7 | 0 3 7 10 |
| SUS2 | 0 2 7 |
| SUS4 | 0 5 7 |
| DIM | 0 3 6 |
| AUG | 0 4 8 |
| POWER | 0 7 12 |
| ADD9 | 0 4 7 14 |

### 6.3 Arpeggiator and HOLD (ARP)

**Short press on ARP:** turns the arpeggiator on (with the last mode used) or off, and opens its pages. SELECT turns
to the second page, ARP+.

| Page | Knob | Range | What it does |
| --- | --- | --- | --- |
| ARP | MODE | OFF, UP, DOWN, UP/DN, RANDOM, PLAYED | Order of the notes |
| | DIV | 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T | Step length |
| | OCT | 1–4 octaves | Range |
| | GATE | 0–100 % | Note length within the step (or within each ratchet hit) |
| ARP+ | RATCH | 1–4 | Ratchet: each step is cut into this many repeated hits of its note |
| | CHNCE | 0–100 % | Chance: how likely each step is to sound (it still moves on when silent) |

While the arpeggiator is on, it also plays the sequencer's steps: each step's note or chord is arpeggiated, as if
its keys were held ([7.6](#76-playing-midi-clock-and-transport)).

**Long press on ARP (½ s):** turns **HOLD** on or off, and ARP blinks while it is on.

- With HOLD on, the notes keep playing after you release the keys.
- Once all keys are up, new keys **transpose** the held notes instead of replacing them
  ([6.4](#64-transposing-the-sequence-and-the-held-arp)).
- To hold a different chord, turn HOLD off and on again.
- Turning HOLD off releases the held notes.

While the sequencer runs, the arp follows its grid.

### 6.4 Transposing the sequence and the held arp

A key transposes instead of playing when either of these is true:

- the **sequencer is running**;
- the **arpeggiator is on with HOLD** and holding notes.

How it works:

- The amount is counted from **C4**, up to ±2 octaves: pressing D4 shifts up 2 semitones, A3 down 3.
- The shift stays after you release the key, and appears as `T+2` on HOME, on the page status line and on the SEQ
  screen.
- It goes back to 0 when the sequencer stops and the arp no longer holds notes.
- While REC is on (SEQ screen), the keys write steps; they never transpose then.

## 7. Step sequencer (SEQ)

The sequencer has one track of up to **64 steps**. Each step holds up to 4 notes (a chord), a velocity and a gate (or
a tie).

### 7.1 The SEQ screen

Press **SEQ** to open the sequencer screen (SEQ lights). Press it again to switch between its two pages, SEQ and
**SEQ+**. Leave with HOME or a function button (EDIT, SEL, ENV, LFO, GLO, ARP, FX).

- The screen is a **list of rows**: the PATTERN row first, then one row per step.
- **SELECT** moves from row to row, and the four knobs set the selected row's values.
- Three rows are visible: the selected one (highlighted, with an orange edge) and its neighbours.
- A step row shows its number, its notes (`C4 MAJ7`, `C4 +2` for keys held together, `REST`), then its velocity and
  gate (on SEQ+: its ratchet and chance, `x2 50%`).
- An orange mark shows the step being played.
- The title shows `SEQ` or `SEQ+`, and `REC` in orange while recording.
- The bottom lines show PLAY with the current step (or STOP), the tempo, the transposition, the selected row, and
  what REC does (`REC: RECORD`, or `TOP F#: REST` while recording).

### 7.2 PATTERN row

| TEMPO | STEPS | DIV | SWING |
| --- | --- | --- | --- |
| 40–240 BPM | 1–64 | 1/4 … 1/32T | 50–75 % |

### 7.3 Step rows

| Knob | What it does |
| --- | --- |
| NOTE | The root note. A chord moves along with it. Turning right from a rest starts at C4; going below C0 makes a rest. |
| CHORD | −, MAJ … ADD9: rebuilds the step's notes from its root. `KEYS` means a chord entered with several keys. |
| VEL | Velocity, 1–127 |
| GATE | Note length within the step. Past 99 % it becomes **TIE**: the notes hold into the next step. |

### 7.4 Second page (SEQ+): ratchet, chance

The PATTERN row shows the same values as on SEQ.

| Row | Knob | Range | What it does |
| --- | --- | --- | --- |
| Step | RATCH | x1–x4 | Ratchet: the step is cut into this many repeated hits, each held for the step's GATE of its part |
| Step | CHNCE | 0–100 % | Chance: how likely the step is to sound each time round |

### 7.5 Recording with the keys (REC)

On the SEQ screen, **REC** arms recording (REC lights, the title shows `REC`). Without it, the keys play normally
(or transpose while the sequencer runs).

While REC is on:

- A key writes into the selected step and plays the note.
- Keys held together make a chord of up to 4 notes. With a CHORD set on GLOBAL, one key writes that chord.
- When all keys are released, the selection moves to the next row.
- The **highest F#** of the keyboard (F#5 at octave 0, whatever the octave) writes a **rest** and moves on. It glows
  dimly while REC is on.
- The step keeps its gate, ratchet and chance when you write over it.

REC turns off when you leave the SEQ screen.

### 7.6 Playing, MIDI clock and transport

- **PLAY** starts the sequence from the top, or stops it.
- MIDI **START / CONTINUE / STOP** do the same.
- An incoming **MIDI clock** (40–240 BPM) sets the tempo.
- With the **arpeggiator on** (ARP lit), the steps play through it: each step's note or chord is arpeggiated (its
  MODE, DIV, OCT, GATE, RATCH, CHNCE) for the length of the step's gate.
- The pattern is saved to flash while the sequencer is stopped.

## 8. Presets and memory

### 8.1 Factory presets

Turn **PRESETS** to step through the 12 factory presets, then the user presets you have saved. MIDI program change
also selects a preset.

| # | Name | Layer | Character |
| --- | --- | --- | --- |
| 1 | CLOUD PAD | GRAIN | Drone and bell pad, slow attack, phaser, delay and reverb |
| 2 | SHIMMER | GRAIN | Bells an octave up, dense grains, long reverb |
| 3 | BREATH PAD | GRAIN | Breath and vowel, SEM filter, LFO on POS |
| 4 | FROZEN | GRAIN | Long grains, still vowel / breath texture, big reverb |
| 5 | STRUM | GRAIN | Pluck and metal, short envelope, arp UP over 2 octaves |
| 6 | SWARM | GRAIN | Chirps and sweeps, many random grains, band-pass, S&H on the cutoff |
| 7 | ACID DUST | GRAIN | Tiny dense grains, resonant LP24, LFO on the cutoff, distortion |
| 8 | GLITCH | GRAIN | Metal and drone, scattered grains, high-pass, bit crush |
| 9 | SUB PAD | GRAIN + ANALOG | CLOUD PAD over a square sub an octave down |
| 10 | ANA BASS | ANALOG | Saw bass with sub, resonant LP24 |
| 11 | POLY SAW | GRAIN + ANALOG | Detuned saws over bells, LFO on the cutoff |
| 12 | PLUCK ARP | GRAIN + ANALOG | Square pluck with the arp UP over 2 octaves, delay |

Presets 1–8 are ChoralRoot's GRAIN sounds, with Kayak's filter, envelope, LFO and effects.

### 8.2 Saving a user preset

1. Press **SAVE**. The save screen opens on the current user slot, or on the first empty one.
2. Turn **PRESETS** to choose a slot (1–16). The screen tells you if the slot is already used.
3. Press **SAVE** again to write the preset, or **HOME** to cancel.

### 8.3 What is kept after power-off

- **Settings:** the current sound, the octave, the MIDI channel, the chord, the PERFORM choices, the function and
  the layer. They are saved 2 s after a change, while nothing is playing, as a journal in flash so the LEDs do not
  flicker.
- **Sequence:** saved while the sequencer is stopped.
- **User presets:** saved when you save them.
- **Samples:** written when the web page sends them ([4.9](#49-your-own-samples-smp)). They stay through Kayak's
  updates; another firmware installed meanwhile may use the same part of the flash and erase them.

## 9. MIDI and the SMK-37

| | |
| --- | --- |
| **MIDI in** | USB-MIDI and the TRS jack: notes on the chosen channel (or OMNI), program change, clock, START / CONTINUE / STOP. The page status line shows `USB IN` / `TRS IN` and the clock tempo. |
| **MIDI out** | USB-MIDI: the notes (and chords) played on the keys and pads. |
| **USB audio** | Inherited from the Dinghy / Felucca platform. |
| **SMK-37** | Velocity-sensitive keys, including the outer keys. The 16 pads play the white keys F3–G5 (bottom row: white keys 1–8, top row: 9–16) and glow while they sound. The pitch wheel bends ±2 semitones. See [SMK37.md](SMK37.md). |

## 10. Version history

<!-- Add the newest version on top. Update the "Applies to" line at the top and the sections that changed. -->

| Version | Changes |
| --- | --- |
| **1.3** | GLO › VOICES: the polyphony (VOICE 1–8, note stealing past it), AUTO (a voice stops when the audio runs late, so it does not crackle) and LOAD (the audio's load). |
| **1.2** | Your own samples: a web page ([Kayak samples](https://tobe313.github.io/Kayak/samples/)) sends up to 8 samples of 0.74 s into the FM-1 over USB-MIDI; SRC A / B TYPE SMP1 … SMP8 plays them. |
| **1.1** | OberVave's chorus replaced by a stereo phaser (FX PHASER: RATE DEPTH FDBK MIX). |
| **1.0** | HOME's two views, MAIN and WAVES (the waves alone on the whole screen), turned by HOME again or SELECT. SELECT no longer leaves HOME. |
| **0.9** | REC arms the sequencer's recording; the highest F# writes a rest. Ratchet and chance for the arp (ARP+) and the steps (SEQ+). With the arp on, the sequencer's steps play through it. GRAIN pages named GRN. Both layers drawn, the current one in front. HOME: four performance knobs chosen on GLO › PERFORM (by default the layers' balance, both cutoffs, the delay's feedback and mix), shown over the scope. ANA pages: the analog output over half a second. User presets, settings and the sequence of 0.8 are kept. |
| **0.8** | Chords on one key (GLO › CHORD). Keys transpose the running sequence and the held arp. Sequencer redesigned as rows (PATTERN; per step NOTE, CHORD, VEL, GATE). Soft limiter on the output. |
| **0.7** | Two layers per voice (GRAIN and ANALOG) chosen with ALGORITHM, each with its own filter, ADSR and LFO. Effects on the whole sound. HOLD (long press ARP). FX MIX (long press FX). SELECT moves through the pages of the current function. 4 new presets (SUB PAD, ANA BASS, POLY SAW, PLUCK ARP). |
| **0.6** | Package identity `FM-1_987`, signed releases for the Firmware Hub. Larger UI font, new splash. No LED jump on page buttons (settings saved as a journal). |
| **0.5** | Starts on HOME. EDIT and SEL page groups. An analog bass on FX. |
| **0.4** | OberVave's arpeggiator and step sequencer. HOME oscilloscope. Glowing buttons. ADSR page (0.41). |
| **0.3** | OberVave's voice filter and effects (FX pages). |
| **0.1** | First release: ChoralRoot's GRAIN engine on Dinghy, splash screen. |

The full release notes and packages are on the [releases page](https://github.com/Tobe313/Kayak/releases).

## 11. Sources and credits

| Part | Source | Authors |
| --- | --- | --- |
| Base firmware, app structure, host tests | [Dinghy](https://github.com/zvenson/dinghy) | zvenson |
| Platform (boot, USB, MIDI, updates, audio, storage) | [Felucca](https://github.com/hugelton/Felucca), via [SLOOP](https://github.com/isod89/sloop-fm1) | Leo Kuroshita (Hügelton Instruments), isod89 |
| SMK-37 port | [genkigenki/sloop-fm1](https://github.com/genkigenki/sloop-fm1) (`smk37`), [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) | M. Geisler (genkigenki) |
| GRAIN engine and source generator | [ChoralRoot FM-1](https://github.com/Quixotic7/ChoralRootFM1) ([GRAIN.md](https://github.com/Quixotic7/ChoralRootFM1/blob/main/docs/GRAIN.md)) | ChoralRoot FM-1 contributors |
| Filter, delay, reverb, drive, arpeggiator, sequencer | [OberVave](https://github.com/zvenson/obervave) | zvenson |
| Update loader screen idea | [Skiff](https://github.com/illuminus5/skiff) | illuminus5 |
| Signed builds | [zvenson/fm1-build](https://github.com/zvenson/fm1-build), [FM-1 Firmware Hub](https://thefirmwarehub.com) | zvenson |
| Font | Terminus (SIL OFL 1.1) | Dimitar Zhekov |
| Kayak: the fork, two layers, chords, transposition, sequencer UI, performance knobs, phaser | [Tobe313/Kayak](https://github.com/Tobe313/Kayak) | Tobe313 |

The full licensing is in [LICENSING.md](../LICENSING.md). Kayak is GPL-3.0-only. "M-VAVE", "FM-1" and "SMK-37" belong
to their owners; Kayak is not made or endorsed by them.

For developers: the platform API is in [API.md](API.md), the chip's rules in [RULES.md](RULES.md), and how to make
your own firmware in [MAKE-IT-YOURS.md](MAKE-IT-YOURS.md).

<!-- Maintaining this guide: on each release, (1) update "Applies to", (2) add a row to the Version history,
(3) edit the sections whose controls changed; the knob tables mirror PAGES[] in firmware/src/dinghy.c and GQ[] in
firmware/src/grain.c (names, ranges), the chords CHORDS[] in firmware/src/kayak_seq.h. -->
