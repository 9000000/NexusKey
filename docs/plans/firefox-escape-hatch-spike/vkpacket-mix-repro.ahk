; NexusKey Firefox <img>-space-eater bug — MIXED VK_PACKET + scancode repro
; -------------------------------------------------------------------------
; Purpose: reproduce EXACTLY what NexusKey does — VK_PACKET for Unicode chars
;          interleaved with hardware scancode space/backspace.
;
; Setup: same as vkpacket-repro.ahk (AHK v2, NexusKey OFF, Firefox vn-z.vn,
;        insert <img> smilie, caret right after, press test key).
;
; Tests:
;   F9  → MIX:      VK_PACKET("á") + hardware scancode SPACE + VK_PACKET("á")
;                   Expected if theory is right: "áá" (scancode space dropped)
;   F10 → ALL PACKET:  VK_PACKET("á") + VK_PACKET(" ") + VK_PACKET("á")
;                      Expected: "á á" (already confirmed earlier test)
;   F11 → ALL SCAN:    scancode 'a' + 's' + SPACE + 'a' + 's'
;                      Pure scancode baseline — shouldn't hit VK_PACKET path at all
;
; Compare F9 vs F10: if F10 works but F9 fails → scancode-space-after-VK_PACKET
; is the exact bug. NexusKey fix: use VK_PACKET for space re-injection.

#Requires AutoHotkey v2.0
#SingleInstance Force

F9:: {
    Sleep 300
    ; VK_PACKET for "á", then HARDWARE scancode for SPACE, then VK_PACKET for "á"
    Send "{U+00E1}"       ; á via VK_PACKET
    Send "{Space}"        ; SPACE via scancode (this is what NexusKey does)
    Send "{U+00E1}"       ; á via VK_PACKET
}

F10:: {
    Sleep 300
    ; All three chars via VK_PACKET (baseline — already confirmed working)
    Send "{U+00E1}{U+0020}{U+00E1}"
}

F11:: {
    Sleep 300
    ; Pure scancode — 'a' 's' space 'a' 's' (no Vietnamese IME, no VK_PACKET)
    Send "as as"
}

Esc::ExitApp
