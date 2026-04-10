// Convert Tool Dialog JavaScript
// Uses Sciter patterns from settings.js

document.ready = function () {
    initSubDialog();  // blur + dark + scrollbar + i18n
    initConvertToolDialog();
};

function initConvertToolDialog() {
    initializeToggles();
    initSwapButton();
    initModeSwitching();
    initBrowseButtons();
}

// Mode switching: Clipboard vs File
function initModeSwitching() {
    var container = document.getElementById("main-container");
    var modeInput = document.getElementById("val-ui-mode");

    function updateMode(value) {
        if (value === "file") {
            container.classList.add("mode-file");
        } else {
            container.classList.remove("mode-file");
        }

        // Sync with hidden inputs for C++
        if (modeInput) {
            modeInput.value = value;
            modeInput.dispatchEvent(new Event("change", { bubbles: true }));
        }
        var sourceTypeInput = document.getElementById("val-source-type");
        if (sourceTypeInput) {
            sourceTypeInput.value = value;
            sourceTypeInput.dispatchEvent(new Event("change", { bubbles: true }));
        }
    }

    // Event listener for radio buttons
    document.on("change", "input[name='source-type']", function (evt, input) {
        updateMode(input.value);
    });

    // Initial sync
    var checkedRadio = document.querySelector("input[name='source-type']:checked");
    if (checkedRadio) {
        updateMode(checkedRadio.value);
    }
}

// Browse buttons - trigger C++ actions
function initBrowseButtons() {
    var btnBrowseSource = document.getElementById("btn-browse-source");
    var btnBrowseDest = document.getElementById("btn-browse-dest");

    if (btnBrowseSource) {
        btnBrowseSource.onclick = function () {
            triggerAction("select-source-file");
        };
    }

    if (btnBrowseDest) {
        btnBrowseDest.onclick = function () {
            triggerAction("select-dest-file");
        };
    }
}

// Initialize toggle switches using Sciter pattern from settings.js
function initializeToggles() {
    var allToggles = document.querySelectorAll(".toggle-switch-small");

    allToggles.forEach(function (toggle) {
        toggle.onclick = function (evt) {
            // Check if this toggle is disabled
            if (this.classList.contains("disabled")) {
                return false;  // Ignore click on disabled toggles
            }

            // Special check for sequential toggle which depends on auto-paste
            if (this.id === "toggle-sequential") {
                var autoPasteHidden = document.getElementById("val-toggle-auto-paste");
                // If auto-paste is 0 (OFF), do not allow toggling sequential
                if (autoPasteHidden && autoPasteHidden.value === "0") {
                    return false;
                }
            }

            var isChecked = this.classList.contains("checked");

            // Toggle the visual state
            if (isChecked) {
                this.classList.remove("checked");
            } else {
                this.classList.add("checked");
            }

            var id = this.id;
            var newState = !isChecked;

            // Update the hidden input to fire VALUE_CHANGED
            var hiddenInput = document.getElementById("val-" + id);
            if (hiddenInput) {
                hiddenInput.value = newState ? "1" : "0";
                hiddenInput.dispatchEvent(new Event("change", { bubbles: true }));
            }

            // Special handling: auto-paste controls sequential
            if (id === "toggle-auto-paste") {
                updateSequentialToggleState(newState);
            }

            return true;
        };
    });

    // Initial state: check if auto-paste is ON to enable/disable sequential
    var autoPasteToggle = document.getElementById("toggle-auto-paste");
    if (autoPasteToggle) {
        var isAutoPasteOn = autoPasteToggle.classList.contains("checked");
        updateSequentialToggleState(isAutoPasteOn);
    }
}

// Update sequential toggle enabled/disabled state based on auto-paste
function updateSequentialToggleState(autoPasteEnabled) {
    var seqToggle = document.getElementById("toggle-sequential");
    var seqRow = document.getElementById("row-sequential");

    if (!seqToggle) return;

    if (autoPasteEnabled) {
        // Enable sequential toggle
        seqToggle.classList.remove("disabled");
        if (seqRow) seqRow.classList.remove("disabled");
    } else {
        // Disable sequential toggle and turn it off
        seqToggle.classList.add("disabled");
        seqToggle.classList.remove("checked");
        if (seqRow) seqRow.classList.add("disabled");

        // Also update hidden input
        var hiddenInput = document.getElementById("val-toggle-sequential");
        if (hiddenInput && hiddenInput.value === "1") {
            hiddenInput.value = "0";
            hiddenInput.dispatchEvent(new Event("change", { bubbles: true }));
        }
    }
}

// Swap button - swap dropdown values directly in JS
function initSwapButton() {
    var btnSwap = document.getElementById("btn-swap");
    if (btnSwap) {
        btnSwap.onclick = function () {
            var sourceEnc = document.getElementById("source-encoding");
            var destEnc = document.getElementById("dest-encoding");

            if (sourceEnc && destEnc) {
                // Read current values
                var srcVal = sourceEnc.value;
                var dstVal = destEnc.value;

                // Swap: source = old dest, dest = old source
                sourceEnc.value = dstVal;
                destEnc.value = srcVal;

                // Trigger change events so C++ saves to registry
                sourceEnc.dispatchEvent(new Event("change", { bubbles: true }));
                destEnc.dispatchEvent(new Event("change", { bubbles: true }));
            }
            return true;
        };
    }
}

// Hotkey input - handled by document.on event delegation below

// Handle text input changes - display "Space" for space character
document.on("change", "#hotkey-char", function (evt, input) {
    var keyChar = input.value;

    if (keyChar === " ") {
        input.value = "Space";
    } else if (keyChar === "Space") {
        // Keep "Space" displayed
    } else if (keyChar.length === 0) {
        // Empty - user deleted everything
    } else if (keyChar.length === 1) {
        // Single character - uppercase
        input.value = keyChar.toUpperCase();
    } else {
        // Partial text - clear it
        input.value = "";
    }
});

// Real-time conversion for space
document.on("input", "#hotkey-char", function (evt, input) {
    if (input.value === " ") {
        input.value = "Space";
    }
});

// Trigger action via hidden input (for C++ to detect)
function triggerAction(action) {
    var actionInput = document.getElementById("val-action");
    if (actionInput) {
        actionInput.value = action;
        var event = new Event("change", { bubbles: true });
        actionInput.dispatchEvent(event);
    }
}
