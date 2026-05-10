; NexusKey Firefox <img>-space bug — ZWSP workaround spike
; -------------------------------------------------------
; Test Cách 2: Detect <img> boundary → force text node
; Chèn ZWSP (\u200B) trước khi gõ ký tự để ép Firefox tạo text node.
;
; Setup: AHK v2, NexusKey OFF, Firefox vn-z.vn, insert <img> smilie,
;        caret right after <img>, press test key.

#Requires AutoHotkey v2.0
#SingleInstance Force

; F12: Test chuỗi gõ đầy đủ (á á) CÓ ZWSP ở đầu
F12:: {
    Sleep 300
    ; 1. Chèn ZWSP để ép Firefox tạo text node tách khỏi <img>
    Send "{U+200B}"
    Sleep 30
    
    ; 2. Gõ chữ 'a' (thể hiện ký tự đầu tiên gõ vào)
    Send "a"                  
    Sleep 30
    
    ; 3. IME sửa thành 'á' (BS + VK_PACKET)
    Send "{BS}{U+00E1}"       
    Sleep 30
    
    ; 4. Gõ Space (đây là phím hay bị Firefox nuốt nếu không có text node)
    Send "{Space}"            
    Sleep 30
    
    ; 5. Gõ chữ 'a' tiếp theo
    Send "a"                  
    Sleep 30
    
    ; 6. IME sửa thành 'á'
    Send "{BS}{U+00E1}"       
}

; F11: Test chuỗi gõ KHÔNG CÓ ZWSP (để so sánh - lỗi nuốt space sẽ xảy ra)
F11:: {
    Sleep 300
    Send "a"                  
    Sleep 30
    Send "{BS}{U+00E1}"       
    Sleep 30
    Send "{Space}"            ; Sẽ bị nuốt
    Sleep 30
    Send "a"                  
    Sleep 30
    Send "{BS}{U+00E1}"       
}

; F10: Chỉ gửi ZWSP và a (đơn giản nhất)
F10:: {
    Sleep 300
    Send "{U+200B}"
    Sleep 30
    Send "a"
}

Esc::ExitApp
