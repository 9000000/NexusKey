// Unified Hotkey Rebind Dialog JS

// Win32 VK constants used for friendly-name rendering and modifier classification.
var VK = {
    ESC: 0x1B, TAB: 0x09, SPACE: 0x20, ENTER: 0x0D, BACK: 0x08,
    SHIFT: 0x10, CTRL: 0x11, ALT: 0x12, LWIN: 0x5B, RWIN: 0x5C,
};
var MOD = { CTRL: 0x01, SHIFT: 0x02, ALT: 0x04, WIN: 0x08 };

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

    var overlay = document.getElementById("capture-overlay");
    var save    = document.getElementById("btn-capture-save");
    var cancel  = document.getElementById("btn-capture-cancel");
    save.addEventListener("click", function () { commitCapture(); });
    cancel.addEventListener("click", function () { closeCapture(); });

    // Capture mode: listen for keydown anywhere while overlay is visible.
    document.body.addEventListener("keydown", function (evt) {
        var overlay = document.getElementById("capture-overlay");
        if (!overlay || overlay.style["display"] === "none") return;

        var keyCode = evt.keyCode || evt.which || 0;
        if (!keyCode) return;

        // Reject bare modifier presses — Sciter delivers separate events for
        // Ctrl/Shift/Alt/Win, and we want a "main key" with optional mods.
        // (Modifier-alone and double-tap rebinds are v2 work — defaults are
        // preserved for users who want Ctrl-alone / 2×Alt.)
        if (isModifier(keyCode)) {
            evt.preventDefault();
            return;
        }

        var mods = 0;
        if (evt.ctrlKey)  mods |= MOD.CTRL;
        if (evt.shiftKey) mods |= MOD.SHIFT;
        if (evt.altKey)   mods |= MOD.ALT;
        if (evt.metaKey)  mods |= MOD.WIN;

        pending.vk    = keyCode;
        pending.mods  = mods;
        pending.label = formatLabel(keyCode, mods);

        document.getElementById("capture-preview").textContent = pending.label;
        document.getElementById("btn-capture-save").removeAttribute("disabled");

        evt.preventDefault();
        evt.stopPropagation();
    });
}

function isModifier(vk) {
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
    document.getElementById("capture-overlay").style["display"] = "block";
    document.getElementById("val-intent").value = intent;
    // Sciter requires focused element to receive keydown — focus the box.
    var box = document.getElementById("capture-overlay");
    if (box && box.focus) box.focus();
}

function closeCapture() {
    document.getElementById("capture-overlay").style["display"] = "none";
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

function addTrigger(intent, label, vk, mods, doubleTap) {
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
