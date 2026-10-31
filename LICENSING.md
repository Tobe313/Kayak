# Licensing

Dinghy is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The SMK-37 board layer: `firmware/hal/smk_input.h`, `fm1_flash_map.h`, the SMK-37 parts of `fm1_cc.h`, `fm1_flash.h`, `fm1_lcd_hw.h`, `lcd.c`, `ota.c`, `bootguard.h`, `firmware/loader/`, `tools/fm1pkg_make.py`, `tools/fm1_install.py`, `tools/build.py`; `firmware/src/smk_wheel.h`; `tests/ota_test.c`, `ldr_test.c`, `install_test.py`, `smk_wheel_test.c`, `smk_pkg_test.py` | Copyright (C) 2026 M. Geisler (genkigenki/makkuro), from [genkigenki/sloop-fm1](https://github.com/genkigenki/sloop-fm1) branch `smk37` (the reference port of the [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit) |
| The TRS MIDI IN `firmware/src/midi_uart.c` | Felucca's, as sloopDX carries it, with the ring read by content from Felucca [Salt] 0.9-salt14 by ChanceTheMaker (found by keremimo) |
| The update loader's screen: `firmware/loader/ldr_screen.c`, `tests/ldr_screen_test.c` | Copyright (C) 2026 zvenson (from OberVave, the Firmware Hub's look); the idea, a loader that draws, its 5 x 7 letters and the order of things from [Skiff](https://github.com/illuminus5/skiff) by illuminus5, GPL-3.0 |
| From OberVave: the storage (blobs, slots), `LCD_IDLE`, the SMK-37 wiring of `platform.c`, `store.h`, the pad event queue in `smk_input.h`, `tests/smk_flash_test.c`, `tests/run_tests.sh`, the CI workflow | Copyright (C) 2026 zvenson |
| The app `firmware/src/dinghy.c`, the wiring `platform.c`, the host mode `tests/`, the docs | zvenson |
| Kayak: the fork and its changes | Tobe313 |
| The arpeggiator and the step sequencer `firmware/src/kayak_seq.h` (cut down to one sound and one track) | Copyright (C) 2026 zvenson ([OberVave](https://github.com/zvenson/obervave): `obv_engine.h`, `obv_seq.h`), with Tobe313's changes, GPL-3.0-only |
| The voice filter `firmware/src/obv_filter.h`, the effects `firmware/src/obv_fx.h` (chorus, delay, reverb, drive), the table `OBV_SVF_G` in `firmware/gen/grain_tables.h` | Copyright (C) 2026 zvenson ([OberVave](https://github.com/zvenson/obervave): `obv_dsp.h`, `obv_fx.h`, `tools/gen_tables.py`), GPL-3.0-only |
| GRAIN: the source generator `firmware/src/grain_src.c` (unchanged), the grain player in `firmware/src/grain.c` (cut down from its `eng_grain.c`) | Copyright (C) 2026 ChoralRoot FM-1 contributors ([ChoralRoot FM-1](https://github.com/Quixotic7/ChoralRootFM1), a Felucca fork), GPL-3.0-only |
| The DSP blocks `firmware/src/grain_dsp.h` and the tables `firmware/gen/grain_tables.h` (the formulas of its `tools/gen_tables.py`) | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca's `dsp.c`, as ChoralRoot carries it) |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## The logo

Dinghy's `assets/logo.svg` / `logo.png` carry the designburg signet: Copyright (C) designburg, all rights reserved, not
under the GPL. It marks Dinghy itself, so Kayak does not carry it.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE", "FM-1", "SMK-37" and "MKE-P37"
belong to their owners. Dinghy is not made or endorsed by them.
