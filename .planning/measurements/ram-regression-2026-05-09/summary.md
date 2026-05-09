# Startup RAM measurement summary — 2026-05-09

Snapshots compare `NextKeyApp.exe` between **v2.1.24** and **HEAD**.

## Process Explorer numbers

Capture immediately after VMMap snapshot (same 15 s mark).

| Metric | v2.1.24 | HEAD | Δ |
|---|---|---|---|
| Private Bytes | 2256 K| 3172 K| |
| Working Set (Private) | 1632 K| 2408 K | |
| Working Set (Shareable) | 11808 K | 18672 K | |
| Working Set (Total) | 13476 K | 21112 K | |
| Threads | 2 | 7 | |
| Handles | 172 | 332 | |
| GDI Objects | 10 | 10 | |

## VMMap category totals (Total = Working Set in MB)

| Category | v2.1.24 | HEAD | Δ |
|---|---|---|---|
| Image | | | |
| Stack | | | |
| Heap | | | |
| Mapped File | | | |
| Shareable | | | |
| Private Data | | | |
| Page Table | | | |
| **Total** | | | |

## Per-section dumpbin diff (NextKeyApp.exe)

| Section | v2.1.24 size | HEAD size | Δ |
|---|---|---|---|
| .text | | | |
| .rdata | | | |
| .data | | | |
| .pdata | | | |
| .reloc | | | |
| .rsrc | | | |

## Top diff lines (>200 KB) from VMMap Compare

| Region | Type | v2.1.24 | HEAD | Δ | Suspected source |
|---|---|---|---|---|---|
| | | | | | |

## Attribution & decision

(Fill after analysis)

## Files in this folder

- `vmmap-v2.1.24-idle.mmp` — VMMap snapshot of v2.1.24 (15 s post-launch)
- `vmmap-HEAD-idle.mmp` — VMMap snapshot of HEAD
- `headers-v2.1.24.txt` — `dumpbin /HEADERS` of v2.1.24 binary
- `headers-HEAD.txt` — `dumpbin /HEADERS` of HEAD binary
- `procexp-v2.1.24.png` — Process Explorer screenshot
- `procexp-HEAD.png` — Process Explorer screenshot
- `vmmap-compare.png` — Screenshot of VMMap Compare view
- `summary.md` — this file
