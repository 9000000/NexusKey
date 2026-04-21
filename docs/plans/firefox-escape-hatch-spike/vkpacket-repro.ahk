; NexusKey Firefox <img>-space-eater bug — raw VK_PACKET repro
; ------------------------------------------------------------
; Purpose: determine if the bug is in Firefox's VK_PACKET handling
;          OR in NexusKey's synth-events-pending guard.
;
; Setup:
;   1. Install AutoHotkey v2 from https://www.autohotkey.com/
;   2. Save this file, double-click to run (tray icon should appear)
;   3. IMPORTANT: TEMPORARILY DISABLE NexusKey (tray icon -> Exit, or
;      set to English mode) — we need raw VK_PACKET, no IME interference
;   4. Open Firefox → vn-z.vn → reply box
;   5. Click the site's emoji/smilie button → insert one smilie (<img>)
;   6. DO NOT click elsewhere, DO NOT press arrow keys
;   7. Press F9
;
; Watch the result in the reply box:
;   - "á á" (with space between) → VK_PACKET works → bug is in NexusKey
;   - "áá"  (space missing)      → VK_PACKET broken in Firefox → bug is in browser
;   - Nothing appears            → script not focused / wrong field
;
; Repeat the test in Chrome (same site, same steps) to compare.

#Requires AutoHotkey v2.0
#SingleInstance Force

F9:: {
    ; Brief pause so the user can confirm the focus is correct
    Sleep 300
    ; Three chars via VK_PACKET: á (U+00E1), space (U+0020), á (U+00E1)
    Send "{U+00E1}{U+0020}{U+00E1}"
}

; Alternative: trigger same sequence with Ctrl+Alt+V
^!v:: {
    Sleep 300
    Send "{U+00E1}{U+0020}{U+00E1}"
}

; F10: send just three chars NO space, to verify VK_PACKET itself works
F10:: {
    Sleep 300
    Send "{U+00E1}{U+00E2}{U+00E3}"
}

; Esc: quit the script
Esc::ExitApp
