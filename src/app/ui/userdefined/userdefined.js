// User Defined Input Dialog JavaScript

document.ready = function () {
    initSubDialog(".keymap-list");
    initUserDefinedDialog();
};

function initUserDefinedDialog() {
    var btnAdd = document.getElementById("btn-add");
    var btnDelete = document.getElementById("btn-delete");
    var btnImport = document.getElementById("btn-import");
    var btnExport = document.getElementById("btn-export");
    var btnClose = document.getElementById("btn-close");
    var btnLoadTelex = document.getElementById("btn-load-telex");
    var btnLoadVni = document.getElementById("btn-load-vni");
    var keyName = document.getElementById("key-name");

    if (btnAdd) btnAdd.addEventListener("click", function () { onAddKey(); });
    if (btnDelete) btnDelete.addEventListener("click", function () { onDeleteKey(); });
    if (btnImport) btnImport.addEventListener("click", function () { triggerAction("import"); });
    if (btnExport) btnExport.addEventListener("click", function () { triggerAction("export"); });
    if (btnClose) btnClose.addEventListener("click", function () { triggerAction("close"); });
    if (btnLoadTelex) btnLoadTelex.addEventListener("click", function () { triggerAction("load_telex"); });
    if (btnLoadVni) btnLoadVni.addEventListener("click", function () { triggerAction("load_vni"); });

    if (keyName) {
        keyName.addEventListener("change", function () { updateAddButtonText(); });
        // Auto-convert to lower case and limit to 1 char
        keyName.addEventListener("input", function() {
            if (this.value.length > 1) {
                this.value = this.value.substring(0, 1);
            }
        });
    }
}

function onAddKey() {
    var keyField = document.getElementById("key-name");
    var actionField = document.getElementById("key-action");

    if (!keyField || !actionField) return;

    var key = keyField.value; // Can be a space, so don't trim if it's 1 char
    if (key.length === 0) return;
    
    var action = actionField.value;

    document.getElementById("val-key").value = key;
    document.getElementById("val-key-action").value = action;
    triggerAction("add");

    keyField.value = "";
    keyField.focus();
}

function onDeleteKey() {
    var keyField = document.getElementById("key-name");
    if (!keyField) return;

    var key = keyField.value;
    if (key.length === 0) return;

    document.getElementById("val-key").value = key;
    triggerAction("delete");

    keyField.value = "";
}

function selectKeyItem(element, key, action) {
    var items = document.querySelectorAll(".keymap-item");
    for (var i = 0; i < items.length; i++) {
        items[i].classList.remove("selected");
    }
    element.classList.add("selected");

    document.getElementById("key-name").value = key;
    document.getElementById("key-action").value = action;

    document.getElementById("btn-add").textContent = t("ud.edit") || "+ S\u1eeda";
}

function updateAddButtonText() {
    var btnAdd = document.getElementById("btn-add");
    if (btnAdd) {
        btnAdd.textContent = t("add") || "+ Th\u00eam";
    }
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
function addKeyToMap(key, action, actionLabel) {
    var list = document.getElementById("keymap-list");
    if (!list) return;

    var item = document.createElement("div");
    item.className = "keymap-item";
    item.setAttribute("data-key", key);
    item.setAttribute("data-action", action);

    var displayKey = key === " " ? "(Space)" : key;

    item.innerHTML = '<span class="keymap-item-key">' + escapeHtml(displayKey) + '</span>' +
        '<span class="keymap-item-action">' + escapeHtml(actionLabel) + '</span>';

    item.addEventListener("click", function () {
        selectKeyItem(this, key, action);
    });

    list.appendChild(item);
}

function clearKeyMap() {
    var list = document.getElementById("keymap-list");
    if (list) list.innerHTML = "";
}

function escapeHtml(text) {
    var div = document.createElement("div");
    div.textContent = text;
    return div.innerHTML;
}
