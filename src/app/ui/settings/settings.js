// NexusKey Settings JavaScript
// Handles div-based toggles with hidden inputs for VALUE_CHANGED events

document.on("ready", function () {
    // Enable blur-behind effect (Sciter built-in)
    // "dark" = dark tint, "source-auto" = automatic blur source
    Window.this.blurBehind = "dark source-auto";

    // Apply i18n translations
    if (typeof applyTranslations === "function") applyTranslations();

    initializeToggles();
    initializeAdvancedPanel();
    initializeOpacitySlider();
    initializeScrollbarResize(".tab-body");
    initializeSwitchKeyDisplay();
    initializeTabPanels();
});

// Initialize switch key input - convert space char to "Space" display
function initializeSwitchKeyDisplay() {
    var input = document.getElementById("switch-key-char");
    if (input && input.value === " ") {
        input.value = "Space";
    }
}

// ============================================
// THEME MANAGEMENT - Called from C++
// ============================================
// setTheme(isDark) - Called from C++ to apply dark/light theme
// This avoids using eval() for security and clean code
function setTheme(isDark) {
    if (isDark) {
        document.body.classList.add("dark");
    } else {
        document.body.classList.remove("dark");
    }

    // Update background color based on theme and current opacity
    updateBackgroundForTheme(isDark);

    // Fix update-card toggle thumb color in dark mode
    fixUpdateToggleThumb();
}

// Force update-card toggle thumb to be white (workaround for Sciter CSS quirk)
function fixUpdateToggleThumb() {
    var thumb = document.getElementById("update-toggle-thumb");
    if (thumb) {
        thumb.style.backgroundColor = "#FFFFFF";
        thumb.style.background = "#FFFFFF";
    }
}

// Helper to update background rgba based on theme
function updateBackgroundForTheme(isDark) {
    var container = document.getElementById("main-container");
    var hiddenInput = document.getElementById("val-bg-opacity");
    if (container && hiddenInput) {
        var opacity = parseInt(hiddenInput.value || "80") / 100;
        if (isDark) {
            // Semi-transparent blue-gray for frosted glass
            container.style.backgroundColor = "rgba(18, 20, 28, " + (opacity * 0.9) + ")";
        } else {
            container.style.backgroundColor = "rgba(255, 255, 255, " + opacity + ")";
        }
    }
}


function initializeToggles() {
    // Get all toggle elements and attach handlers
    const allToggles = document.querySelectorAll(".toggle-switch, .toggle-switch-small");

    allToggles.forEach(function (toggle) {
        toggle.onclick = function (evt) {
            const isChecked = this.classList.contains("checked");

            // Toggle the visual state
            if (isChecked) {
                this.classList.remove("checked");
            } else {
                this.classList.add("checked");
            }

            const id = this.id;
            const newState = !isChecked;

            // English UI toggle: switch language immediately in JS
            if (id === "english-ui") {
                document.documentElement.setAttribute("lang", newState ? "en" : "vi");
                applyTranslations();
            }

            // Show-advanced toggle: update container class
            if (id === "show-advanced") {
                const container = document.getElementById("main-container");
                if (container) {
                    if (newState) container.classList.add("expanded");
                    else container.classList.remove("expanded");

                    // Force sync layout before C++ measures DOM
                    Window.this.update();
                }
            }

            // Update the hidden input to fire VALUE_CHANGED
            const hiddenInput = document.getElementById("val-" + id);
            if (hiddenInput) {
                hiddenInput.value = newState ? "1" : "0";
                hiddenInput.dispatchEvent(new Event("change", { bubbles: true }));
            }

            return true;
        };
    });
}

// Initialize advanced panel toggle and tabs
function initializeAdvancedPanel() {


    // Tab switching
    const tabItems = document.querySelectorAll(".tab-item");
    tabItems.forEach(function (tab) {
        tab.onclick = function () {
            switchTab(this.getAttribute("data-tab"));
            return true;
        };
    });

}

// Initialize tab panels with Sciter native state (first tab expanded)
function initializeTabPanels() {
    const tabPanels = document.querySelectorAll(".tab-panel");
    tabPanels.forEach(function (panel, index) {
        if (index === 0) {
            panel.state.expanded = true;
            panel.state.collapsed = false;
        } else {
            panel.state.expanded = false;
            panel.state.collapsed = true;
        }
    });
}

// Note: Advanced settings toggle uses div-based toggle with hidden input
// JS updates container class and hidden input, C++ catches VALUE_CHANGED

// Toggle advanced settings panel expansion
function toggleAdvancedSettings() {

    const container = document.getElementById("main-container");
    if (container) {
        const isExpanded = container.classList.contains("expanded");

        if (isExpanded) {
            container.classList.remove("expanded");

        } else {
            container.classList.add("expanded");

        }

        // IMMEDIATE synchronous force reflow on toggles
        // Do it as fast as possible to minimize visible flash
        const toggles = document.querySelectorAll(".toggle-switch, .toggle-switch-small");
        toggles.forEach(function (toggle) {
            toggle.style.display = "none";
        });
        // Force synchronous reflow by reading offsetHeight
        container.offsetHeight;
        toggles.forEach(function (toggle) {
            toggle.style.display = "";
        });

        // Notify C++ to resize window
        const hiddenInput = document.getElementById("val-expand-state");
        if (hiddenInput) {
            hiddenInput.value = !isExpanded ? "1" : "0";
            hiddenInput.dispatchEvent(new Event("change", { bubbles: true }));
        }
    }
}
// Switch between tabs - using Sciter native state pattern
function switchTab(tabIndex) {
    // Update active tab header (visual only, use classList)
    const tabItems = document.querySelectorAll(".tab-item");
    tabItems.forEach(function (tab) {
        if (tab.getAttribute("data-tab") === tabIndex) {
            tab.classList.add("active");
        } else {
            tab.classList.remove("active");
        }
    });

    // Update tab panels using Sciter native state (no flicker)
    const tabPanels = document.querySelectorAll(".tab-panel");
    tabPanels.forEach(function (panel) {
        const panelIndex = panel.id.replace("tab-panel-", "");
        if (panelIndex === tabIndex) {
            panel.state.expanded = true;
            panel.state.collapsed = false;
        } else {
            panel.state.expanded = false;
            panel.state.collapsed = true;
        }
    });

    // Notify C++ to recalculate window size for new tab content
    const tabChangeInput = document.getElementById("val-tab-change");
    if (tabChangeInput) {
        tabChangeInput.value = tabIndex;
        tabChangeInput.dispatchEvent(new Event("change", { bubbles: true }));
    }
}

