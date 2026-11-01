# Kayak

**A firmware for the M-VAVE FM-1 (and the M-Vave SMK-37), forked from [Dinghy](https://github.com/zvenson/dinghy) by zvenson.**

**[The user guide](docs/GUIDE.md)**: every control, page and value, with the sources.
**[Kayak samples](https://tobe313.github.io/Kayak/samples/)**: send your own samples into the FM-1 over USB (Chrome / Edge, Chrome on Android).

Package identity `FM-1_987` (SMK-37: `SMK-37 Pro_987`, `SMK-37 Elite_987`, `MKE-P37_987`; up to 0.41: `_946`), packages `kayak*.fwsc`.
Kayak is a granular synth: the GRAIN engine of [ChoralRoot FM-1](https://github.com/Quixotic7/ChoralRootFM1)
(its [docs/GRAIN.md](https://github.com/Quixotic7/ChoralRootFM1/blob/main/docs/GRAIN.md)), cut down to one part.
Two sources, A and B, are rendered by a small synth from four knobs each (no samples): TYPE (PLUCK DRONE BELL BREATH
CHIRP SWEEP VOWEL METAL), TONE, MOVE (the motion), SEED. Up to 16 grains, shared by 8 voices, read A and B mixed by MORPH.

Each voice has two layers: the grains, and an analog synth (two detuned oscillators SAW, SQR (a pulse of PW) or TRI,
a square an octave under, noise), each with its own filter, ADSR and LFO; the effects take the whole sound.

| control | |
| --- | --- |
| ALGORITHM | the layer the sound's functions show: **GRAIN** (turned left) or **ANALOG** (turned right) |
| EDIT | GRAIN: **GRN SRC A**, **GRN SRC B** (TYPE TONE MOVE SEED; TYPE past METAL: **SMP1 .. SMP8**, your own samples sent from the [web page](https://tobe313.github.io/Kayak/samples/)), **GRN GRAIN** (SIZE DENS POS SPRD), **GRN GRAIN+** (MORPH RAND PITCH SCAN); ANALOG: **ANA OSC** (WAVE DETUN OCT PW), **ANA OSC+** (SUB NOISE). The band shows the layer in front (the grains' material, or the analog layer's output over the last half second, after its filter and envelope) and the other layer dim behind it |
| SEL | the layer's filter: **GRN FILT** / **ANA FILT** (TYPE CUT RES DRIVE), **+** (KTRK SEM) |
| ENV, LFO | the layer's ADSR (ATK DEC SUS REL), its LFO (RATE WAVE DEPTH DEST) |
| GLO | **GLOBAL**: the two layers' levels (GRAIN, ANLG), the MIDI channel (OMNI or 1..16) and **CHORD**: the chord a key plays (- the one note, MAJ MIN 7 MAJ7 MIN7 SUS2 SUS4 DIM AUG POWER ADD9), out over MIDI too; **PERFORM**: what HOME's four knobs turn (by default BAL: the grains against the analog layer, CUTS: both filters' cutoffs, D.FB and DELAY: the delay's feedback and mix; or MORPH, POS, a filter, a level, an effect's mix, the arp's GATE RATCH CHNCE...) |
| ARP | a short press: the arpeggiator on (lit, as it was last) or off, and its pages: **ARP** (MODE DIV OCT GATE: UP DOWN UP/DN RANDOM PLAYED, 1/4 .. 1/32T, 1..4 octaves), **ARP+** (RATCH: the step cut into 1..4 hits, CHNCE: the chance it plays); held: **HOLD**, the notes stay after the keys are up (ARP blinks); then the keys transpose them |
| FX | a short press: the effects' pages and back: **PHASER** (RATE DEPTH FDBK MIX: a stereo phaser), **DELAY** (TIME FDBK TONE MIX), **REVERB** (SIZE DAMP PRE MIX), **DRIVE** (TYPE AMNT TONE MIX); held: **FX MIX**, the four effects' MIX on the four knobs |
| SELECT | the pages of the function its button opened (the button pressed again: the next one too) |
| SEQ | the step sequencer's screen (lit): one track of up to 64 steps, as a list of rows. SELECT goes from row to row, the four knobs set the row's values: **PATTERN** (TEMPO 40..240 BPM, STEPS, DIV, SWING), a **step** (NOTE: under C0 a rest; CHORD: the notes again from the root; VEL; GATE: past 99 % a TIE into the next step). SEQ again: **SEQ+**, the second page: a step's **RATCH** (1..4 hits) and **CHNCE** (the chance it plays). With the arpeggiator on (ARP), the steps' notes and chords play it. **REC** (lit) arms the keys: they write the row's step (keys held together, or GLO's CHORD: a chord of up to 4) and go to the next row; the top F# (F#5 at OCT 0, dimly lit) writes a rest. HOME or a function button leaves |
| transpose | while the sequencer runs, or the arp holds notes (HOLD), a key transposes what plays instead of playing it: from C4, +-2 octaves (HOME and SEQ show it, T+5); it goes back to 0 when both stop. While REC writes, the keys write steps; to hold another chord, HOLD off and on |
| PLAY | the sequencer from the top, or stop (lit while it runs, a blink a beat); MIDI START / STOP too, and a MIDI clock sets the tempo |
| HOME | the screen Kayak starts on, after the splash, a short press too. Two views, turned by **HOME again** or **SELECT**: **MAIN**, the layer, both layers' waves (the layer in front), an oscilloscope of the output and over it the four **performance knobs** (picked on GLO's PERFORM); **WAVES**, the whole screen for the waves alone: the grains, the analog layer's output, the output. The knobs turn the performance values in both; a function button goes back to the pages |
| KNOB 1..4 | the page's four values, in their units (ms, grains a second, %, Hz, semitones); the faster the turn, the bigger the step (up to 8 a detent) |
| PRESETS | the sound: 12 factory presets (ChoralRoot's CLOUD PAD, SHIMMER, BREATH PAD, FROZEN, STRUM, SWARM, ACID DUST, GLITCH; SUB PAD, ANA BASS, POLY SAW, PLUCK ARP with the analog layer), then the user presets; MIDI program change too |
| SAVE | save the sound: PRESETS picks the user slot (1..16), SAVE again writes it, HOME cancels |
| OCT- OCT+ | the octave |

The filter and most effects are [OberVave](https://github.com/zvenson/obervave)'s (zvenson): its voice filter (an
input stage driven into a tanh, then LP12 HP12 BP12 NOTCH and SEM (LP .. notch .. HP by the SEM knob) from a
saturating SVF, or LP24 from a ladder; KTRK makes the cutoff follow the note), and its ping-pong delay
(20 .. 550 ms here), Dattorro plate reverb and parallel drive (OVER DIST FUZZ CRUSH). The phaser is Kayak's: two chains of six
allpasses, left and right, swept a quarter turn apart, with feedback.
The LFOs (SINE TRI SAW SQR S&H, 0.05 .. 20 Hz): the grains' moves MORPH POS SIZE DENS SPRD PITCH or CUT, the
analog layer's PITCH CUT LEVEL or PW.
The arpeggiator and the step sequencer are OberVave's too, cut down to one sound and one track (`kayak_seq.h`); the arp
follows the sequencer's grid while it runs. The buttons that do something glow, and light fully while it is on. The sound being
played (marked `*` once changed), the octave, the channel, the function and the layer are kept in flash.

The screen shows the material the grains read (A on SRC A, B on SRC B, the mix elsewhere, with POS and SPRD) and every
grain sounding as a line moving across it. A source knob renders its buffer again in the main loop, a slice at a time,
and the new material takes the old one's place when it is whole. Code: `firmware/src/grain.c` (the player, the renders),
`grain_src.c` (ChoralRoot's generator, unchanged), `grain_dsp.h`, `firmware/gen/grain_tables.h`; the app `dinghy.c`.

## Build from source

Kayak is built like [Dinghy](https://github.com/zvenson/dinghy), the firmware it is forked from. You need Linux on
x86-64 (or macOS / Windows with Docker or WSL), Python 3 with Pillow, the JieLi toolchain and three files of the JieLi
AC79 SDK:

```sh
pip3 install Pillow mido python-rtmidi
tools/get_toolchain.sh                      # JieLi's clang for the chip, to ~/.jieli/toolchain
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13     https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
```

```sh
tests/run_host.sh tests/scripts/grain.txt   # on the computer: build/host/kayak.wav and the screens as PNG
./build.sh                                  # -> build/kayak.fwsc (BOARD=smk37 for the SMK-37)
python3 tools/fm1_install.py build/kayak.fwsc   # over USB; asks before it writes
sh tests/run_tests.sh                       # the tests the CI runs
```

If the FM-1 will not start: hold **OCT-** while switching it on (the USB rescue, only the updater) and install a
working firmware from there, or from the [FM-1 Firmware Hub](https://thefirmwarehub.com). Installing firmware is at
your own risk.

More: [docs/API.md](docs/API.md) (the platform), [docs/RULES.md](docs/RULES.md) (the chip's rules),
[docs/SMK37.md](docs/SMK37.md), [docs/MAKE-IT-YOURS.md](docs/MAKE-IT-YOURS.md).

## License

GPL-3.0-only (`LICENSE`): you may change and share Kayak, and whoever gets a firmware built from it gets its source.
Kayak is a modified version of Dinghy by zvenson; the platform is Copyright (C) 2026 Leo Kuroshita (@kurogedelic),
Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with SLOOP's changes (isod89); the SMK-37 layer
by M. Geisler (genkigenki); the loader's screen after Skiff (illuminus5); the GRAIN engine from ChoralRoot FM-1; the
filter, effects, arpeggiator and sequencer from OberVave (zvenson). Who made what, and the third-party parts:
[LICENSING.md](LICENSING.md).
