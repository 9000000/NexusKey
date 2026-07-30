# Advanced Engine Manual Installation UX

## Goal

When Advanced spell check needs `vkey_engine.dll`, VKey must support users who
block the application from accessing the Internet. Declining or failing an
automatic download must leave the application in Standard mode and must not
produce another prompt on the next VKey or Windows startup.

## Dialog and Data Flow

Replace the Yes/No message box with the same `TaskDialogIndirect` command-link
pattern used by Check Update. If a trusted engine already exists beside the
running executable, no dialog or network request occurs. Otherwise the first
dialog offers:

1. Download and install automatically.
2. Install manually using the default browser.
3. Continue with Standard spell check.

The manual option delegates the exact, versioned GitHub release URL to the
default browser with `ShellExecuteW`; VKey itself does not download the file.
Before opening the browser or instructions, the caller synchronously persists
Standard mode. A second TaskDialog explains that `vkey_engine.dll` must be
placed beside `VKey.exe`, shows the exact URL, and provides a command link to
open that directory. Selecting Advanced again performs the existing trusted
length/hash verification before loading the file.

Automatic download failures use a TaskDialog with two command links: switch to
manual installation, or continue with Standard mode. Verification failures
remain visually distinct from network and storage failures, but none of them
leave a persisted Advanced setting without an installed trusted engine.

## Persistence Contract

`Ready` is the only result that permits callers to store Advanced mode. Every
other result sets the UI selection and persisted configuration to Standard. A
`ManualRequested` result tells the caller to save Standard first, then open the
manual browser/instruction flow. At startup, any non-Ready result synchronously
changes and saves the configured level to Standard. This also covers
configurations created by older builds and failed downloads after an
engine-changing application update.

Manual installation intentionally does not watch the download folder or copy a
browser-downloaded file automatically. The user controls the file operation,
and VKey verifies the final file only when Advanced mode is requested again.

## Implementation Plan

### Task 1 — TaskDialog installation choice

Acceptance:

- Missing/untrusted engine shows Automatic, Manual, and Standard command links.
- Manual opens the exact versioned asset URL in the default browser.
- Manual instructions can open the directory containing `VKey.exe`.

Verification:

- Windows build succeeds with warnings treated as errors.
- Manual UI check confirms command-link labels and hyperlink behavior.

### Task 2 — Failure and persistence behavior

Acceptance:

- All non-Ready startup results are synchronously persisted as Standard.
- Tray, Sciter, and Classic selectors switch to and synchronously persist
  Standard unless the engine becomes Ready.
- Automatic failure offers Manual or Standard without a retry-on-startup loop.

Verification:

- Every `EnsureInstalledWithUi` caller is audited for the Ready-only contract.
- Existing configuration tests continue to pass.

### Checkpoint

- Run the focused test target and the relevant Windows cross-build.
- Review every `EnsureInstalledWithUi` call site for the Ready-only contract.
- Confirm the pre-existing `extern/vkey_engine/README.md` modification remains
  untouched.

## Risks

- `ShellExecuteW` can fail because no browser association exists. Mitigation:
  check its return value, keep the manual instructions visible, and show the
  exact GitHub release URL or directory path in a fallback message.
- The executable directory may not be writable. Mitigation: explain the target
  location and retain the existing storage-failure message for automatic mode.
- Startup has no owner window. Mitigation: preserve the Check Update dialog's
  foreground/topmost callback behavior.
