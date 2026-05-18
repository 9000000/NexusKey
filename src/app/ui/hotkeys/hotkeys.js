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
function codeToVk(code) {
    if (!code) return 0;
    if (CODE_TO_VK.hasOwnProperty(code)) return CODE_TO_VK[code];
    // "KeyA".."KeyZ"   -> 0x41..0x5A
    if (code.length === 4 && code.substr(0, 3) === "Key") {
        var c = code.charCodeAt(3);
        if (c >= 65 && c <= 90) return c;
    }
    // "Digit0".."Digit9" -> 0x30..0x39
    if (code.length === 6 && code.substr(0, 5) === "Digit") {
        var d = code.charCodeAt(5);
        if (d >= 48 && d <= 57) return d;
    }
    // "Numpad0".."Numpad9" -> VK_NUMPAD0(0x60)..VK_NUMPAD9(0x69)
    if (code.length === 7 && code.substr(0, 6) === "Numpad") {
        var n = code.charCodeAt(6);
        if (n >= 48 && n <= 57) return 0x60 + (n - 48);
    }
    // "F1".."F24" -> VK_F1(0x70)..VK_F24(0x87)
    if (code.length >= 2 && code.charAt(0) === "F") {
        var num = parseInt(code.substr(1), 10);
        if (num >= 1 && num <= 24) return 0x6F + num;
    }
    return 0;
}

// Pending capture state — only meaningful while #capture-overlay is visible.
var pending = { vk: 0, mods: 0, label: "—" };

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

    // Capture mode: document-level keydown is the Sciter idiom (per SDK docs:
    // `document.on("keydown", ...)`). Fires regardless of focus target, so we
    // don't need a hidden input or to chase focus. We gate the handler on
    // overlay visibility so it only intercepts keys during capture.
    document.on("keydown", function (evt) {
        var overlay = document.getElementById("capture-overlay");
        if (!overlay || overlay.style.display === "none") return;

        // Use `event.code` (string, DOM Level 3) — `event.keyCode` is Sciter's
        // GLFW scheme, NOT Win32 VK (F1=290 there). codeToVk() maps to VK.
        var vk = codeToVk(evt.code);
        if (!vk) {
            evt.preventDefault();
            return;
        }

        // Reject bare modifier presses — Sciter delivers separate events for
        // Ctrl/Shift/Alt/Win, and we want a "main key" with optional mods.
        // (Modifier-alone and double-tap rebinds are v2 work — defaults are
        // preserved for users who want Ctrl-alone / 2×Alt.)
        if (isModifierVk(vk)) {
            evt.preventDefault();
            return;
        }

        var mods = 0;
        if (evt.ctrlKey)  mods |= MOD.CTRL;
        if (evt.shiftKey) mods |= MOD.SHIFT;
        if (evt.altKey)   mods |= MOD.ALT;
        if (evt.metaKey)  mods |= MOD.WIN;

        pending.vk    = vk;
        pending.mods  = mods;
        pending.label = formatLabel(vk, mods);

        document.getElementById("capture-preview").textContent = pending.label;
        document.getElementById("btn-capture-save").removeAttribute("disabled");

        evt.preventDefault();
        evt.stopPropagation();
    });
}

function isModifierVk(vk) {
    return vk === VK.CTRL || vk === VK.SHIFT || vk === VK.ALT
        || vk === VK.LWIN || vk === VK.RWIN;
}

function formatLabel(vk, mods) {
    var parts = [];
    if (mods & MOD.CTRL)  parts.push("Ctrl");
    if (mods & MOD.SHIFT) parts.push("Shift");
    if (mods & MOD.ALT)   parts.push("Alt");
    if (mods & MOD.WIN)   parts.push("Win");
    parts.push(vkName(vk));
    return parts.join("+");
}

function vkName(vk) {
    switch (vk) {
        case VK.ESC:   return "Esc";
        case VK.TAB:   return "Tab";
        case VK.SPACE: return "Space";
        case VK.ENTER: return "Enter";
        case VK.BACK:  return "Backspace";
    }
    if (vk >= 0x70 && vk <= 0x7B) return "F" + (vk - 0x6F);  // F1..F12
    if ((vk >= 0x30 && vk <= 0x39) || (vk >= 0x41 && vk <= 0x5A)) {
        return String.fromCharCode(vk);
    }
    return "VK_" + vk;
}

function openCapture(intent) {
    pending.vk = 0;
    pending.mods = 0;
    pending.label = "—";
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
    document.getElementById("val-double-tap").value = "false";  // v1: no DT capture
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

function clearAll() {
    ["cancel-composition", "skip-macro", "toggle-enabled"].forEach(function (intent) {
        var list = document.getElementById("chips-" + intent);
        if (list) list.innerHTML = "";
    });
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