// Handle dropdown changes (already works via C++ VALUE_CHANGED handler)
document.on("change", "select", function (evt, select) {
    const id = select.id || select.getAttribute("id");
    const value = parseInt(select.value);

});

// Handle text input changes - display "Space" for space character
document.on("change", "#switch-key-char", function (evt, input) {
    let char = input.value;

    if (char === " ") {
        // Space character typed - display "Space"
        input.value = "Space";
    } else if (char === "Space") {
        // Full "Space" text - keep it (already displayed correctly)
    } else if (char.length === 0) {
        // Empty - user deleted everything
    } else if (char.length === 1) {
        // Single character input - uppercase it
        input.value = char.toUpperCase();
    } else {
        // Partial text (like "Spac", "Sp" from deletion) - clear it
        input.value = "";
    }
});

// Real-time conversion while typing space
document.on("input", "#switch-key-char", function (evt, input) {
    if (input.value === " ") {
        input.value = "Space";
    }
});

// Handle icon dropdown change - show/hide custom color row
document.on("change", "#modern-icon", function (evt, select) {
    var colorRow = document.getElementById("custom-color-row");
    if (colorRow) {
        var value = select.value;
        // Show color row only when Custom (value=3) is selected
        colorRow.style.display = (value == "3" || value == 3) ? "flex" : "none";

        // Notify C++ to recalculate window size for the changed row
        // Use setTimeout to let Sciter update the style attribute before recalc
        setTimeout(function () {
            const tabChangeInput = document.getElementById("val-tab-change");
            if (tabChangeInput) {
                tabChangeInput.value = "icon-change";
                tabChangeInput.dispatchEvent(new Event("change", { bubbles: true }));
            }
        }, 50);
    }
});

// Handle button clicks
document.on("click", "button", function (evt, button) {
    const id = button.id || button.getAttribute("id");
    // Color buttons (btn-color-v, btn-color-e, btn-reset-colors) are handled by C++
    // which opens Windows ChooseColor dialog
});

// ============================================
// CUSTOM OPACITY SLIDER - Background Transparency
// ============================================
var sliderDragging = false;

function initializeOpacitySlider() {
    var slider = document.getElementById("bg-opacity-slider");
    var thumb = document.getElementById("bg-opacity-thumb");
    var fill = document.getElementById("bg-opacity-fill");
    var valueLabel = document.getElementById("bg-opacity-value");
    var hiddenInput = document.getElementById("val-bg-opacity");

    if (!slider || !thumb) return;

    // Click on track to set value
    slider.onmousedown = function (evt) {
        sliderDragging = true;
        updateSliderFromMouse(evt, slider, thumb, fill, valueLabel, hiddenInput);
    };

    // Drag thumb
    document.onmousemove = function (evt) {
        if (sliderDragging) {
            updateSliderFromMouse(evt, slider, thumb, fill, valueLabel, hiddenInput);
        }
    };

    document.onmouseup = function (evt) {
        if (sliderDragging) {
            sliderDragging = false;
            // Save to C++ on release
            hiddenInput.dispatchEvent(new Event("change", { bubbles: true }));
        }
    };
}

function updateSliderFromMouse(evt, slider, thumb, fill, valueLabel, hiddenInput) {
    var rect = slider.getBoundingClientRect();
    var x = evt.clientX - rect.left;
    var width = rect.width;

    // Clamp to 0-100%
    var percent = Math.max(0, Math.min(100, (x / width) * 100));
    var value = Math.round(percent);

    // Update UI
    thumb.style.left = percent + "%";
    fill.style.width = percent + "%";
    valueLabel.textContent = value + "%";
    hiddenInput.value = value.toString();

    // Apply background immediately (respect dark/light mode)
    var opacity = value / 100;
    var container = document.getElementById("main-container");
    if (container) {
        var isDark = document.body.classList.contains("dark");
        if (isDark) {
            // Semi-transparent blue-gray for frosted glass effect
            container.style.backgroundColor = "rgba(18, 20, 28, " + (opacity * 0.9) + ")";
        } else {
            container.style.backgroundColor = "rgba(255, 255, 255, " + opacity + ")";
        }
    }
}

// Called from C++ to set initial opacity value
function setBackgroundOpacity(value) {
    var thumb = document.getElementById("bg-opacity-thumb");
    var fill = document.getElementById("bg-opacity-fill");
    var valueLabel = document.getElementById("bg-opacity-value");
    var hiddenInput = document.getElementById("val-bg-opacity");

    if (thumb && fill) {
        thumb.style.left = value + "%";
        fill.style.width = value + "%";
        valueLabel.textContent = value + "%";
        hiddenInput.value = value.toString();

        // Apply background directly on container (respect dark/light mode)
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
}
