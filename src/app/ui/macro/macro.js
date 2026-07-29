// Macro Dialog JavaScript

var MACRO_CLIPBOARD_THRESHOLD = 200;

// Map, not a plain object: macro names come from a user-editable TOML, and a
// name like "__proto__" would silently fail to store in an object literal.
var allMacros = new Map();
var selectedMacroName = null;
var checkedMacroNames = new Set();
var lastClickedKey = null;
var lastShiftRangeKeys = null;
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
    var chkSelectAll = document.getElementById("chk-select-all");

    if (btnAdd) btnAdd.onclick = function () { onAddMacro(); };
    if (btnEdit) btnEdit.onclick = function () { onEditMacro(); };
    if (btnDelete) btnDelete.onclick = function () { onDeleteMacro(); };
    if (btnClear) btnClear.onclick = function () { clearSelection(); };
    if (btnImport) btnImport.onclick = function () { triggerAction("import"); };
    if (btnExport) btnExport.onclick = function () { triggerAction("export"); };
    if (btnClose) btnClose.onclick = function () { triggerAction("close"); };

    if (chkSelectAll) {
        chkSelectAll.addEventListener("change", function () {
            onToggleSelectAll(this.checked);
        });
    }

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

function getVisibleMacroKeys() {
    var sortedKeys = Array.from(allMacros.keys()).sort();
    var result = [];
    for (var i = 0; i < sortedKeys.length; i++) {
        var name = sortedKeys[i];
        var content = allMacros.get(name);
        var displayContent = storageToDisplay(content);
        if (searchQuery !== "") {
            var matchName = name.toLowerCase().indexOf(searchQuery) !== -1;
            var matchContent = displayContent.toLowerCase().indexOf(searchQuery) !== -1;
            if (!matchName && !matchContent) continue;
        }
        result.push(name);
    }
    return result;
}

function onToggleSelectAll(isChecked) {
    lastShiftRangeKeys = null;
    var visibleKeys = getVisibleMacroKeys();
    for (var i = 0; i < visibleKeys.length; i++) {
        if (isChecked) {
            checkedMacroNames.add(visibleKeys[i]);
        } else {
            checkedMacroNames.delete(visibleKeys[i]);
        }
    }
    renderMacroList();
}

