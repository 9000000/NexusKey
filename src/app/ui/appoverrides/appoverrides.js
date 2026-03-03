// App Overrides Dialog JavaScript
// Handles dropdown selection and C++ communication

document.ready = function () {
    initSubDialog(".app-list");  // blur + dark + scrollbar + i18n
    initRunningAppsDropdown();
    initButtons();
    initEventDelegation();
};

// ===== Behavior Type Labels =====
var behaviorLabels = {
    0: "-",
    1: "Skip IME",
    2: "Qt/Electron"
};

var clipboardLabels = {
    "-1": "-",
    0: "Ctrl+V",
    1: "Shift+Ins"
};

// ===== Running Apps Dropdown =====
function initRunningAppsDropdown() {
    var input = document.getElementById("app-name");
    var dropdown = document.getElementById("running-apps-dropdown");
    var refreshBtn = document.getElementById("btn-refresh");

    if (!input || !dropdown) return;

    // Show dropdown on focus
    input.addEventListener("focus", function () {
        triggerAction("get-running-apps");
        setTimeout(function () {
            dropdown.classList.add("visible");
        }, 100);
    });

    // Hide dropdown when clicking outside
    document.addEventListener("click", function (e) {
        if (!input.contains(e.target) && !dropdown.contains(e.target) && !refreshBtn.contains(e.target)) {
            dropdown.classList.remove("visible");
        }
    });

    // Filter on typing
    input.addEventListener("input", function () {
        filterDropdown(input.value.toLowerCase());
    });

    // Refresh button
    if (refreshBtn) {
        refreshBtn.addEventListener("click", function (e) {
            e.stopPropagation();
            triggerAction("get-running-apps");
            input.focus();
        });
    }
}

function filterDropdown(query) {
    var dropdown = document.getElementById("running-apps-dropdown");
    var items = dropdown.querySelectorAll(".dropdown-item");

    items.forEach(function (item) {
        var text = item.textContent.toLowerCase();
        if (text.indexOf(query) >= 0) {
            item.style.display = "block";
        } else {
            item.style.display = "none";
        }
    });
}

// Called from C++ to populate dropdown
function setRunningApps(apps) {
    var dropdown = document.getElementById("running-apps-dropdown");
    if (!dropdown) return;

    dropdown.innerHTML = "";

    if (!apps || apps.length === 0) {
        var empty = document.createElement("div");
        empty.className = "dropdown-empty";
        empty.textContent = (typeof t === "function" && t("no_running_apps")) || "Không có ứng dụng nào đang chạy";
        dropdown.appendChild(empty);
        return;
    }

    apps.forEach(function (app) {
        var item = document.createElement("div");
        item.className = "dropdown-item";
        item.textContent = app;
        item.addEventListener("click", function () {
            document.getElementById("app-name").value = app;
            dropdown.classList.remove("visible");
        });
        dropdown.appendChild(item);
    });

    dropdown.classList.add("visible");
}

// ===== Get Clipboard Method from Dropdown =====
function getClipboardMethod() {
    var clipboardSelect = document.getElementById("clipboard-method");
    if (clipboardSelect) {
        return parseInt(clipboardSelect.value, 10);
    }
    return -1; // None (default)
}

// ===== Buttons =====
function initButtons() {
    var addBtn = document.getElementById("btn-add");
    var pickBtn = document.getElementById("btn-pick-window");

    if (addBtn) {
        addBtn.onclick = function () {
            onAddApp();
        };
    }

    if (pickBtn) {
        pickBtn.onclick = function () {
            triggerAction("pick-window");
        };
    }
}

function onAddApp() {
    var appName = document.getElementById("app-name").value.trim();
    if (!appName) {
        return;
    }

    var behaviorType = document.getElementById("behavior-type").value;
    var clipboardMethod = getClipboardMethod();

    // Set hidden inputs and trigger action
    document.getElementById("val-app-name").value = appName;
    document.getElementById("val-behavior-type").value = behaviorType;
    document.getElementById("val-clipboard-method").value = clipboardMethod;
    triggerAction("add-app");
}

// ===== Event Delegation for Dynamic Elements =====
function initEventDelegation() {
    // Handle delete button click using event delegation
    document.on("click", ".app-item-delete", function (evt, btn) {
        var item = btn.closest(".app-item");
        if (item) {
            var appName = item.getAttribute("data-app");
            if (appName) {
                document.getElementById("val-app-name").value = appName;
                triggerAction("delete-app");
            }
        }
        evt.stopPropagation();
    });
}

// ===== Action Trigger =====
function triggerAction(action) {
    var el = document.getElementById("val-action");
    if (el) {
        el.value = action;
        el.dispatchEvent(new Event("change", { bubbles: true }));
    }
}

// ===== App List Functions (called from C++) =====
function clearAppList() {
    var list = document.getElementById("app-list");
    if (list) list.innerHTML = "";
}

function addAppToList(appName, behaviorType, clipboardMethod) {
    var list = document.getElementById("app-list");
    if (!list) return;

    var item = document.createElement("div");
    item.className = "app-item";
    item.setAttribute("data-app", appName);

    // App name column
    var nameSpan = document.createElement("span");
    nameSpan.className = "app-item-name";
    nameSpan.textContent = appName;
    nameSpan.title = appName;
    item.appendChild(nameSpan);

    // Behavior column
    var behaviorSpan = document.createElement("span");
    behaviorSpan.className = "app-item-behavior";
    behaviorSpan.textContent = behaviorLabels[behaviorType] || "-";
    item.appendChild(behaviorSpan);

    // Clipboard column
    var clipboardSpan = document.createElement("span");
    clipboardSpan.className = "app-item-clipboard";
    clipboardSpan.textContent = clipboardLabels[clipboardMethod] || "-";
    item.appendChild(clipboardSpan);

    // Delete button (uses event delegation, no inline handler)
    var deleteBtn = document.createElement("button");
    deleteBtn.className = "app-item-delete";
    deleteBtn.textContent = "×";
    item.appendChild(deleteBtn);

    list.appendChild(item);
}

function removeAppFromList(appName) {
    var list = document.getElementById("app-list");
    if (!list) return;

    var items = list.querySelectorAll(".app-item");
    items.forEach(function (item) {
        if (item.getAttribute("data-app") === appName) {
            item.remove();
        }
    });
}

function clearInput() {
    var input = document.getElementById("app-name");
    if (input) input.value = "";

    var behaviorSelect = document.getElementById("behavior-type");
    if (behaviorSelect) behaviorSelect.value = "0";

    var clipboardSelect = document.getElementById("clipboard-method");
    if (clipboardSelect) clipboardSelect.value = "-1";
}

function forceRefresh(scrollToBottom) {
    var list = document.getElementById("app-list");
    if (list && scrollToBottom) {
        list.scrollTop = list.scrollHeight;
    }
}
