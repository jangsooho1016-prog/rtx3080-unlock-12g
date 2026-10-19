#!/usr/bin/env python3
"""Generate unlock/board_measured.h from a read-only probe run.

The probe (probe_app.c, built as its own BOOTX64.EFI) writes REPORT.TXT and
PROM.BIN to the USB. This script turns that output into the per-card values
the unlock app checks before it touches the GPU. It never guesses a value: if
the report is incomplete or the card is in an unexpected state it stops.

Usage:
  python tools/make_board_profile.py --report REPORT.TXT --prom PROM.BIN \
      --output unlock/board_measured.h
"""
import argparse
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
IDS_H = os.path.join(HERE, "..", "unlock", "board_ids.h")

REG_CHIP = 0x000000
REG_GFW_BOOT = 0x118234
REG_WPR2_HI = 0x1FA828
REG_FEAT_PLM = 0x823804
REG_SS0 = 0x82381C
REG_SS1 = 0x823820
REG_FB_MB = 0x1183A4
PROM_SIZE = 0x100000


class ProfileError(Exception):
    pass


def read_define(name):
    with open(IDS_H, encoding="utf-8") as f:
        m = re.search(r"#define\s+%s\s+(0x[0-9a-fA-F]+)U?" % name, f.read())
    if not m:
        raise ProfileError("%s not found in board_ids.h" % name)
    return int(m.group(1), 16)


def parse_report(text):
    """Return (pci_entries, gops, matching, target) parsed from REPORT.TXT."""
    pci, gops, matching, target = [], [], None, None
    section = None
    pending_cfg = pending_reg = None
    reg_status = None
    for raw in text.splitlines():
        line = raw.strip()
        if line in ("[NVIDIA PCI]", "[GOP]", "[TARGET]"):
            section = line
            if line == "[NVIDIA PCI]":
                pci.append({})
            elif line == "[GOP]":
                gops.append({"paths": []})
            else:
                target = {"config": {}, "regs": {}, "values": {}}
            continue
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if section == "[GOP]" and key == "path_node":
            gops[-1]["paths"].append(value.lower())
            continue
        try:
            num = int(value, 16)
        except ValueError:
            continue
        if key == "matching_cards":
            matching = num
            section = None
        elif section == "[NVIDIA PCI]":
            pci[-1][key] = num
        elif section == "[TARGET]":
            if key == "config_offset":
                pending_cfg = num
            elif key == "value" and pending_cfg is not None:
                target["config"][pending_cfg] = num
                pending_cfg = None
            elif key == "register":
                pending_reg, reg_status = num, None
            elif key == "status" and pending_reg is not None:
                reg_status = num
            elif key == "value" and pending_reg is not None:
                target["regs"][pending_reg] = (reg_status, num)
                pending_reg = None
            else:
                target["values"][key] = num
    return pci, gops, matching, target


