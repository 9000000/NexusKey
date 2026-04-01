# Manual Test Matrix — Universal SendInput Output

## Test Words
- "thương" (t-h-u-o-w-n-g) — modifier + coda consonants after
- "đã" (d-d-a-a-j) — stroke modifier + tone
- "người" (n-g-u-o-w-i-r) — horn modifier + tone
- "tế" (t-e-e-s) — circumflex + tone
- "xuyên" + 'e' (x-u-y-e-e-n-e) — circumflex escape after coda
- "test" (t-e-s-s-t) — English word via tone escape
- YouTube hotkeys: F (fullscreen), M (mute), K (play/pause)
- Ctrl+Z after typing Vietnamese word (undo test)

## Test Matrix

| App + Control | thương | đã | người | tế | xuyên+e | test | Hotkeys | Ctrl+Z | Result |
|---|---|---|---|---|---|---|---|---|---|
| Notepad++ Scintilla (editor) | | | | | | | N/A | | |
| Notepad++ Find (ComboBox Edit) | | | | | | | N/A | | |
| Notepad++ Replace (ComboBox Edit) | | | | | | | N/A | | |
| Windows Notepad | | | | | | | N/A | | |
| WordPad (RichEdit) | | | | | | | N/A | | |
| Chrome address bar | | | | | | | N/A | | |
| Chrome text input (web page) | | | | | | | N/A | | |
| YouTube (video playing, no text) | N/A | N/A | N/A | N/A | N/A | N/A | | N/A | |
| VS Code editor | | | | | | | N/A | | |
| Telegram chat | | | | | | | N/A | | |
| Windows Terminal (cmd) | | | | | | | N/A | | |
| Microsoft Word | | | | | | | N/A | | |
| Run dialog (Win+R) | | | | | | | N/A | | |

## Pass Criteria
- All Vietnamese words display correctly with proper diacritics
- "test" (tone escape) produces "test" not "tést"
- "xuyên"+e produces "xuyene" (circumflex escape works)
- YouTube hotkeys work when video is focused
- Ctrl+Z undoes the last typed word correctly
- No ghost characters after deleting all text and retyping
