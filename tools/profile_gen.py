#!/usr/bin/env python3
"""Generate a per-card Turing profile header + JSON from offline evidence.

Inputs (all offline; no GPU access):
  --rom PATH           complete 1 MiB PROM capture (optional; --rom-report may
                       replace it when only report JSON is available)
  --rom-report PATH    JSON output of tools/rom_analyze.py (alternative to --rom)
  --observations PATH  JSON of pre-driver tu104_diag readings + PCI identity
  --header PATH        output C header (default kernel/tu104_profile.h)
  --profile-json PATH  output profile JSON (optional)

Validates the descriptor shape against the two empirically-anchored profiles
(CMP50HX TU102, KohakUnlock Leadtek TU102) and refuses anything else. The
generated header keeps every tu104_core guard; it only supplies the pinned
constants. Never hand-edit hashes; re-run this tool instead.
"""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

CHIP_BY_BOOT0 = {0x162: "TU102", 0x164: "TU104", 0x166: "TU106"}
FRTS_SPACING = 0x200000


def parse_int(value):
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"non-numeric field: {value!r}")


def obs_int(obs, key):
    if key not in obs:
        raise ValueError(f"observations missing key: {key}")
    return parse_int(obs[key])


def load_report(path, rom_bytes=None):
    if rom_bytes is not None:
        from tools.rom_analyze import parse
        report = parse(rom_bytes)
    else:
        report = json.loads(path.read_text())
    for key in (
        "rom_sha256", "fwsec_descriptor_offset", "fwsec_descriptor_words",
        "fwsec_image_offset", "fwsec_bytes", "fwsec_sha256",
        "fwsec_interface_word", "mapper_offset", "command_offset", "command_size",
    ):
        if key not in report:
            raise ValueError(f"rom report missing key: {key}")
    return report


def validate(report, obs):
    fields = report["fwsec_descriptor_words"]
    if not isinstance(fields, list) or len(fields) != 15:
        raise ValueError("descriptor must hold 15 words")
    fields = [parse_int(v) for v in fields]
    if ((fields[0] >> 8) & 255) != 2 or not fields[0] & 1:
        raise ValueError("descriptor is not v2")
    if not fields[1] or fields[1] != fields[2] or fields[3]:
        raise ValueError("unsupported compressed/entry layout")
    if report["fwsec_bytes"] != fields[1]:
        raise ValueError("fwsec_bytes does not match StoredSize")
    if not fields[6] or fields[6] % 0x100 or fields[10] != fields[6]:
        raise ValueError("unexpected IMEM/DMEM layout")
    if fields[4] + 16 > fields[6] or fields[4] % 4:
        raise ValueError("interface offset outside DMEM")
    iface_word = parse_int(report["fwsec_interface_word"])
    if (iface_word & 0xFF) != 1 or ((iface_word >> 8) & 0xFF) != 4 or ((iface_word >> 16) & 0xFF) != 8:
        raise ValueError(f"unsupported interface word {iface_word:#x}")
    if iface_word >> 24 < 2:
        raise ValueError("too few interface entries")
    mapper = parse_int(report["mapper_offset"])
    cmd_off = parse_int(report["command_offset"])
    cmd_size = parse_int(report["command_size"])
    if not mapper % 4 == 0 or mapper + 64 > fields[6]:
        raise ValueError("mapper outside DMEM")
    if cmd_size < 48 or cmd_off % 4 or cmd_off + cmd_size > fields[6]:
        raise ValueError("command buffer outside DMEM")
    if mapper < cmd_off + cmd_size and cmd_off < mapper + 64:
        raise ValueError("command overlaps mapper")

    boot0 = obs_int(obs, "boot0")
    chip = (boot0 >> 20) & 0x1FF
    if chip not in CHIP_BY_BOOT0:
        raise ValueError(f"unknown BOOT0 chip {chip:#x}; only {sorted(CHIP_BY_BOOT0)} are anchored")
    if (boot0 >> 24) != 0x16:
        raise ValueError("BOOT0 is not a Turing architecture value")

    capacity = obs_int(obs, "capacity_register")
    fb = ((capacity >> 4) & 63) << ((capacity & 15) + 20)
    if capacity & 0x40000000:
        fb = fb // 16 * 15
    if not (1 << 30) <= fb <= (32 << 30):
        raise ValueError(f"decoded framebuffer {fb:#x} outside sane range")
    gb = (fb + (1 << 30) - 1) >> 30
    heap_mib = 8 + (((96 << 10) * gb + (1 << 20) - 1) >> 20) + 96
    frts = fb - FRTS_SPACING
    if frts <= 0 or frts % FRTS_SPACING or (frts >> 12) > 0xFFFFFFFF:
        raise ValueError(f"FRTS {frts:#x} not representable")
    if (obs_int(obs, "wpr2_low_init") << 8) <= fb:
        raise ValueError("initial WPR2 low is inside FB; not the empty-WPR pre-driver state")

    for key in ("workspace_register", "wpr2_low_init", "wpr2_high_init",
                "selector0_init", "selector1_init", "feature_word",
                "readout_word", "gfw_word"):
        obs_int(obs, key)  # presence/numeric check
    device_id = obs_int(obs, "device_id")
    sub_vendor = obs_int(obs, "subsystem_vendor")
    sub_device = obs_int(obs, "subsystem_device")
    if device_id >> 16 or sub_vendor >> 16 or sub_device >> 16:
        raise ValueError("PCI identity fields out of range")
    bdf = obs["bdf"]
    if not re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]", bdf):
        raise ValueError(f"invalid BDF: {bdf}")

    return {
        "fields": fields, "iface_word": iface_word, "mapper": mapper,
        "cmd_off": cmd_off, "cmd_size": cmd_size, "boot0": boot0,
        "chip": CHIP_BY_BOOT0[chip], "fb": fb, "gb": gb, "heap_mib": heap_mib,
        "frts": frts, "capacity": capacity, "device_id": device_id,
        "sub_vendor": sub_vendor, "sub_device": sub_device, "bdf": bdf,
        "rom_sha256": report["rom_sha256"],
        "fwsec_sha256": report["fwsec_sha256"],
        "fwsec_desc_off": parse_int(report["fwsec_descriptor_offset"]),
        "fwsec_img_off": parse_int(report["fwsec_image_offset"]),
        "workspace": parse_int(obs["workspace_register"]),
        "wpr2_low": parse_int(obs["wpr2_low_init"]),
        "wpr2_high": parse_int(obs["wpr2_high_init"]),
        "feature": parse_int(obs["feature_word"]),
        "readout": parse_int(obs["readout_word"]),
        "gfw": parse_int(obs["gfw_word"]),
        "sel0": parse_int(obs["selector0_init"]),
        "sel1": parse_int(obs["selector1_init"]),
    }


