// Macro Dialog JavaScript

var MACRO_CLIPBOARD_THRESHOLD = 200;

// Map, not a plain object: macro names come from a user-editable TOML, and a
// name like "__proto__" would silently fail to store in an object literal.
var allMacros = new Map();
var selectedMacroName = null;
var searchQuery = "";

document.ready = function () {
    initSubDialog(".macro-list");
    initMacroDialog();
};

function initMacroDialog() {
    var btnAdd = document.getElementById("btn-add");
    var btnEdit = document.getElementById("btn-edit");
    var btnDelete = document.getElementById("btn-delete");
    var btnClear = document.getElementById("btn-clear");
    var btnImport = document.getElementById("btn-import");
    var btnExport = document.getElementById("btn-export");
    var btnClose = document.getElementById("btn-close");
    var macroContent = document.getElementById("macro-content");
    var searchInput = document.getElementById("macro-search-input");

    if (btnAdd) btnAdd.onclick = function () { onAddMacro(); };
    if (btnEdit) btnEdit.onclick = function () { onEditMacro(); };
    if (btnDelete) btnDelete.onclick = function () { onDeleteMacro(); };
    if (btnClear) btnClear.onclick = function () { clearSelection(); };
    if (btnImport) btnImport.onclick = function () { triggerAction("import"); };
    if (btnExport) btnExport.onclick = function () { triggerAction("export"); };
    if (btnClose) btnClose.onclick = function () { triggerAction("close"); };

    if (searchInput) {
        searchInput.addEventListener("input", function () {
            searchQuery = this.value.trim().toLowerCase();
            renderMacroList();
        });
    }

    // Bind trigger checkboxes
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

    updateButtonStates();
}

// Case-insensitive lookup for duplicate macro shortcuts
function hasMacroKey(key) {
    if (!key) return false;
    var lowerKey = key.toLowerCase();
    var keys = Array.from(allMacros.keys());
    for (var i = 0; i < keys.length; i++) {
        if (keys[i].toLowerCase() === lowerKey) return true;
    }
    return false;
}

// Convert storage format (\n literal) → real newlines for textarea display
function storageToDisplay(text) {
    if (!text) return "";
    return text.replace(/\\n/g, "\n");
}

// Convert real newlines → storage format (\n literal) for C++
function displayToStorage(text) {
    if (!text) return "";
    return text.replace(/\n/g, "\\n");
}

function updateCharCounter() {
    var content = document.getElementById("macro-content");
    var counter = document.getElementById("char-counter");
    var clipHint = document.getElementById("clipboard-hint");
    if (!content || !counter) return;

    var storageLen = displayToStorage(content.value).length;
    counter.textContent = storageLen + " / 20480";

    if (storageLen > 18400) {
        counter.classList.add("near-limit");
    } else {
        counter.classList.remove("near-limit");
    }

    if (clipHint) {
        if (storageLen > MACRO_CLIPBOARD_THRESHOLD) {
            clipHint.classList.add("visible");
        } else {
            clipHint.classList.remove("visible");
        }
    }
}

// Edit / Delete / Clear only exist while a row is selected — hiding them is the
// whole gate, so the handlers need no disabled-state bookkeeping.
function updateButtonStates() {
    var display = selectedMacroName ? "block" : "none";
    var ids = ["btn-edit", "btn-delete", "btn-clear"];
    for (var i = 0; i < ids.length; i++) {
        var el = document.getElementById(ids[i]);
        if (el) el.style.display = display;
    }
}

function clearSelection() {
    selectedMacroName = null;

    var nameField = document.getElementById("macro-name");
    var contentField = document.getElementById("macro-content");

    if (nameField) nameField.value = "";
    if (contentField) contentField.value = "";

    updateCharCounter();
    updateButtonStates();

    var items = document.querySelectorAll(".macro-item");
    for (var i = 0; i < items.length; i++) {
        items[i].classList.remove("selected");
    }
}

function onAddMacro() {
    var nameField = document.getElementById("macro-name");
    var contentField = document.getElementById("macro-content");

    if (!nameField || !contentField) return;

    var name = nameField.value.trim();
    var content = contentField.value.trim();

    if (name === "" || content === "") return;

    // Check for duplicate shortcut key (case-insensitive)
    if (hasMacroKey(name)) {
        showDuplicateWarning();
        return;
    }

    document.getElementById("val-old-macro-name").value = "";
    document.getElementById("val-macro-name").value = name;
    document.getElementById("val-macro-content").value = displayToStorage(content);
    triggerAction("add");

    if (typeof showToastI18n === "function") {
        showToastI18n("Đã thêm gõ tắt: " + name, "Added shortcut: " + name);
    }

    clearSelection();
    nameField.focus();
}

function showDuplicateWarning() {
    if (typeof showToastI18n !== "function") return;
    showToastI18n(
        "Từ gõ tắt này đã tồn tại. Hãy chọn macro đó để sửa hoặc dùng từ gõ tắt khác.",
        "This shortcut already exists. Select it to edit or use another shortcut."
    );
}

