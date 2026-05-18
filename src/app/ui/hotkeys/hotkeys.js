// Unified Hotkey Rebind Dialog JS

// Win32 VK constants used for friendly-name rendering and modifier classification.
var VK = {
    ESC: 0x1B, TAB: 0x09, SPACE: 0x20, ENTER: 0x0D, BACK: 0x08,
    SHIFT: 0x10, CTRL: 0x11, ALT: 0x12, LWIN: 0x5B, RWIN: 0x5C,
};
var MOD = { CTRL: 0x01, SHIFT: 0x02, ALT: 0x04, WIN: 0x08 };

// Sciter delivers `event.keyCode` in its own (GLFW-derived) scheme — see
// `extern/sciter/include/sciter-x-key-codes.h`. F1=290, LeftShift=340, etc.
// We need Win32 VK on the C++ side (matches HookEngine + ConfigManager), so
// we map from `event.code` (DOM Level 3 string, e.g. "KeyA", "F1", "Escape").
// Win32 VK reference: https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
var CODE_TO_VK = {
    "Escape":        0x1B, "Tab":           0x09, "Space":         0x20,
    "Enter":         0x0D, "NumpadEnter":   0x0D, "Backspace":     0x08,
    "Delete":        0x2E, "Insert":        0x2D,
    "Home":          0x24, "End":           0x23,
    "PageUp":        0x21, "PageDown":      0x22,
    "ArrowLeft":     0x25, "ArrowUp":       0x26,
    "ArrowRight":    0x27, "ArrowDown":     0x28,
    "CapsLock":      0x14, "PrintScreen":   0x2C, "Pause":         0x13,
    "ContextMenu":   0x5D,
    // Modifiers — captured for completeness; bare presses are rejected.
    "ShiftLeft":     0x10, "ShiftRight":    0x10,
    "ControlLeft":   0x11, "ControlRight":  0x11,
    "AltLeft":       0x12, "AltRight":      0x12,
    "MetaLeft":      0x5B, "MetaRight":     0x5C, "OSLeft": 0x5B, "OSRight": 0x5C,
    // OEM punctuation — common rebind candidates.
    "Semicolon":     0xBA, "Equal":         0xBB, "Comma":         0xBC,
    "Minus":         0xBD, "Period":        0xBE, "Slash":         0xBF,
    "Backquote":     0xC0, "BracketLeft":   0xDB, "Backslash":     0xDC,
    "BracketRight":  0xDD, "Quote":         0xDE,
};

// Convert "event.code" string -> Win32 VK number. Returns 0 if unmapped.
// Patterned codes (KeyA, Digit3, Numpad7, F12) are decoded by regex; specific
// codes (Escape, Enter, ArrowUp...) come from CODE_TO_VK above.
function codeToVk(code) {
    if (!code) return 0;
    if (CODE_TO_VK[code]) return CODE_TO_VK[code];
    var m = /^(Key|Digit|Numpad|F)([A-Z]|\d+)$/.exec(code);
    if (!m) return 0;
    var suffix = m[2];
    switch (m[1]) {
        case "Key":    return suffix.charCodeAt(0);              // A-Z   -> 0x41..0x5A
        case "Digit":  return suffix.charCodeAt(0);              // 0-9   -> 0x30..0x39
        case "Numpad": return 0x60 + parseInt(suffix, 10);       // 0-9   -> VK_NUMPAD0..9
        case "F":      var n = parseInt(suffix, 10);
                       return (n >= 1 && n <= 24) ? 0x6F + n : 0;  // F1..F24
    }
    return 0;
}

// Pending capture state — only meaningful while #capture-overlay is visible.
var pending = { vk: 0, mods: 0, doubleTap: false, label: "—" };

// Double-tap detection — second same-key press within window upgrades to 2×.
var DOUBLE_TAP_WINDOW_MS = 400;
var lastTapVk = 0, lastTapTs = 0;

// Friendly VK→name table — uploaded from C++ via setVkNames() on dialog init
// (HotkeysDialog::sendVkNames). C++ owns the canonical list to avoid drift;
// we only keep the algorithmic ranges (letters / digits / F-keys / numpad)
// below since those would be redundant to ship over the wire.
var VK_NAMES = {};

document.ready = function () {
    initSubDialog();
    initHotkeysDialog();
};

