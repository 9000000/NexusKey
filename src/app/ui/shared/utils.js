// NexusKey Shared JavaScript Utilities
// Reusable functions for all Sciter dialogs

// ============================================
// SUBDIALOG INIT - Common setup for subdialog windows
// ============================================

// Common subdialog initialization (blur + dark theme + scrollbar)
function initSubDialog(scrollSelector) {
    Window.this.blurBehind = "dark source-auto";
    document.body.classList.add("dark");
    if (scrollSelector) initializeScrollbarResize(scrollSelector);
}

// Set background opacity (called from C++ via call_function)
// Shared between all dialogs that use frosted glass effect
function setBackgroundOpacity(value) {
    var opacity = value / 100;
    var container = document.getElementById("main-container");
    if (container) {
        var isDark = document.body.classList.contains("dark");
        if (isDark) {
            container.style.backgroundColor = "rgba(18, 20, 28, " + (opacity * 0.9) + ")";
        } else {
            container.style.backgroundColor = "rgba(255, 255, 255, " + opacity + ")";
        }
    }
}

// ============================================
// SCROLLBAR RESIZE - Thin when idle, normal when scrolling
// Sciter approach: Find scrollbar element and style directly
// ============================================
var scrollResizeTimeout = null;

function initializeScrollbarResize(containerSelector) {
    containerSelector = containerSelector || ".tab-body, .macro-list, .app-list";
    var containers = document.querySelectorAll(containerSelector);

    containers.forEach(function (container) {
        // Find the scrollbar element inside container
        var scrollbar = container.querySelector("scrollbar");

        if (scrollbar) {
            // Initially thin
            setScrollbarThin(scrollbar);

            // Expand on scroll
            container.addEventListener("scroll", function () {
                setScrollbarNormal(scrollbar);
                resetShrinkTimer(scrollbar);
            });

            // Expand on mouse wheel
            container.addEventListener("wheel", function () {
                setScrollbarNormal(scrollbar);
                resetShrinkTimer(scrollbar);
            });

            // Expand on hover
            container.addEventListener("mouseenter", function () {
                setScrollbarNormal(scrollbar);
            });

            // Shrink on leave
            container.addEventListener("mouseleave", function () {
                shrinkScrollbarDelayed(scrollbar, 500);
            });
        }
    });
}

function setScrollbarThin(scrollbar) {
    scrollbar.style.width = "2px";
    scrollbar.style.opacity = "0.3";
}

function setScrollbarNormal(scrollbar) {
    scrollbar.style.width = "6px";
    scrollbar.style.opacity = "1";
}

function resetShrinkTimer(scrollbar) {
    if (scrollResizeTimeout) {
        clearTimeout(scrollResizeTimeout);
        scrollResizeTimeout = null;
    }
}

function shrinkScrollbarDelayed(scrollbar, delay) {
    if (scrollResizeTimeout) {
        clearTimeout(scrollResizeTimeout);
    }
    scrollResizeTimeout = setTimeout(function () {
        setScrollbarThin(scrollbar);
    }, delay);
}

// ============================================
// RUNNING APPS DROPDOWN - Shared dropdown component
// Used by excludedapps.js and appoverrides.js
// ============================================

/**
 * Creates a running apps dropdown controller
 * @param {Object} options Configuration options
 * @param {string} options.inputId - ID of the input element
 * @param {string} options.dropdownId - ID of the dropdown container element
 * @param {function} options.triggerAction - Function to call C++ actions
 * @returns {Object} Controller object with methods
 */
