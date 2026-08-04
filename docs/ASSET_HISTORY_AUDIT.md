# Asset History Audit

**Date:** 2026-08-03
**Scope:** every font file that has ever existed in this repository's git history
**Method:** `git log --all --diff-filter=A/D`, `git cat-file`, and unzipping the
published assets of every affected GitHub release

This record exists because presence in a distributed artifact creates licensing
liability regardless of whether anything uses the file. A Vietnamese developer
was billed roughly 500 million VND for a commercial font that had been tested
once, replaced because it could not render Vietnamese, and left behind in the
assets folder of a free app with no revenue. It never displayed anything. It was
in the APK.

Three font files have existed in this repository's history. All three are gone
from every branch. The question that matters is whether any of them reached a
published artifact, and the answer, verified below, is no.

## Fonts that were in the tree

| File | Bytes | Blob | License |
| --- | --- | --- | --- |
| `tools/ArialRoundedMTBold.ttf` | 45,260 | `20b92a27` | **Arial Rounded MT Bold — Monotype, commercial** |
| `tools/Anton-Regular.ttf` | 161,588 | `c7d74558` | Anton — Google Fonts, SIL Open Font License |
| `Sources/OpenKey/win32/OpenKey/OpenKey/Jua-Regular.ttf` | 2,101,500 | `f0943a5a` | Jua — Google Fonts, SIL Open Font License |

Only the Monotype file is a commercial typeface. The other two are freely
licensed; OFL asks that its license text accompany redistribution, it does not
ask for a fee.

## When each was present

```
tools/ArialRoundedMTBold.ttf   added 2025-12-16  95d95595  "enhance and bug fix"
tools/Anton-Regular.ttf        added 2025-12-16  95d95595  "enhance and bug fix"
both                         removed 2026-01-13  60dfff8e  "before fix review code"

Jua-Regular.ttf                added 2019-04-02  836b4c68  "Publish source code for macOS"
                                                           (arrived inside the vendored
                                                           Sources/OpenKey subtree)
                             removed 2025-11-28  41ffdbf5  "Clean up"
                                                           (removed with that subtree)
```

## Releases published during those windows

Every ZIP asset of every release published while a font was in the tree was
downloaded and listed. The count is font files found inside the archive.

| Release | Published | Assets checked | Fonts found |
| --- | --- | --- | --- |
| `Windows` | 2025-11-27 | `OpenKey.zip` | 0 |
| `NextKey` | 2025-12-13 | `OpenKey.zip` | 0 |
| `V1.0.1` | 2025-12-16 | `OpenKey.zip` | 0 |
| `V1.0.2` | 2025-12-19 | `OpenKey.zip` | 0 |
| `v1.0.3` | 2025-12-31 | `OpenKey-x64.zip`, `OpenKey-x86.zip` | 0 |
| `v1.0.3-rc` | 2026-01-01 | `OpenKey-x64.zip`, `OpenKey-x86.zip` | 0 |
| `v1.0.4-RC1` | 2026-01-06 | `OpenKey-x64.zip`, `OpenKey-x86.zip` | 0 |
| `v1.0.5-RC1` | 2026-01-14 | `NextKey-x64.zip`, `NextKey-x86.zip` | 0 |

`v1.0.5-RC1` is listed for completeness; it was published one day after the
fonts were removed.

**No published artifact has ever contained a font file.** The reason is
structural rather than lucky in one respect and lucky in another: the two
`tools/` fonts were in a development directory, and `Jua-Regular.ttf` was inside
a vendored third-party source subtree. The release payload is assembled from
built binaries only — `VKey.exe`, `VKeyTSF.dll`, `VKeyWatchdog.exe`, `sciter.dll`
— so neither location is reachable from an archive. What was lucky is that
nobody had written that down, or enforced it.

## Current state

- No font file is tracked on any branch.
- The three blobs remain reachable in this repository's public git history.
  Rewriting history would not remove them: one fork already carries the engine
  branch and GitHub's fork network shares an object store, so the copies would
  survive. This record is the honest mitigation, not deletion.
- `tools/audit/check_asset_provenance.py` now refuses font files outright and
  requires an origin for every other binary asset. It runs twice in CI: over
  tracked files, and over the assembled release payload immediately before
  archiving. A repeat of this situation fails the build.

## Related

- `THIRD_PARTY_NOTICES.txt` — notices that ship inside the release archives,
  including the Sciter.JS BSD 3-Clause text and the fluentui-emoji MIT terms.
- `tools/audit/assets_allowlist.txt` — every binary asset with its recorded
  origin. The one third-party trademark in the set, the SignPath logo, is
  recorded with its permission: use confirmed by SignPath on 2026-08-03.
- `extern/vkey_engine/LICENSE` (branch `engine/rust-engine-lib`) — the prebuilt
  engine is not AGPL-3.0 and is noncommercial-only.
