# Macro Selection State

## Problem

The macro dialog originally stored only the checked names, the last anchor and
the most recent Shift range. Shrinking that range removed every old range
member outside the new endpoints. That also removed a macro which had been
selected independently with Ctrl before the range covered it.

Changing the search filter cleared only the remembered range. The checked rows
and anchor survived, so the next Shift click looked like a resize but could not
remove any part of the old range. Hidden rows also remained delete targets.

## Decision

One Shift session has three layers:

- `shiftBaseMacroNames`: snapshot of all checked macros before its first range.
- `lastShiftRangeKeys`: the current inclusive range between anchor and endpoint.
- `shiftCtrlOverrides`: explicit one-row states chosen with Ctrl during the
  session.

Each Shift resize rebuilds the checked set as:

`base selection ∪ current range`, followed by the Ctrl overrides.

This keeps the range continuous by default while preserving deliberate
selection and deselection outside that geometry. A normal checkbox click ends
the session, toggles only that row and establishes the next anchor. Ctrl does
not move the anchor.

A filter change preserves checked macros but ends the Shift session and clears
its anchor. The first Shift click in the new visible list therefore establishes
a fresh anchor rather than silently resizing a range indexed against another
list.

## Verification

The Node regression suite loads the production `macro.js` in a small DOM stub
and covers:

- `4..8 → 4..6 → 1..4`;
- a Ctrl-selected row surviving range expansion and contraction;
- a Ctrl-deselected hole surviving later range changes;
- filter changes preserving checks while clearing range and anchor state;
- a normal checkbox click ending the current Shift session.

CTest registers this suite when Node is available. Sciter still owns rendering;
the tests exercise only the selection state machine.
