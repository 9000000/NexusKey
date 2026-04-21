; NexusKey Firefox <img>-space bug — FULL sequence repro
; -------------------------------------------------------
; Replicate NexusKey's real output for user typing "a s space a s":
;   physical 'a' → BS + VK_PACKET("á") → SPACE → physical 'a' → BS + VK_PACKET("á")
;
; Setup: AHK v2, NexusKey OFF, Firefox vn-z.vn, insert <img> smilie,
;        caret right after <img>, press test key.

#Requires AutoHotkey v2.0
#SingleInstance Force

; F7: Full NexusKey-style sequence (physical + BS + VK_PACKET + scancode space)
F7:: {
    Sleep 300
    Send "a"                  ; physical 'a' (AHK scancode)
    Sleep 30
    Send "{BS}{U+00E1}"       ; BS + VK_PACKET "á" (like NexusKey correction)
    Sleep 30
    Send "{Space}"            ; scancode space (what NexusKey InjectKey does)
    Sleep 30
    Send "a"                  ; physical 'a' again
    Sleep 30
    Send "{BS}{U+00E1}"       ; BS + VK_PACKET "á"
}

; F8: Same as F7 but WITHOUT the scancode space — use VK_PACKET space instead
F8:: {
    Sleep 300
    Send "a"
    Sleep 30
    Send "{BS}{U+00E1}"
    Sleep 30
    Send "{U+0020}"           ; VK_PACKET space (instead of scancode)
    Sleep 30
    Send "a"
    Sleep 30
    Send "{BS}{U+00E1}"
}

; F9: Previous mixed test — VK_PACKET + scancode space + VK_PACKET (already PASSED)
F9:: {
    Sleep 300
    Send "{U+00E1}"
    Send "{Space}"
    Send "{U+00E1}"
}

; F10: All VK_PACKET baseline (already PASSED)
F10:: {
    Sleep 300
    Send "{U+00E1}{U+0020}{U+00E1}"
}

; F11: Pure scancode 'as as'
F11:: {
    Sleep 300
    Send "as as"
}

; F2: ATOMIC batch — BS + á + space bundled in one Send call (single SendInput batch)
F2:: {
    Sleep 300
    Send "{U+0061}"
    Sleep 30
    Send "{BS}{U+00E1}{Space}"    ; all 3 events in ONE SendInput call
    Sleep 30
    Send "{U+0061}"
    Sleep 30
    Send "{BS}{U+00E1}"
}

; F3: Longer Sleep (200ms) between correction and space
F3:: {
    Sleep 300
    Send "{U+0061}"
    Sleep 30
    Send "{BS}{U+00E1}"
    Sleep 200                     ; much longer wait
    Send "{Space}"
    Sleep 200
    Send "{U+0061}"
    Sleep 30
    Send "{BS}{U+00E1}"
}

; F4: Everything atomic in single Send
F4:: {
    Sleep 300
    SendInput "{U+0061}{BS}{U+00E1}{Space}{U+0061}{BS}{U+00E1}"
}

; F5: All VK_PACKET (no physical scancode 'a' — simulates NexusKey WITHOUT passthrough)
F5:: {
    Sleep 300
    Send "{U+0061}"           ; a via VK_PACKET (NO physical scancode)
    Sleep 30
    Send "{BS}{U+00E1}"       ; BS + á
    Sleep 30
    Send "{Space}"            ; space scancode
    Sleep 30
    Send "{U+0061}"           ; a via VK_PACKET
    Sleep 30
    Send "{BS}{U+00E1}"       ; BS + á
}

; F6: Same as F5 but VK_PACKET space too (space also via KEYEVENTF_UNICODE)
F6:: {
    Sleep 300
    Send "{U+0061}"
    Sleep 30
    Send "{BS}{U+00E1}"
    Sleep 30
    Send "{U+0020}"           ; space via VK_PACKET
    Sleep 30
    Send "{U+0061}"
    Sleep 30
    Send "{BS}{U+00E1}"
}

Esc::ExitApp
