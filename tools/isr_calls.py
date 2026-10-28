"""isr_calls.py - audit what the audio ISR calls, from the firmware disassembly.

    arm-none-eabi-objdump -d build/tonnetz.elf > build/tonnetz.dis
    python tools/isr_calls.py build/tonnetz.dis

Walks the call graph from TonnetzCard::ProcessSample and reports every
function reached, flagging any that live in FLASH (0x10...) - an XIP cache miss
inside the interrupt - and any soft-float, double or 64-bit helper
(__aeabi_f*, __aeabi_d*, __aeabi_l*, __aeabi_uldivmod...), which do not belong
in the audio path. Expected flash calls (the calibrated CV output, which
ComputerCard keeps in flash and main.cpp only calls on a change) are listed
separately.
"""
import re
import sys
from collections import defaultdict

ROOT = "_ZN11TonnetzCard13ProcessSampleEv"
EXPECTED_FLASH = ("MIDIToDAC", "CVOutMIDINote", "CVOut1MIDINote", "CVOut2MIDINote")
BAD_HELPERS = re.compile(r"__aeabi_(f|d|l|ul)|__aeabi_uldivmod|__aeabi_ldivmod|__aeabi_lmul|__.*sf|__.*df")


def parse(path):
    funcs = {}            # name -> address
    calls = defaultdict(set)
    cur = None
    head = re.compile(r"^([0-9a-f]{8}) <(.+)>:$")
    bl = re.compile(r"\tbl\s+[0-9a-f]+ <([^>+]+)")
    blx_target = re.compile(r"<([^>+]+)>")
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = head.match(line.strip("\n"))
            if m:
                cur = m.group(2)
                funcs[cur] = int(m.group(1), 16)
                continue
            if cur is None:
                continue
            m = bl.search(line)
            if m:
                calls[cur].add(m.group(1))
            elif "\tb.n" in line or "\tb\t" in line or "\tb.w" in line:
                # tail calls into another function
                t = blx_target.search(line)
                if t and t.group(1) != cur and not t.group(1).startswith(cur):
                    calls[cur].add(t.group(1))
    return funcs, calls


def main():
    paths = [a for a in sys.argv[1:] if not a.startswith("--")]
    funcs, calls = parse(paths[0] if paths else "build/tonnetz.dis")
    if ROOT not in funcs:
        print("root not found:", ROOT)
        return 1
    seen, stack = set(), [ROOT]
    while stack:
        f = stack.pop()
        if f in seen:
            continue
        # A long-branch veneer ("___X_veneer", in RAM) jumps to X, which may
        # well be in flash: follow it rather than stopping at the trampoline.
        if f.startswith("__") and f.endswith("_veneer"):
            seen.add(f)
            stack.append(f[2:-len("_veneer")])
            continue
        if f not in funcs:
            continue
        seen.add(f)
        stack.extend(calls.get(f, ()))
    seen = {f for f in seen if f in funcs}
    flash, bad, expected = [], [], []
    for f in sorted(seen):
        addr = funcs[f]
        if BAD_HELPERS.search(f):
            bad.append(f)
        if addr < 0x20000000:
            (expected if any(e in f for e in EXPECTED_FLASH) else flash).append((hex(addr), f))
    print(f"{len(seen)} functions reachable from the ISR")
    if "--list" in sys.argv:
        for f in sorted(seen, key=lambda x: funcs[x]):
            print("   ", hex(funcs[f]), f)
    print("in flash, expected (called only on a CV note change):")
    for a, f in expected:
        print("   ", a, f)
    print("in flash, UNEXPECTED:")
    for a, f in flash:
        print("   ", a, f)
    print("float / double / 64-bit helpers:", bad if bad else "none")
    return 1 if (flash or bad) else 0


if __name__ == "__main__":
    sys.exit(main())