def header_lines(v, name):
    bus = int(v["bdf"].split(":")[1], 16)
    bdf = v["bdf"]
    desc = ", ".join(f"0x{w:x}u" for w in v["fields"])
    lines = [
        "/* SPDX-License-Identifier: GPL-2.0-only */",
        f"/* Generated by tools/profile_gen.py for profile {name}.",
        f" * Chip {v['chip']}; decoded FB {v['fb']:#x} from capacity reg "
        f"{v['capacity']:#x}; FRTS {v['frts']:#x}; heap {v['heap_mib']} MiB.",
        f" * ROM sha256 {v['rom_sha256']}; native FWSEC sha256 {v['fwsec_sha256']}.",
        " * Regenerate with tools/profile_gen.py; never hand-edit hashes.",
        " * Guards still refuse unless every pre-driver value matches THIS card.",
        " */",
        "#ifndef TU104_PROFILE_H",
        "#define TU104_PROFILE_H",
        "",
        f'#define KOH_PROFILE_NAME "{name}"',
        f"#define KOH_PCI_DEVICE 0x{v['device_id']:x}u",
        f"#define KOH_PCI_SUBSYSTEM_VENDOR 0x{v['sub_vendor']:x}u",
        f"#define KOH_PCI_SUBSYSTEM_DEVICE 0x{v['sub_device']:x}u",
        f"#define KOH_TARGET_BUS 0x{bus:02x}",
        f'#define KOH_TARGET_BDF "{bdf}"',
        "",
        f"#define KOH_FB_SIZE 0x{v['fb']:x}ULL",
        f"#define KOH_BOOT0_EXPECT 0x{v['boot0']:x}u",
        "",
        f"#define KOH_WORKSPACE_REG 0x{v['workspace']:x}u",
        f"#define KOH_WPR2_LOW_INIT 0x{v['wpr2_low']:x}u",
        f"#define KOH_WPR2_HIGH_INIT 0x{v['wpr2_high']:x}u",
        f"#define KOH_SELECTOR0_INIT 0x{v['sel0']:x}u",
        f"#define KOH_SELECTOR1_INIT 0x{v['sel1']:x}u",
        f"#define KOH_FEATURE_WORD 0x{v['feature']:x}u",
        f"#define KOH_READOUT_WORD 0x{v['readout']:x}u",
        f"#define KOH_GFW_WORD 0x{v['gfw']:x}u",
        "",
        f"#define KOH_FWSEC_SIZE 0x{v['fields'][1]:x}u",
        f'#define KOH_FWSEC_SHA256 "{v["fwsec_sha256"]}"',
        f"#define KOH_PROM_DESC_OFF 0x{v['fwsec_desc_off']:x}u",
        f"#define KOH_PROM_FWSEC_OFF 0x{v['fwsec_img_off']:x}u",
        "/* generated descriptor words */",
        "/* clang-format off */",
        f"#define KOH_DESC_WORDS {{{desc}}}",
        "/* clang-format on */",
        "",
        f"#define KOH_IFACE_WORD 0x{v['iface_word']:x}u",
        f"#define KOH_MAPPER_OFF 0x{v['mapper']:x}u",
        f"#define KOH_CMD_OFF 0x{v['cmd_off']:x}u",
        f"#define KOH_CMD_SIZE 0x{v['cmd_size']:x}u",
        "",
        f"#define KOH_FRTS 0x{v['frts']:x}ULL",
        f"#define KOH_HEAP_MIB {v['heap_mib']}u",
        "",
        "#define KOH_GENERIC_SIZE 0x300u",
        "#define KOH_BOOTER_SIZE 0xe700u",
        "#define KOH_SIGNATURE_SIZE 0xfa00u",
        "#define KOH_BOOT_CONTAINER_SIZE 0x20000u",
        "#define KOH_BOOT_SIZE 0x6000u",
        "#define KOH_ELF_SIZE 0x1bfadc8u",
        "",
        '#define KOH_GENERIC_SHA256 "f21f1cfbb8fffa8067685314367fc4c50f3f84dd99eda7c19db4292d9269b9d9"',
        '#define KOH_BOOTER_SHA256 "e0f0fc93da097b5b68e5af3adb58b2235ed1fad5fce0b23592d3895af44cb743"',
        '#define KOH_SIGNATURE_SHA256 "de7288bcfb80c2d4b259a59e823c926ba0a9adcab594219cc6802c94257ce271"',
        '#define KOH_BOOT_SHA256 "fa43239bcee7b97ca62f007cc68487560a39e19f74f3dde7486db3f98df8e471"',
        "",
        "#endif",
    ]
    return lines


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--rom", type=Path)
    p.add_argument("--rom-report", type=Path)
    p.add_argument("--observations", type=Path, required=True)
    p.add_argument("--profile-name", required=True)
    p.add_argument("--header", type=Path, required=True)
    p.add_argument("--profile-json", type=Path)
    args = p.parse_args()
    if (args.rom is None) == (args.rom_report is None):
        p.error("provide exactly one of --rom or --rom-report")
    rom_bytes = args.rom.read_bytes() if args.rom else None
    report = load_report(args.rom_report, rom_bytes)
    obs = json.loads(args.observations.read_text())
    v = validate(report, obs)
    out = "\n".join(header_lines(v, args.profile_name)) + "\n"
    with args.header.open("w") as f:
        f.write(out)
    if args.profile_json:
        profile = {
            "profile": args.profile_name,
            "execution_supported_generically": False,
            "hardware_validated": False,
            "chip": v["chip"],
            "pci_id": f"10de:{v['device_id']:04x}",
            "subsystem_id": f"{v['sub_vendor']:04x}:{v['sub_device']:04x}",
            "historical_target": v["bdf"],
            "framebuffer_bytes": v["fb"],
            "frts_offset": v["frts"],
            "heap_mib": v["heap_mib"],
            "rom_sha256": v["rom_sha256"],
            "fwsec_desc_off": v["fwsec_desc_off"],
            "assets": [
                {"name": "tu104-fwsec.bin", "bytes": v["fields"][1],
                 "sha256": v["fwsec_sha256"], "origin": "own native PROM; tools/rom_analyze.py"},
                {"name": "bl_gsp_tu102.bin", "bytes": 768,
                 "sha256": "f21f1cfbb8fffa8067685314367fc4c50f3f84dd99eda7c19db4292d9269b9d9",
                 "origin": "pinned CMP efi-unlock/blobs (TU102 origin; TU10x-family reuse unvalidated on this card)"},
                {"name": "booter_ucode_prod.bin", "bytes": 59136,
                 "sha256": "e0f0fc93da097b5b68e5af3adb58b2235ed1fad5fce0b23592d3895af44cb743",
                 "origin": "pinned CMP efi-unlock/blobs"},
                {"name": "v67_payload.bin", "bytes": 64000,
                 "sha256": "de7288bcfb80c2d4b259a59e823c926ba0a9adcab594219cc6802c94257ce271",
                 "origin": "pinned CMP efi-unlock/blobs"},
                {"name": "gsp_rm_boot_dbg.bin", "bytes": 131072,
                 "sha256": "fa43239bcee7b97ca62f007cc68487560a39e19f74f3dde7486db3f98df8e471",
                 "origin": "pinned CMP efi-unlock/blobs; only first 0x6000 used"},
            ],
        }
        with args.profile_json.open("w") as f:
            json.dump(profile, f, indent=2)
            f.write("\n")
    print(f"wrote {args.header}" + (f" and {args.profile_json}" if args.profile_json else ""))


if __name__ == "__main__":
    main()
