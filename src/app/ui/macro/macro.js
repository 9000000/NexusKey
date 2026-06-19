// Macro Dialog JavaScript

var MACRO_CLIPBOARD_THRESHOLD = 200;

// True while a row is selected and the Add button acts as "Edit" (set by
// selectMacroItem, cleared once the edit is committed / button text resets).
var macroEditMode = false;

document.ready = function () {
    initSubDialog(".macro-list");
    initMacroDialog();
};

function initMacroDialog() {
    var btnAdd = document.getElementById("btn-add");
    var btnDelete = document.getElementById("btn-delete");
    var btnImport = document.getElementById("btn-import");
    var btnExport = document.getElementById("btn-export");
    var btnClose = document.getElementById("btn-close");
    var macroName = document.getElementById("macro-name");
    var macroContent = document.getElementById("macro-content");

    if (btnAdd) btnAdd.addEventListener("click", function () { onAddMacro(); });
    if (btnDelete) btnDelete.addEventListener("click", function () { onDeleteMacro(); });
    if (btnImport) btnImport.addEventListener("click", function () { triggerAction("import"); });
    if (btnExport) btnExport.addEventListener("click", function () { triggerAction("export"); });
    if (btnClose) btnClose.addEventListener("click", function () { triggerAction("close"); });

    if (macroName) {
        macroName.addEventListener("change", function () { updateAddButtonText(); });
    }

    // Bind triggers
    var triggers = ["cfg-macro_trigger_space", "cfg-macro_trigger_enter", "cfg-macro_trigger_tab", "cfg-macro_trigger_dir"];
    for (var i = 0; i < triggers.length; i++) {
        var el = document.getElementById(triggers[i]);
        if (el) {
            el.addEventListener("change", function () {
                document.getElementById("val-trigger-id").value = this.id;
                document.getElementById("val-trigger-val").value = this.checked ? "1" : "0";
                triggerAction("toggle_trigger");
            });
        }
    }

    // Character counter + clipboard hint
    if (macroContent) {
        macroContent.addEventListener("input", function () { updateCharCounter(); });
    }
}

// Convert storage format (\n literal) → real newlines for textarea display
function storageToDisplay(text) {
    return text.replace(/\\n/g, "\n");
}

// Convert real newlines → storage format (\n literal) for C++
function displayToStorage(text) {
    return text.replace(/\n/g, "\\n");
}

function updateCharCounter() {
    var content = document.getElementById("macro-content");
    var counter = document.getElementById("char-counter");
    var clipHint = document.getElementById("clipboard-hint");
    if (!content || !counter) return;

    // Count storage-format length (what C++ will receive)
    var storageLen = displayToStorage(content.value).length;
    counter.textContent = storageLen + " / 20480";

    if (storageLen > 18400) {
        counter.classList.add("near-limit");
    } else {
        counter.classList.remove("near-limit");
    }

    // Show clipboard hint when macro will use clipboard paste
    if (clipHint) {
        if (storageLen > MACRO_CLIPBOARD_THRESHOLD) {
            clipHint.classList.add("visible");
        } else {
            clipHint.classList.remove("visible");
        }
    }
}

function onAddMacro() {
    var nameField = document.getElementById("macro-name");
    var contentField = document.getElementById("macro-content");

    if (!nameField || !contentField) return;

    var name = nameField.value.trim();
    var content = contentField.value.trim();

    if (name === "" || content === "") return;

    // Convert real newlines to \n storage format before sending to C++
    document.getElementById("val-macro-name").value = name;
    document.getElementById("val-macro-content").value = displayToStorage(content);
    triggerAction("add");

    // Toast feedback
    if (typeof showToastI18n === "function") {
        if (macroEditMode) {
            showToastI18n("Đã cập nhật gõ tắt: " + name, "Updated shortcut: " + name);
        } else {
            showToastI18n("Đã thêm gõ tắt: " + name, "Added shortcut: " + name);
        }
    }

    nameField.value = "";
    contentField.value = "";
    updateCharCounter();
    nameField.focus();
}

function onDeleteMacro() {
    var nameField = document.getElementById("macro-name");
    if (!nameField) return;

    var name = nameField.value.trim();
    if (name === "") return;

    document.getElementById("val-macro-name").value = name;
    triggerAction("delete");

    // Toast feedback
    if (typeof showToastI18n === "function") {
        showToastI18n("Đã xóa gõ tắt: " + name, "Deleted shortcut: " + name);
    }

    nameField.value = "";
    document.getElementById("macro-content").value = "";
    updateCharCounter();
}

function selectMacroItem(element, name, content) {
    var items = document.querySelectorAll(".macro-item");
    for (var i = 0; i < items.length; i++) {
        items[i].classList.remove("selected");
    }
    element.classList.add("selected");

    // content from C++ is in storage format — convert to real newlines for textarea
    document.getElementById("macro-name").value = name;
    document.getElementById("macro-content").value = storageToDisplay(content);
    updateCharCounter();

    document.getElementById("btn-add").textContent = t("m.edit") || "+ S\u1eeda";
    macroEditMode = true;
}

function updateAddButtonText() {
    var btnAdd = document.getElementById("btn-add");
    if (btnAdd) {
        btnAdd.textContent = t("add") || "+ Th\u00eam";
    }
    macroEditMode = false;
}

function triggerAction(action) {
    var actionInput = document.getElementById("val-action");
    if (actionInput) {
        actionInput.value = action;
        var event = new Event("change", { bubbles: true });
        actionInput.dispatchEvent(event);
    }
}

// Called by C++ to add items to the list
function addMacroToList(name, content) {
    var list = document.getElementById("macro-list");
    if (!list) return;

    var item = document.createElement("div");
    item.className = "macro-item";
    item.setAttribute("data-name", name);
    item.setAttribute("data-content", content);

    // Preview: first line, max 60 chars, line count badge
    var preview = formatPreview(content);
    item.innerHTML = '<span class="macro-item-name">' + escapeHtml(name) + '</span>' +
        '<span class="macro-item-content">' + preview + '</span>';

    // Tooltip: first 500 chars of content with real newlines
    var tooltipText = storageToDisplay(content);
    if (tooltipText.length > 500) tooltipText = tooltipText.substring(0, 500) + "...";
    item.setAttribute("title", tooltipText);

    item.addEventListener("click", function () {
        selectMacroItem(this, name, content);
    });

    list.appendChild(item);
}

function formatPreview(content) {
    // Split on \n escape sequences
    var lines = content.split("\\n");
    var firstLine = lines[0];
    var lineCount = lines.length;

    // Truncate before escaping to avoid cutting HTML entities mid-way
    if (firstLine.length > 60) firstLine = firstLine.substring(0, 60) + "...";
    var display = escapeHtml(firstLine);

    // Add line count badge if multi-line
    if (lineCount > 1) {
        display += ' <span style="opacity:0.5; font-size:10px">\u23CE' + lineCount + '</span>';
    }

    return display;
}

function clearMacroList() {
    var list = document.getElementById("macro-list");
    if (list) list.innerHTML = "";
}

function escapeHtml(text) {
    var div = document.createElement("div");
    div.textContent = text;
    return div.innerHTML;
}

// setBackgroundOpacity is provided by shared/utils.js
