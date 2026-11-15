/* Public inert defaults; local reviewed GUIDs required for execution
 * (fill both with your own ESP and optional payload partition GUIDs;
 * the payload partition is matched by GUID only, its partition number
 * is a wildcard). */
#pragma once
#define KOH_WINDOWS_ESP_GUID 0x62199451,0x6407,0x43bb,{0xbd,0x67,0x50,0xa6,0x2b,0x1e,0xab,0x20}
#define KOH_PAYLOAD_ESP_GUID 0x072d7ec5,0x5bba,0x422f,{0x8e,0xe4,0x80,0xaf,0x57,0x4f,0xc7,0x00}
/* Auto-confirm window (ms): after this wait with no key, Arm fires the attempt
 * by default (bias inverted 2026-10-27; Esc is the only skip of the day). */
#define KOH_AUTOCONFIRM_MS 5000
