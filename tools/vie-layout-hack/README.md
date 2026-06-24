# Vietnamese layout → US physical key map

VKey's TSF profile is registered under Vietnamese (LANGID `0x042A`) so the Windows
language indicator shows **VIE – VKey** instead of ENG. VKey itself produces Vietnamese
from a US QWERTY base (Telex/VNI), so the *physical* key map must stay US — but Windows'
stock Vietnamese layout (`KBDVNTC.DLL`) remaps the number row to tone vowels (1→ă, 2→â,
3→ê, 4→ô …), which breaks typing.

These `.reg` files repoint the Vietnamese layout (`0000042a`) at `KBDUS.DLL`, system-wide.
This is the reliable equivalent of the per-TIP substitute keyboard layout, which Windows
did **not** honor in testing.

## Apply

1. *(Recommended)* Back up the current value from an **Admin** Command Prompt:
   ```cmd
   reg export "HKLM\SYSTEM\CurrentControlSet\Control\Keyboard Layouts\0000042a" backup-0000042a.reg
   ```
2. Double-click **`enable-us-for-vietnamese.reg`** and accept the prompt.
3. **Sign out and sign in** (or reboot). The number row then types digits under VIE.

## Revert

Run **`restore-vietnamese.reg`** (restores `KBDVNTC.DLL` / `"Vietnamese"`), then sign out /
sign in. If you made the backup in step 1, importing `backup-0000042a.reg` is the exact
restore.

## Notes

- Affects the Vietnamese layout for the **whole machine**, not just VKey. Fine if your only
  use of Vietnamese is through VKey; revert if you also need the real Vietnamese hardware
  layout.
- Requires admin (writes `HKLM`). The sign-out/in is mandatory — the keyboard-layout DLL is
  bound at session start, so the change is not live until then.

## Known & accepted: two "VIE" entries in Win+Space

Registering VKey under Vietnamese (`0x042A`) — which is what gives the **VIE** pill — makes
Windows show a **second** "Vietnamese" entry next to *VKey Vietnamese IME*. This is a Windows
behaviour: any TIP enabled under a language that isn't in your *Preferred Languages* gets a
synthesised "base" entry beside it.

This was investigated thoroughly (2026-06-24) and **accepted as a limitation, not a bug**:

- A per-TIP US `SubstituteLayout` is declared in `RegisterTIP()` (the trick MS's own IMEs
  use to show one entry). On Win11 it was written correctly but Windows still did **not**
  merge the two — confirmed via registry.
- The VIE pill is welded to `0x042A`; you cannot relabel a `0x0409` (ENG) profile as "VIE"
  (the pill is the global locale abbreviation). So "single entry **and** VIE pill" is not
  achievable here.
- The only single-entry option is registering under `0x0409` (ENG pill) — rejected because
  VKey's own red-V/blue-E tray icon is the real indicator and the VIE label is wanted.

**The 2nd entry is harmless**: selecting it gives a plain US layout (no Telex). Use the
*VKey Vietnamese IME* entry. Do not spend more time trying to merge them.
