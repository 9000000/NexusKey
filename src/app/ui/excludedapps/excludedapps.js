// Excluded Apps Dialog JavaScript

// Global dropdown controller instance
var dropdownController = null;

document.ready = function () {
    initSubDialog(".app-list");
    initExcludedAppsDialog();
};

function initExcludedAppsDialog() {
    // Initialize dropdown using shared component
    dropdownController = createRunningAppsDropdown({
        inputId: "app-name",
        dropdownId: "running-apps-dropdown",
        triggerAction: triggerAction
    });
    dropdownController.init();

    // Bind button clicks
    var btnAddManual = document.getElementById("btn-add-manual");
    var btnAddCurrent = document.getElementById("btn-add-current");
    var btnDelete = document.getElementById("btn-delete");
    var btnClose = document.getElementById("btn-close");
    var btnRefresh = document.getElementById("btn-refresh");

    if (btnAddManual) {
        btnAddManual.addEventListener("click", function () {
            onAddManual();
        });
    }

    if (btnAddCurrent) {
        btnAddCurrent.addEventListener("click", function () {
            triggerAction("add-current");
        });
    }

    if (btnClose) {
        btnClose.addEventListener("click", function () {
            triggerAction("close");
        });
    }

    // Refresh button: Force reload running apps
    if (btnRefresh) {
        btnRefresh.addEventListener("click", function () {
            dropdownController.resetCache();
            triggerAction("get-running-apps");
        });
    }

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

    // Event delegation for delete button clicks in app list
    document.on("click", ".app-item-delete", function (evt, btn) {
        var item = btn.closest(".app-item");
        if (item) {
            var appName = item.getAttribute("data-name");
            if (appName) {
                onDeleteApp(appName);
            }
        }
        evt.stopPropagation();
    });

    // Event delegation for mode selector changes in app list
    document.on("change", ".app-item-mode", function (evt, select) {
        var item = select.closest(".app-item");
        if (item) {
            var appName = item.getAttribute("data-name");
            var newMode = select.value;
            if (appName && newMode) {
                item.setAttribute("data-mode", newMode);
                onSetMode(appName, newMode);
            }
        }
        evt.stopPropagation();
    });
}

// Called by C++ to set the list of running apps
function setRunningApps(apps) {
    if (dropdownController) {
        dropdownController.setApps(apps);
    }
}

function onAddManual() {
    var nameField = document.getElementById("app-name");

    if (!nameField) return;

    var name = nameField.value.trim();

    if (name === "") {
        return;
    }

    // Set hidden inputs for C++ to read
    document.getElementById("val-app-name").value = name;
    triggerAction("add-manual");

    // Clear input after add
    nameField.value = "";
    nameField.focus();

    // Show dropdown again so user can continue adding apps
    setTimeout(function () {
        if (dropdownController) {
            dropdownController.filterAndShow("");
        }
    }, 100);
}

function onDeleteApp(name) {
    if (!name) return;

    document.getElementById("val-app-name").value = name;
    triggerAction("delete");
}

function onSetMode(name, mode) {
    if (!name || !mode) return;

    document.getElementById("val-app-name").value = name;
    document.getElementById("val-app-mode").value = mode;
    triggerAction("set-mode");
}

// Clear input field and selection - called by C++ after window picker add
function clearInput() {
    var nameField = document.getElementById("app-name");
    if (nameField) {
        nameField.value = "";
    }

    // Also clear selection
    var items = document.querySelectorAll(".app-item.selected");
    for (var i = 0; i < items.length; i++) {
        items[i].classList.remove("selected");
    }
}

function selectAppItem(element, name) {
    // Remove selected class from all items
    var items = document.querySelectorAll(".app-item");
    for (var i = 0; i < items.length; i++) {
        items[i].classList.remove("selected");
    }

    // Add selected class to clicked item
    element.classList.add("selected");

    // Fill input field
    document.getElementById("app-name").value = name;
}

function triggerAction(action) {
    var actionInput = document.getElementById("val-action");
    if (actionInput) {
        actionInput.value = action;
        // Dispatch change event for C++ to detect
        var event = new Event("change", { bubbles: true });
        actionInput.dispatchEvent(event);
    }
}

// Called by C++ to add items to the list
function addAppToList(name, mode) {
    var list = document.getElementById("app-list");
    if (!list) return;

    if (!mode) mode = "hard";

    var item = document.createElement("div");
    item.className = "app-item";
    item.setAttribute("data-name", name);
    item.setAttribute("data-mode", mode);

    // Create name span with tooltip
    var nameSpan = document.createElement("span");
    nameSpan.className = "app-item-name";
    nameSpan.textContent = name;
    nameSpan.setAttribute("title", name);  // Tooltip shows full name on hover
    item.appendChild(nameSpan);

    // Mode selector
    var modeSelect = document.createElement("select");
    modeSelect.className = "app-item-mode";

    var optHard = document.createElement("option");
    optHard.value = "hard";
    optHard.textContent = "Hard";
    modeSelect.appendChild(optHard);

    var optSoft = document.createElement("option");
    optSoft.value = "soft";
    optSoft.textContent = "Soft";
    modeSelect.appendChild(optSoft);

    modeSelect.value = mode;
    item.appendChild(modeSelect);

    // Delete button (× icon)
    var deleteBtn = document.createElement("button");
    deleteBtn.className = "app-item-delete";
    deleteBtn.textContent = "\u00d7";
    item.appendChild(deleteBtn);

    list.appendChild(item);
}

// Called by C++ to remove a single item without full reload
function removeAppFromList(name) {
    var list = document.getElementById("app-list");
    if (!list) return;

    var items = list.querySelectorAll('.app-item');
    for (var i = 0; i < items.length; i++) {
        if (items[i].getAttribute('data-name') === name) {
            items[i].remove();
            break;
        }
    }
}

// Called by C++ to clear the list before refreshing
function clearAppList() {
    var list = document.getElementById("app-list");
    if (list) {
        list.innerHTML = "";
    }
}

// Called by C++ after updating list to force Sciter to refresh visuals
function forceRefresh() {
    var list = document.getElementById("app-list");
    if (list) {
        // Save original display, hide, force reflow, restore
        var origDisplay = list.style.display || "";
        list.style.display = "none";
        void list.offsetHeight;  // Force reflow - void to ensure execution
        list.style.display = origDisplay || "block";

        // Also scroll to bottom to show new items
        list.scrollTop = list.scrollHeight;
    }
}

function escapeHtml(text) {
    var div = document.createElement("div");
    div.textContent = text;
    return div.innerHTML;
}

// setBackgroundOpacity is provided by shared/utils.js