function onEditMacro() {
    if (!selectedMacroName) return;

    var nameField = document.getElementById("macro-name");
    var contentField = document.getElementById("macro-content");

    if (!nameField || !contentField) return;

    var name = nameField.value.trim();
    var content = contentField.value.trim();

    if (name === "" || content === "") return;

    // Check for target shortcut collision if key name was changed (case-insensitive)
    if (name.toLowerCase() !== selectedMacroName.toLowerCase() && hasMacroKey(name)) {
        showDuplicateWarning();
        return;
    }

    document.getElementById("val-old-macro-name").value = selectedMacroName;
    document.getElementById("val-macro-name").value = name;
    document.getElementById("val-macro-content").value = displayToStorage(content);

    // Move the selection to the new name BEFORE the trigger: C++ repopulates the
    // list synchronously inside it, and renderMacroList drops a selection whose
    // name no longer exists in the table.
    selectedMacroName = name;
    triggerAction("edit");

    if (typeof showToastI18n === "function") {
        showToastI18n("Đã cập nhật gõ tắt: " + name, "Updated shortcut: " + name);
    }
}

function onDeleteMacro() {
    if (!selectedMacroName) return;

    document.getElementById("val-macro-name").value = selectedMacroName;
    triggerAction("delete");
}

function selectMacroItem(element, name, content) {
    var items = document.querySelectorAll(".macro-item");
    for (var i = 0; i < items.length; i++) {
        items[i].classList.remove("selected");
    }

    element.classList.add("selected");
    selectedMacroName = name;

    document.getElementById("macro-name").value = name;
    document.getElementById("macro-content").value = storageToDisplay(content);
    updateCharCounter();
    updateButtonStates();
}

function triggerAction(action) {
    var actionInput = document.getElementById("val-action");
    if (actionInput) {
        actionInput.value = action;
        var event = new Event("change", { bubbles: true });
        actionInput.dispatchEvent(event);
    }
}

// Called by C++ to start populating list
function clearMacroList() {
    allMacros.clear();
    var list = document.getElementById("macro-list");
    if (list) list.innerHTML = "";
    var emptyEl = document.getElementById("macro-list-empty");
    if (emptyEl) emptyEl.style.display = "none";
}

// Called by C++ for each item
function addMacroToList(name, content) {
    allMacros.set(name, content);
}

// Called by C++ after adding all items
function finishMacroList() {
    renderMacroList();
}

function renderMacroList() {
    var list = document.getElementById("macro-list");
    var emptyEl = document.getElementById("macro-list-empty");
    if (!list) return;

    list.innerHTML = "";

    // Every C++ refresh (delete, import-replace) funnels through here, so this is
    // where a selection pointing at a macro that no longer exists gets dropped —
    // otherwise Edit would write the stale name straight back into the table.
    if (selectedMacroName && !allMacros.has(selectedMacroName)) {
        clearSelection();
    }

    var sortedKeys = Array.from(allMacros.keys()).sort();
    var matchCount = 0;

    for (var i = 0; i < sortedKeys.length; i++) {
        var name = sortedKeys[i];
        var content = allMacros.get(name);
        var displayContent = storageToDisplay(content);

        // Filter by search query on both shortcut name and content
        if (searchQuery !== "") {
            var matchName = name.toLowerCase().indexOf(searchQuery) !== -1;
            var matchContent = displayContent.toLowerCase().indexOf(searchQuery) !== -1;
            if (!matchName && !matchContent) {
                continue;
            }
        }

        matchCount++;
        var item = document.createElement("div");
        item.className = "macro-item";
        item.setAttribute("data-name", name);
        item.setAttribute("data-content", content);

        if (name === selectedMacroName) {
            item.classList.add("selected");
        }

        var preview = formatPreview(content);
        item.innerHTML = '<span class="macro-item-name">' + escapeHtml(name) + '</span>' +
            '<span class="macro-item-content">' + preview + '</span>';

        var tooltipText = displayContent;
        if (tooltipText.length > 500) tooltipText = tooltipText.substring(0, 500) + "...";
        item.setAttribute("title", tooltipText);

        (function (el, n, c) {
            el.addEventListener("click", function () {
                selectMacroItem(el, n, c);
            });
        })(item, name, content);

        list.appendChild(item);
    }

    if (emptyEl) {
        emptyEl.style.display = matchCount === 0 ? "block" : "none";
    }

    updateButtonStates();
}

function formatPreview(content) {
    var lines = content.split("\\n");
    var firstLine = lines[0];
    var lineCount = lines.length;

    if (firstLine.length > 60) firstLine = firstLine.substring(0, 60) + "...";
    var display = escapeHtml(firstLine);

    if (lineCount > 1) {
        display += ' <span style="opacity:0.5; font-size:10px">\u23CE' + lineCount + '</span>';
    }

    return display;
}

function escapeHtml(text) {
    var div = document.createElement("div");
    div.textContent = text;
    return div.innerHTML;
}
