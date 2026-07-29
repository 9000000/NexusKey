"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const macroScript = fs.readFileSync(
    path.join(__dirname, "../../src/app/ui/macro/macro.js"),
    "utf8"
);

function createDialogState() {
    const sandbox = {
        document: {
            ready: null,
            getElementById: function () { return null; },
            querySelectorAll: function () { return []; }
        },
        initSubDialog: function () {}
    };

    vm.createContext(sandbox);
    vm.runInContext(macroScript, sandbox);

    for (let i = 1; i <= 8; i++) {
        const key = String(i);
        sandbox.allMacros.set(key, key);
    }
    return sandbox;
}

function checked(state) {
    return Array.from(state.checkedMacroNames).sort().join("");
}

function testRangeCanShrinkAndReverseAroundOneAnchor() {
    const state = createDialogState();

    state.setMacroChecked("4", true, false);
    state.applyShiftRange("8");
    assert.equal(checked(state), "45678");

    state.applyShiftRange("6");
    assert.equal(checked(state), "456");

    state.applyShiftRange("1");
    assert.equal(checked(state), "1234");
}

function testCtrlSelectionSurvivesRangeResize() {
    const state = createDialogState();

    state.setMacroChecked("4", true, false);
    state.applyShiftRange("6");
    state.setMacroChecked("8", true, true);

    state.applyShiftRange("8");
    assert.equal(checked(state), "45678");

    state.applyShiftRange("6");
    assert.equal(checked(state), "4568");
}

function testSelectionPredatingShiftSurvivesRangeResize() {
    const state = createDialogState();

    state.setMacroChecked("7", true, true);
    state.setMacroChecked("4", true, false);
    state.applyShiftRange("8");
    assert.equal(checked(state), "45678");

    state.applyShiftRange("6");
    assert.equal(checked(state), "4567");
}

function testCtrlExclusionSurvivesRangeResize() {
    const state = createDialogState();

    state.setMacroChecked("4", true, false);
    state.applyShiftRange("8");
    state.setMacroChecked("6", false, true);
    assert.equal(checked(state), "4578");

    state.applyShiftRange("6");
    assert.equal(checked(state), "45");

    state.applyShiftRange("8");
    assert.equal(checked(state), "4578");
}

function testFilterBoundaryKeepsChecksButRequiresFreshAnchor() {
    const state = createDialogState();

    state.setMacroChecked("4", true, false);
    state.applyShiftRange("8");
    state.resetShiftSelectionSession(true);

    assert.equal(checked(state), "45678");
    assert.equal(state.lastClickedKey, null);
    assert.equal(state.shiftBaseMacroNames, null);
    assert.equal(state.shiftCtrlOverrides, null);

    state.applyShiftRange("5");
    assert.equal(checked(state), "45678");
    assert.equal(state.lastClickedKey, "5");
    assert.equal(state.shiftBaseMacroNames, null);
}

function testPlainCheckboxClickEndsShiftSession() {
    const state = createDialogState();

    state.setMacroChecked("4", true, false);
    state.applyShiftRange("8");
    state.setMacroChecked("6", false, false);

    assert.equal(checked(state), "4578");
    assert.equal(state.lastClickedKey, "6");
    assert.equal(state.shiftBaseMacroNames, null);
    assert.equal(state.shiftCtrlOverrides, null);
}

testRangeCanShrinkAndReverseAroundOneAnchor();
testCtrlSelectionSurvivesRangeResize();
testSelectionPredatingShiftSurvivesRangeResize();
testCtrlExclusionSurvivesRangeResize();
testFilterBoundaryKeepsChecksButRequiresFreshAnchor();
testPlainCheckboxClickEndsShiftSession();

console.log("Macro selection tests passed");