// Exact-case lookup for duplicate macro shortcuts (C++ engine supports distinct case entries like nma vs nMa)
function hasMacroKey(key) {
    if (!key) return false;
    return allMacros.has(key);
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

function updateButtonStates() {
    var btnEdit = document.getElementById("btn-edit");
    var btnDelete = document.getElementById("btn-delete");
    var btnClear = document.getElementById("btn-clear");

    var checkedCount = checkedMacroNames.size;

    // Checkboxes win over row selection as the Delete target, so Edit only shows
    // when both agree on one macro — otherwise Edit and Delete would silently act
    // on different rows while the form fields display the selected one.
    var canEdit = checkedCount === 0
        ? !!selectedMacroName
        : (checkedCount === 1 && checkedMacroNames.has(selectedMacroName));

    if (btnEdit) btnEdit.style.display = canEdit ? "block" : "none";
    if (btnDelete) btnDelete.style.display = (checkedCount > 0 || selectedMacroName) ? "block" : "none";
    if (btnClear) btnClear.style.display = (checkedCount > 0 || selectedMacroName) ? "block" : "none";

    // Count lives in its own span: applyTranslations() rewrites the sibling
    // [data-i18n] span only, so the label stays translated and the count survives.
    var deleteCount = document.getElementById("delete-count");
    if (deleteCount) deleteCount.textContent = checkedCount > 0 ? " (" + checkedCount + ")" : "";

    // Sync header select-all checkbox
    var chkSelectAll = document.getElementById("chk-select-all");
    if (chkSelectAll) {
        var visibleKeys = getVisibleMacroKeys();
        if (visibleKeys.length === 0) {
            chkSelectAll.checked = false;
        } else {
            var allChecked = true;
            for (var i = 0; i < visibleKeys.length; i++) {
                if (!checkedMacroNames.has(visibleKeys[i])) {
                    allChecked = false;
                    break;
                }
            }
            chkSelectAll.checked = allChecked;
        }
    }
}

function clearSelection() {
    selectedMacroName = null;
    checkedMacroNames.clear();
    lastClickedKey = null;
    lastShiftRangeKeys = null;

    var nameField = document.getElementById("macro-name");
    var contentField = document.getElementById("macro-content");

    if (nameField) nameField.value = "";
    if (contentField) contentField.value = "";

    updateCharCounter();

    var items = document.querySelectorAll(".macro-item");
    for (var i = 0; i < items.length; i++) {
        items[i].classList.remove("selected");
    }

    var chkSelectAll = document.getElementById("chk-select-all");
    if (chkSelectAll) chkSelectAll.checked = false;

    updateButtonStates();
    renderMacroList();
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

    // Check for target shortcut collision if key name was changed (case-sensitive)
    if (name !== selectedMacroName && hasMacroKey(name)) {
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
    var targets = [];
    if (checkedMacroNames.size > 0) {
        targets = Array.from(checkedMacroNames);
    } else if (selectedMacroName) {
        targets = [selectedMacroName];
    }

    if (targets.length === 0) return;

    // '\n' delimiter: a shortcut may contain any printable char (';' included),
    // but never a newline — the name field is a single-line <input>.
    document.getElementById("val-macro-name").value = targets.join("\n");
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
    lastClickedKey = null;
    lastShiftRangeKeys = null;
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

function updateCheckboxesUI() {
    var list = document.getElementById("macro-list");
    if (!list) return;
    var items = list.querySelectorAll(".macro-item");
    for (var i = 0; i < items.length; i++) {
        var name = items[i].getAttribute("data-name");
        var chk = items[i].querySelector(".chk-item");
        var isChecked = checkedMacroNames.has(name);
        if (chk) {
            chk.checked = isChecked;
        }
        if (isChecked) {
            items[i].classList.add("checked");
        } else {
            items[i].classList.remove("checked");
        }
    }
}

function applyShiftRange(currentKey) {
    var visibleKeys = getVisibleMacroKeys();
    var currentIdx = visibleKeys.indexOf(currentKey);
    var anchorIdx = lastClickedKey ? visibleKeys.indexOf(lastClickedKey) : -1;

    if (anchorIdx === -1 || currentIdx === -1) {
        checkedMacroNames.add(currentKey);
        lastClickedKey = currentKey;
        lastShiftRangeKeys = null;
        return;
    }

    var start = Math.min(anchorIdx, currentIdx);
    var end = Math.max(anchorIdx, currentIdx);
    var newRangeKeys = new Set();
    for (var k = start; k <= end; k++) {
        newRangeKeys.add(visibleKeys[k]);
    }

    // Uncheck items from previous shift range that are outside the updated range
    if (lastShiftRangeKeys) {
        var prevKeysArr = Array.from(lastShiftRangeKeys);
        for (var p = 0; p < prevKeysArr.length; p++) {
            if (!newRangeKeys.has(prevKeysArr[p])) {
                checkedMacroNames.delete(prevKeysArr[p]);
            }
        }
    }

    // Add all keys in the new range
    var newKeysArr = Array.from(newRangeKeys);
    for (var n = 0; n < newKeysArr.length; n++) {
        checkedMacroNames.add(newKeysArr[n]);
    }

    lastShiftRangeKeys = newRangeKeys;
}

function renderMacroList() {
    var list = document.getElementById("macro-list");
    var emptyEl = document.getElementById("macro-list-empty");
    if (!list) return;

    list.innerHTML = "";

    // Clean up checkedMacroNames for items that no longer exist
    var checkedArr = Array.from(checkedMacroNames);
    for (var k = 0; k < checkedArr.length; k++) {
        if (!allMacros.has(checkedArr[k])) {
            checkedMacroNames.delete(checkedArr[k]);
        }
    }

    if (selectedMacroName && !allMacros.has(selectedMacroName)) {
        selectedMacroName = null;
        var nameField = document.getElementById("macro-name");
        var contentField = document.getElementById("macro-content");
        if (nameField) nameField.value = "";
        if (contentField) contentField.value = "";
        updateCharCounter();
    }

    if (lastClickedKey && !allMacros.has(lastClickedKey)) {
        lastClickedKey = null;
        lastShiftRangeKeys = null;
    }

    var sortedKeys = Array.from(allMacros.keys()).sort();
    var matchCount = 0;

    for (var i = 0; i < sortedKeys.length; i++) {
        const name = sortedKeys[i];
        const content = allMacros.get(name);
        const displayContent = storageToDisplay(content);

        // Filter by search query on both shortcut name and content
        if (searchQuery !== "") {
            var matchName = name.toLowerCase().indexOf(searchQuery) !== -1;
            var matchContent = displayContent.toLowerCase().indexOf(searchQuery) !== -1;
            if (!matchName && !matchContent) {
                continue;
            }
        }

        matchCount++;
        const item = document.createElement("div");
        item.className = "macro-item";
        item.setAttribute("data-name", name);
        item.setAttribute("data-content", content);

        if (name === selectedMacroName) {
            item.classList.add("selected");
        }

        var isChecked = checkedMacroNames.has(name);
        if (isChecked) {
            item.classList.add("checked");
        }

        var preview = formatPreview(content);
        item.innerHTML =
            '<span class="macro-item-check"><input type="checkbox" class="chk-item"' + (isChecked ? ' checked' : '') + '></span>' +
            '<span class="macro-item-name">' + escapeHtml(name) + '</span>' +
            '<span class="macro-item-content">' + preview + '</span>';

        var tooltipText = displayContent;
        if (tooltipText.length > 500) tooltipText = tooltipText.substring(0, 500) + "...";
        item.setAttribute("title", tooltipText);

        var chk = item.querySelector(".chk-item");
        if (chk) {
            chk.addEventListener("click", function (e) {
                e.stopPropagation();
                if (e.shiftKey) {
                    applyShiftRange(name);
                } else {
                    // Ctrl+click only toggles this item. Keep the existing Shift
                    // anchor/range so the next Shift+click can still resize it.
                    if (!e.ctrlKey) {
                        lastShiftRangeKeys = null;
                        lastClickedKey = name;
                    }
                    if (this.checked) {
                        checkedMacroNames.add(name);
                    } else {
                        checkedMacroNames.delete(name);
                    }
                }
                updateCheckboxesUI();
                updateButtonStates();
            });
        }

        item.addEventListener("click", function (e) {
            if (e.shiftKey) {
                applyShiftRange(name);
                updateCheckboxesUI();
                updateButtonStates();
            } else if (e.ctrlKey) {
                if (checkedMacroNames.has(name)) {
                    checkedMacroNames.delete(name);
                } else {
                    checkedMacroNames.add(name);
                }
                updateCheckboxesUI();
                updateButtonStates();
            } else {
                lastShiftRangeKeys = null;
                lastClickedKey = name;
                selectMacroItem(item, name, content);
            }
        });

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
