/* Public inert defaults; local reviewed GUIDs required for execution
 * (fill both with your own ESP and optional payload partition GUIDs;
 * the payload partition is matched by GUID only, its partition number
 * is a wildcard). */
#pragma once
#define KOH_WINDOWS_ESP_GUID 0,0,0,{0,0,0,0,0,0,0,0}
#define KOH_PAYLOAD_ESP_GUID 0,0,0,{0,0,0,0,0,0,0,0}
/* Auto-confirm window (ms): after this wait with no key, Arm fires the attempt
 * by default (bias inverted 2026-10-27; Esc is the only skip of the day). */
#define KOH_AUTOCONFIRM_MS 5000