function createRunningAppsDropdown(options) {
    var inputId = options.inputId || "app-name";
    var dropdownId = options.dropdownId || "running-apps-dropdown";
    var triggerAction = options.triggerAction;

    // State
    var runningApps = [];
    var runningAppsLoaded = false;
    var highlightedIndex = -1;
    var skipNextFocus = false;

    // Get elements
    function getInput() {
        return document.getElementById(inputId);
    }

    function getDropdown() {
        return document.getElementById(dropdownId);
    }

    // Show dropdown
    function show() {
        var dropdown = getDropdown();
        if (dropdown) {
            dropdown.classList.add("visible");
        }
    }

    // Hide dropdown
    function hide() {
        var dropdown = getDropdown();
        if (dropdown) {
            dropdown.classList.remove("visible");
        }
        highlightedIndex = -1;
    }

    // Filter and show dropdown based on query
    function filterAndShow(query) {
        var dropdown = getDropdown();
        if (!dropdown) return;

        dropdown.innerHTML = "";
        highlightedIndex = -1;

        var filtered = [];
        var lowerQuery = (query || "").toLowerCase();

        for (var i = 0; i < runningApps.length; i++) {
            if (lowerQuery === "" || runningApps[i].toLowerCase().indexOf(lowerQuery) !== -1) {
                filtered.push(runningApps[i]);
            }
        }

        // Show message if no apps
        if (filtered.length === 0) {
            if (runningAppsLoaded && runningApps.length === 0) {
                dropdown.innerHTML = '<div class="dropdown-empty">Không tìm thấy ứng dụng nào</div>';
            } else if (query !== "") {
                dropdown.innerHTML = '<div class="dropdown-empty">Không có kết quả phù hợp</div>';
            } else {
                hide();
                return;
            }
            show();
            return;
        }

        // Build dropdown items
        for (var j = 0; j < filtered.length; j++) {
            var item = document.createElement("div");
            item.className = "dropdown-item";
            item.textContent = filtered[j];
            item.setAttribute("data-app", filtered[j]);

            (function (appName) {
                item.addEventListener("click", function (e) {
                    e.stopPropagation();
                    selectItem(appName);
                });
            })(filtered[j]);

            dropdown.appendChild(item);
        }

        show();
    }

    // Select an item from dropdown
    function selectItem(appName) {
        var input = getInput();
        if (input) {
            input.value = appName;
        }
        hide();

        // Focus back to input, but skip showing dropdown THIS time
        if (input) {
            skipNextFocus = true;
            input.focus();
            // Reset skipNextFocus after a short delay so user can reopen by clicking again
            setTimeout(function () {
                skipNextFocus = false;
            }, 200);
        }
    }

    // Update highlight on keyboard navigation
    function updateHighlight(items) {
        for (var i = 0; i < items.length; i++) {
            if (i === highlightedIndex) {
                items[i].classList.add("highlighted");
                items[i].scrollIntoView({ block: "nearest" });
            } else {
                items[i].classList.remove("highlighted");
            }
        }
    }

    // Handle keyboard navigation
    function handleKeyboard(e) {
        var dropdown = getDropdown();
        if (!dropdown || !dropdown.classList.contains("visible")) return;

        var items = dropdown.querySelectorAll(".dropdown-item");
        if (items.length === 0) return;

        if (e.code === "ArrowDown") {
            e.preventDefault();
            highlightedIndex = (highlightedIndex + 1) % items.length;
            updateHighlight(items);
        } else if (e.code === "ArrowUp") {
            e.preventDefault();
            highlightedIndex = highlightedIndex <= 0 ? items.length - 1 : highlightedIndex - 1;
            updateHighlight(items);
        } else if (e.code === "Enter") {
            e.preventDefault();
            if (highlightedIndex >= 0 && highlightedIndex < items.length) {
                var appName = items[highlightedIndex].getAttribute("data-app");
                selectItem(appName);
            }
        }
    }

    // Initialize event listeners
    function init() {
        var input = getInput();

        if (input) {
            // Focus: Request running apps from C++ (only first time)
            input.addEventListener("focus", function () {
                if (skipNextFocus) {
                    skipNextFocus = false;
                    return;
                }
                if (!runningAppsLoaded) {
                    if (triggerAction) triggerAction("get-running-apps");
                } else {
                    filterAndShow(input.value);
                }
            });

            // Input: Filter dropdown as user types
            input.addEventListener("input", function () {
                filterAndShow(this.value);
            });

            // Keydown: Navigate dropdown with arrow keys
            input.addEventListener("keydown", function (e) {
                if (e.code === "ArrowDown" || e.code === "ArrowUp" || e.code === "Enter") {
                    handleKeyboard(e);
                } else if (e.code === "Escape") {
                    hide();
                }
            });
        }

        // Document click: Close dropdown if clicking outside
        document.addEventListener("click", function (e) {
            var input = getInput();
            var inputContainer = input ? input.parentElement : null;
            if (inputContainer && !inputContainer.contains(e.target)) {
                hide();
            }
        });
    }

    // Public API
    return {
        init: init,
        show: show,
        hide: hide,
        filterAndShow: filterAndShow,
        selectItem: selectItem,

        // Called by C++ to set the list of running apps
        setApps: function (apps) {
            runningApps = [];
            if (apps && apps.length) {
                for (var i = 0; i < apps.length; i++) {
                    runningApps.push(apps[i]);
                }
            }
            runningAppsLoaded = true;
            var input = getInput();
            if (input) {
                filterAndShow(input.value);
            }
        },

        // Reset cache (for refresh button)
        resetCache: function () {
            runningAppsLoaded = false;
            runningApps = [];
        },

        // Check if apps are loaded
        isLoaded: function () {
            return runningAppsLoaded;
        }
    };
}