function initHotkeysDialog() {
    document.getElementById("btn-close").addEventListener("click", function () {
        triggerAction("close");
    });
    document.getElementById("btn-reset").addEventListener("click", function () {
        triggerAction("reset");
    });
    document.on("click", ".btn-add-trigger", function (evt, btn) {
        var intent = btn.getAttribute("data-intent");
        openCapture(intent);
        evt.stopPropagation();
    });
    document.on("click", ".chip-delete", function (evt, btn) {
        var chip = btn.closest(".hotkey-chip");
        if (!chip) return;
        var intent = chip.getAttribute("data-intent");
        var vk     = parseInt(chip.getAttribute("data-vk"), 10) || 0;
        var mods   = parseInt(chip.getAttribute("data-mods"), 10) || 0;
        var dt     = chip.getAttribute("data-double-tap") === "true";
        if (!vk) return;
        document.getElementById("val-intent").value      = intent;
        document.getElementById("val-vk").value          = String(vk);
        document.getElementById("val-mods").value        = String(mods);
        document.getElementById("val-double-tap").value  = dt ? "true" : "false";
        triggerAction("delete");
        evt.stopPropagation();
    });

    var save    = document.getElementById("btn-capture-save");
    var cancel  = document.getElementById("btn-capture-cancel");
    save.addEventListener("click", function () { commitCapture(); });
    cancel.addEventListener("click", function () { closeCapture(); });

    // Per-intent enable toggle — single delegated handler. data-intent on each
    // `.toggle-switch-small` carries the matching Intent string so the C++ side
    // can wire VALUE_CHANGED back to HotkeyRegistry::SetEnabled.
    document.on("click", ".hotkey-section .toggle-switch-small", function (evt, toggle) {
        var intent = toggle.getAttribute("data-intent");
        if (!intent) return;
        var newState = !toggle.classList.contains("checked");
        if (newState) toggle.classList.add("checked");
        else          toggle.classList.remove("checked");
        document.getElementById("val-intent").value  = intent;
        document.getElementById("val-enabled").value = newState ? "true" : "false";
        triggerAction("set-enabled");
        evt.stopPropagation();
    });

    // Capture mode — sinking-phase keydown so Alt's menu-accelerator default
    // handler at the Sciter window level doesn't swallow our events. `^keydown`
    // dispatches root→target BEFORE the target phase where default actions
    // (including the Alt→menu activation) execute. Documented in Sciter SDK
    // `samples.sciter/input-elements/input-events-handling.htm`.
    document.on("^keydown", function (evt) {
        var vk = codeToVk(evt.code);
        captureKey(evt, vk);
    });
}

function captureKey(evt, vk) {
    var overlay = document.getElementById("capture-overlay");
    if (!overlay || overlay.style.display === "none") return;
    // Ignore OS auto-repeat — would otherwise trigger false 2× detection.
    if (evt.repeat) { evt.preventDefault(); return; }
    if (!vk) { evt.preventDefault(); return; }

    // Double-tap: second press of same key within window → upgrade to 2×.
    var now = Date.now();
    var isDoubleTap = (vk === lastTapVk) && (now - lastTapTs <= DOUBLE_TAP_WINDOW_MS);
    lastTapVk = vk;
    lastTapTs = now;

    // Modifier-alone (vk is modifier) and double-tap both imply mods=0
    // (matches HotkeyRegistry::Trigger semantics). Otherwise collect chord flags.
    var mods = 0;
    if (!isDoubleTap && !isModifierVk(vk)) {
        if (evt.ctrlKey)  mods |= MOD.CTRL;
        if (evt.shiftKey) mods |= MOD.SHIFT;
        if (evt.altKey)   mods |= MOD.ALT;
        if (evt.metaKey)  mods |= MOD.WIN;
    }

    pending.vk        = vk;
    pending.mods      = mods;
    pending.doubleTap = isDoubleTap;
    pending.label     = formatLabel(vk, mods, isDoubleTap);

    document.getElementById("capture-preview").textContent = pending.label;
    document.getElementById("btn-capture-save").removeAttribute("disabled");

    evt.preventDefault();
    evt.stopPropagation();
}

function isModifierVk(vk) {
    return vk === VK.CTRL || vk === VK.SHIFT || vk === VK.ALT
        || vk === VK.LWIN || vk === VK.RWIN;
}

function formatLabel(vk, mods, doubleTap) {
    if (doubleTap) return "2×" + vkName(vk);                 // 2× implies mods=0
    var parts = [];
    if (mods & MOD.CTRL)  parts.push("Ctrl");
    if (mods & MOD.SHIFT) parts.push("Shift");
    if (mods & MOD.ALT)   parts.push("Alt");
    if (mods & MOD.WIN)   parts.push("Win");
    parts.push(vkName(vk));
    return parts.join("+");
}

