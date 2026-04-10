// Spell Check Exclusions Dialog JavaScript

document.ready = function () {
    initSubDialog(".app-list");
    initSpellExclusionsDialog();
};

function initSpellExclusionsDialog() {
    var btnAdd = document.getElementById("btn-add");
    var btnClose = document.getElementById("btn-close");
    var entryInput = document.getElementById("entry-name");

    if (btnAdd) {
        btnAdd.addEventListener("click", function () {
            onAdd();
        });
    }

    if (btnClose) {
        btnClose.addEventListener("click", function () {
            triggerAction("close");
        });
    }

    // Enter key in input field triggers add
    if (entryInput) {
        entryInput.addEventListener("keydown", function (evt) {
            if (evt.keyCode === 13) {
                onAdd();
                evt.preventDefault();
            }
        });
    }

    // Event delegation for delete button clicks
    document.on("click", ".app-item-delete", function (evt, btn) {
        var item = btn.closest(".app-item");
        if (item) {
            var name = item.getAttribute("data-name");
            if (name) {
                onDelete(name);
            }
        }
        evt.stopPropagation();
    });

    var btnImport = document.getElementById("btn-import");
    var btnExport = document.getElementById("btn-export");

    if (btnImport) {
        btnImport.addEventListener("click", function () {
            triggerAction("import");
        });
    }

    if (btnExport) {
        btnExport.addEventListener("click", function () {
            triggerAction("export");
        });
    }
}

function onAdd() {
    var nameField = document.getElementById("entry-name");
    if (!nameField) return;

    var name = nameField.value.trim();
    if (name.length < 2) return;

    document.getElementById("val-entry-name").value = name;
    triggerAction("add");

    nameField.value = "";
    nameField.focus();
}

function onDelete(name) {
    if (!name) return;
    document.getElementById("val-entry-name").value = name;
    triggerAction("delete");
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
function addToList(name) {
    var list = document.getElementById("entry-list");
    if (!list) return;

    var item = document.createElement("div");
    item.className = "app-item";
    item.setAttribute("data-name", name);

    var nameSpan = document.createElement("span");
    nameSpan.className = "app-item-name";
    nameSpan.textContent = name;
    item.appendChild(nameSpan);

    var deleteBtn = document.createElement("button");
    deleteBtn.className = "app-item-delete";
    deleteBtn.textContent = "\u00d7";
    item.appendChild(deleteBtn);

    list.appendChild(item);
}

// Called by C++ to remove a single item
function removeFromList(name) {
    var list = document.getElementById("entry-list");
    if (!list) return;

    var items = list.querySelectorAll(".app-item");
    for (var i = 0; i < items.length; i++) {
        if (items[i].getAttribute("data-name") === name) {
            items[i].remove();
            break;
        }
    }
}

// Called by C++ to clear the list
function clearList() {
    var list = document.getElementById("entry-list");
    if (list) {
        list.innerHTML = "";
    }
}

// Called by C++ to force visual refresh
function forceRefresh() {
    var list = document.getElementById("entry-list");
    if (list) {
        list.scrollTop = list.scrollHeight;
    }
}
