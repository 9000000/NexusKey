// Icon Settings Dialog JavaScript

document.ready = function () {
    initSubDialog();
    initializeIconToggles();
    initializeIconStyle();
};

function initializeIconToggles() {
    var toggles = document.querySelectorAll(".toggle-switch-small");
    toggles.forEach(function (toggle) {
        toggle.onclick = function () {
            var enabled = !this.classList.contains("checked");
            if (enabled) this.classList.add("checked");
            else this.classList.remove("checked");
            this.setAttribute("aria-checked", enabled ? "true" : "false");

            var hidden = document.getElementById("val-" + this.id);
            if (hidden) {
                hidden.value = enabled ? "1" : "0";
                hidden.dispatchEvent(new Event("change", { bubbles: true }));
            }

            var label = this.closest(".setting-row").querySelector(".setting-label");
            if (label && typeof showToastI18n === "function") {
                showToastI18n(
                    (enabled ? "Đã bật: " : "Đã tắt: ") + label.textContent.trim(),
                    (enabled ? "Enabled: " : "Disabled: ") + label.textContent.trim());
            }
            return true;
        };
        toggle.onkeydown = function (evt) {
            if (evt.keyCode === 13 || evt.keyCode === 32) {
                this.onclick();
                evt.preventDefault();
                return true;
            }
        };
    });
}

function initializeIconStyle() {
    var select = document.getElementById("modern-icon");
    if (!select) return;

    function updateCustomColors() {
        var row = document.getElementById("custom-color-row");
        if (row) row.style.display = String(select.value) === "3" ? "block" : "none";
        var selected = select.querySelector("option:checked");
        if (selected) select.setAttribute("title", selected.textContent);
    }

    select.addEventListener("change", updateCustomColors);
    updateCustomColors();
}