function vkName(vk) {
    if (VK_NAMES[vk]) return VK_NAMES[vk];
    if (vk >= 0x60 && vk <= 0x69) return "Num" + (vk - 0x60);            // VK_NUMPAD0..9
    if (vk >= 0x70 && vk <= 0x87) return "F" + (vk - 0x6F);              // F1..F24
    if ((vk >= 0x30 && vk <= 0x39) || (vk >= 0x41 && vk <= 0x5A)) {
        return String.fromCharCode(vk);                                  // 0..9 / A..Z
    }
    return "VK_" + vk;
}

function openCapture(intent) {
    pending.vk = 0;
    pending.mods = 0;
    pending.doubleTap = false;
    pending.label = "—";
    lastTapVk = 0; lastTapTs = 0;  // fresh state per session
    document.getElementById("capture-preview").textContent = "—";
    document.getElementById("btn-capture-save").setAttribute("disabled", "disabled");
    document.getElementById("capture-overlay").style.display = "block";
    document.getElementById("val-intent").value = intent;
    // No focus call needed — document-level keydown handler catches everything.
}

function closeCapture() {
    document.getElementById("capture-overlay").style.display = "none";
}

function commitCapture() {
    if (!pending.vk) return;
    document.getElementById("val-vk").value         = String(pending.vk);
    document.getElementById("val-mods").value       = String(pending.mods);
    document.getElementById("val-double-tap").value = pending.doubleTap ? "true" : "false";
    triggerAction("add");
    closeCapture();
}

function triggerAction(action) {
    var actionInput = document.getElementById("val-action");
    if (!actionInput) return;
    actionInput.value = action;
    var event = new Event("change", { bubbles: true });
    actionInput.dispatchEvent(event);
}

// ────────────────────── Called from C++ side ────────────────────────────

// Receive the canonical VK→name table from HotkeysDialog::sendVkNames.
// Payload shape: [[vk:int, name:string], ...]. We rebuild a flat object so
// vkName() lookups stay O(1).
function setVkNames(pairs) {
    if (!pairs || typeof pairs.length !== "number") return;
    var map = {};
    for (var i = 0; i < pairs.length; ++i) {
        var p = pairs[i];
        if (p && p.length >= 2) map[p[0]] = p[1];
    }
    VK_NAMES = map;
}

function clearAll() {
    ["cancel-composition", "skip-macro", "toggle-enabled"].forEach(function (intent) {
        var list = document.getElementById("chips-" + intent);
        if (list) list.innerHTML = "";
    });
}

// Receive per-intent enabled state from HotkeysDialog::sendEnabledStates.
// Payload: [[intent:string, enabled:bool], ...].
function setEnabledStates(pairs) {
    if (!pairs || typeof pairs.length !== "number") return;
    for (var i = 0; i < pairs.length; ++i) {
        var p = pairs[i];
        if (!p || p.length < 2) continue;
        var toggle = document.getElementById("enable-" + p[0]);
        if (!toggle) continue;
        if (p[1]) toggle.classList.add("checked");
        else      toggle.classList.remove("checked");
    }
}

// C++ packs the 5 fields into a single sciter::value array:
//   [intent, label, vk, mods, doubleTap]
// because sciter::host::call_function tops out below the 6 args we'd need
// to pass them individually. We accept both shapes here so future refactors
// (or alternative callers) can use either.
function addTrigger(intent, label, vk, mods, doubleTap) {
    if (typeof label === "undefined" && intent && typeof intent === "object"
        && typeof intent.length === "number") {
        var arr = intent;
        intent    = arr[0];
        label     = arr[1];
        vk        = arr[2];
        mods      = arr[3];
        doubleTap = arr[4];
    }
    var list = document.getElementById("chips-" + intent);
    if (!list) return;

    var chip = document.createElement("div");
    chip.className = "hotkey-chip";
    chip.setAttribute("data-intent", intent);
    chip.setAttribute("data-vk",   String(vk));
    chip.setAttribute("data-mods", String(mods));
    chip.setAttribute("data-double-tap", doubleTap ? "true" : "false");

    var span = document.createElement("span");
    span.className = "chip-label";
    span.textContent = label;
    chip.appendChild(span);

    var del = document.createElement("button");
    del.className = "chip-delete";
    del.textContent = "×";
    chip.appendChild(del);

    list.appendChild(chip);
}

function forceRefresh() {
    // No-op for now — chips are static after populate.
}