def build_profile(report_text, prom_bytes=None, prom_sha256=None):
    pci, gops, matching, target = parse_report(report_text)
    board_id = read_define("GU_BOARD_ID")
    vendor = read_define("GU_BOARD_SUBSYSTEM_VENDOR")
    expected_fb = read_define("GU_BOARD_EXPECTED_FB_MB")
    warnings = []

    if matching != 1 or target is None:
        raise ProfileError("probe did not select exactly one target card (matching_cards=%r)"
                           % matching)
    if target["values"].get("collect_status", 1) != 0:
        raise ProfileError("probe collect_status is not success")
    cfg = target["config"]
    if len(cfg) != 64:
        raise ProfileError("config space dump incomplete (%d/64 dwords)" % len(cfg))
    if cfg[0] != board_id:
        raise ProfileError("card identity %#010x does not match target %#010x" % (cfg[0], board_id))
    if (cfg[0x2C] & 0xFFFF) != vendor:
        raise ProfileError("subsystem vendor %#06x is not MSI" % (cfg[0x2C] & 0xFFFF))

    entry = [e for e in pci if e.get("identity") == board_id]
    if len(entry) != 1 or (entry[0].get("segment"), entry[0].get("bus"),
                           entry[0].get("device"), entry[0].get("function")) != (0, 1, 0, 0):
        raise ProfileError("target GPU is not at 0000:01:00.0; the unlock app only supports that BDF")

    def reg(offset):
        if offset not in target["regs"]:
            raise ProfileError("register %#x missing from report" % offset)
        status, value = target["regs"][offset]
        if status != 0 or value == 0xFFFFFFFF or (value & 0xFFFF0000) == 0xBADF0000:
            raise ProfileError("register %#x was not readable (status=%r value=%#x)"
                               % (offset, status, value))
        return value

    if target["values"].get("memory_decode_enabled") != 1:
        raise ProfileError("memory decode was disabled during the probe; no register data")
    bar0 = cfg[0x10]
    bar0_len = target["values"].get("bar0_length")
    if bar0 & 0xF:
        raise ProfileError("BAR0 %#x is not a 32-bit non-prefetchable BAR" % bar0)
    if bar0_len != 0x1000000:
        raise ProfileError("BAR0 length %r is not 16 MiB" % bar0_len)

    chip = reg(REG_CHIP)
    if (chip >> 20) != 0xB72:
        raise ProfileError("BOOT0 %#x is not a GA102 chip" % chip)
    if chip != 0xB72000A1:
        warnings.append("BOOT0 %#x differs from the verified GA102 A1 (0xb72000a1)" % chip)

    gfw = reg(REG_GFW_BOOT)
    if (gfw & 0xFF) != 0xFF:
        warnings.append("GFW boot not complete in probe (%#x); probe again after a full power-off" % gfw)
    if reg(REG_WPR2_HI) >> 4:
        raise ProfileError("WPR2 already set: GPU was not cold. Power off fully and probe again")
    plm = reg(REG_FEAT_PLM)
    if plm != 0xFFFFFF8F:
        warnings.append("feature PLM %#x differs from verified 0xffffff8f; the V67 path may not apply" % plm)
    ss0 = reg(REG_SS0)
    ss1 = reg(REG_SS1)
    if ss0 == 0x88888888:
        raise ProfileError("SS0 already reads as unlocked; probe from a cold boot")
    if ss1 not in (2, 3):
        raise ProfileError("SS1 %#x is outside the verified values (2 or 3)" % ss1)
    if ss0 != 0x46254040:
        warnings.append("cold SS0 %#x differs from the 10GB card (0x46254040); expected for a different SKU, but untested" % ss0)
    fb_mb = reg(REG_FB_MB)
    if fb_mb != expected_fb:
        warnings.append("usable FB %d MiB differs from expected %d MiB" % (fb_mb, expected_fb))

    if prom_bytes is not None:
        if len(prom_bytes) != PROM_SIZE or prom_bytes[:2] != b"\x55\xaa":
            raise ProfileError("PROM.BIN must be exactly 1 MiB and start with 55aa")
        prom_hash = hashlib.sha256(prom_bytes).hexdigest()
    elif prom_sha256 and re.fullmatch(r"[0-9a-fA-F]{64}", prom_sha256):
        prom_hash = prom_sha256.lower()
    else:
        raise ProfileError("provide --prom PROM.BIN (or --prom-sha256)")

    intel_gop = any(g["paths"][:2] == ["02010c00d041030a00000000", "010106000002"]
                    for g in gops if g["paths"])
    if not intel_gop:
        warnings.append("no Intel iGPU GOP (PCI 00:02.0) found; the unlock app requires the screen on the Intel iGPU")

    values = [
        ("GU_BOARD_MEASURED_ID", cfg[0]),
        ("GU_BOARD_SUBSYSTEM", cfg[0x2C]),
        ("GU_BOARD_CLASS_REV", cfg[0x08]),
        ("GU_BOARD_CFG_C8", cfg[0xC8]),
        ("GU_BOARD_BAR0", bar0),
        ("GU_BOARD_BAR0_LEN", bar0_len),
        ("GU_BOARD_CHIP", chip),
        ("GU_BOARD_COLD_FEAT_PLM", plm),
        ("GU_BOARD_COLD_SS0", ss0),
        ("GU_BOARD_FB_MB", fb_mb),
    ]
    lines = ["#pragma once",
             "/* Generated by tools/make_board_profile.py from a read-only probe run.",
             " * Regenerate after any VBIOS update, BIOS update or hardware change. */",
             "#define GU_BOARD_MEASURED 1"]
    lines += ["#define %s 0x%08xU" % (k, v) for k, v in values]
    lines.append('#define GU_BOARD_PROM_SHA256 "%s"' % prom_hash)
    for w in warnings:
        lines.append("/* WARNING: %s */" % w)
    return "\n".join(lines) + "\n", warnings


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--report", required=True)
    p.add_argument("--prom")
    p.add_argument("--prom-sha256")
    p.add_argument("--output", default=os.path.join(HERE, "..", "unlock", "board_measured.h"))
    a = p.parse_args(argv)
    with open(a.report, encoding="ascii", errors="replace") as f:
        text = f.read()
    prom = None
    if a.prom:
        with open(a.prom, "rb") as f:
            prom = f.read()
    try:
        header, warnings = build_profile(text, prom, a.prom_sha256)
    except ProfileError as e:
        print("ERROR: %s" % e, file=sys.stderr)
        return 1
    with open(a.output, "w", encoding="ascii", newline="\n") as f:
        f.write(header)
    print(header)
    for w in warnings:
        print("WARNING: %s" % w, file=sys.stderr)
    print("Wrote %s" % a.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
