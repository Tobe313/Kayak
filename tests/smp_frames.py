#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Kayak's sample transfer (firmware/src/kayak_smp.h) for the host: writes tests/scripts/samples.txt, a script that
# asks the slots (INFO), sends a test sample into slot 1 (BEGIN, DATA, COMMIT) the way docs/samples/index.html does,
# picks it as SRC A (SMP1), plays it, then empties the slot (ERASE).   python3 tests/smp_frames.py
import math
import pathlib

HDR = [0x7D, 0x4B, 0x59]


def hexs(b):
    return "".join(f"{x:02X}" for x in b)


def pack7(data):                                # 7 bytes -> their top bits in a byte, then their low 7 bits
    out = []
    for i in range(0, len(data), 7):
        g = data[i:i + 7]
        out.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        out += [x & 0x7F for x in g]
    return out


def main():
    n, rate = 4096, 22050
    s = []
    for i in range(n):                          # C4 with two overtones, dying away
        t = i / rate
        v = (math.sin(2 * math.pi * 261.63 * t) + 0.5 * math.sin(2 * math.pi * 523.25 * t)
             + 0.25 * math.sin(2 * math.pi * 784.9 * t)) * math.exp(-t * 6)
        s.append(int(max(-32767, min(32767, v * 18000))))
    raw = b"".join(int(x).to_bytes(2, "little", signed=True) for x in s)
    name = b"TESTSMP\0"
    lines = ["# the sample transfer (written by tests/smp_frames.py): INFO, a sample into slot 1, SMP1 played, ERASE",
             f"200 sx {hexs(HDR + [0x01])}",
             f"300 sx {hexs(HDR + [0x02, 0, n & 0x7F, n >> 7 & 0x7F, n >> 14 & 0x7F, 60] + list(name))}"]
    t = 320
    for q in range(0, (len(raw) + 511) // 512):
        lines.append(f"{t} sx {hexs(HDR + [0x03, q & 0x7F, q >> 7 & 0x7F] + pack7(raw[q * 512:(q + 1) * 512]))}")
        t += 10
    lines += [f"{t} sx {hexs(HDR + [0x04])}",
              f"{t + 200} sx {hexs(HDR + [0x01])}",
              "2400 btn 6",                     # EDIT: GRN SRC A
              "2500 enc 2 7",                   # TYPE: DRONE + 7 = SMP1
              "3000 keys 0x80",                 # C4
              "3510 shot",
              "4400 keys 0",
              f"4800 sx {hexs(HDR + [0x05, 0])}",
              f"5000 sx {hexs(HDR + [0x01])}",
              "5500 end"]
    p = pathlib.Path(__file__).parent / "scripts" / "samples.txt"
    p.write_text("\n".join(lines) + "\n")
    print("->", p)


main()
