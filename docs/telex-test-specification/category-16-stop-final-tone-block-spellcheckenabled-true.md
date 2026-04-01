# Category 16: Stop-Final Tone Block (spellCheckEnabled = true)

**Purpose:** Verify that Huyền/Hỏi/Ngã keys are treated as literals when the coda
is a stop final (c, ch, k, p, t). See `docs/vietnamese-phonology-spec.md §8`.

**Note:** All tests in this category require `spellCheckEnabled = true`.

### 16.1 Blocked: Coda `c`

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| SF-01 | `ocr` | `ocr` | `StopFinal_C_Hook_Blocked` | Hook(r) + c |
| SF-02 | `ocf` | `ocf` | `StopFinal_C_Grave_Blocked` | Grave(f) + c |
| SF-03 | `ocx` | `ocx` | `StopFinal_C_Tilde_Blocked` | Tilde(x) + c |
| SF-04 | `ocs` | `ốc` | `StopFinal_C_Acute_Allowed` | Acute(s) valid |
| SF-05 | `ocj` | `ọc` | `StopFinal_C_Dot_Allowed` | Dot(j) valid |

### 16.2 Blocked: Coda `t` and `p`

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| SF-06 | `atr` | `atr` | `StopFinal_T_Hook_Blocked` | Hook(r) + t |
| SF-07 | `atf` | `atf` | `StopFinal_T_Grave_Blocked` | Grave(f) + t |
| SF-08 | `ats` | `át` | `StopFinal_T_Acute_Allowed` | Acute(s) valid |
| SF-09 | `atj` | `ạt` | `StopFinal_T_Dot_Allowed` | Dot(j) valid |
| SF-10 | `apr` | `apr` | `StopFinal_P_Hook_Blocked` | Hook(r) + p |

### 16.3 Blocked: Coda `ch`

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| SF-11 | `achr` | `achr` | `StopFinal_Ch_Hook_Blocked` | Hook(r) + ch |
| SF-12 | `achs` | `ách` | `StopFinal_Ch_Acute_Allowed` | Acute(s) valid |

### 16.4 Non-stop finals — NOT blocked

| ID | Input | Expected | Test Name | Notes |
|----|-------|----------|-----------|-------|
| SF-13 | `anr` | `ản` | `NonStopFinal_N_Hook_Allowed` | n is non-stop |
| SF-14 | `amf` | `àm` | `NonStopFinal_M_Grave_Allowed` | m is non-stop |
| SF-15 | `angx` | `ãng` | `NonStopFinal_Ng_Tilde_Allowed` | ng is non-stop |

### 16.5 Spell check OFF — no change in behavior

| ID | Input | spellCheck | Expected | Test Name |
|----|-------|-----------|----------|-----------|
| SF-16 | `ocr` | OFF | `ỏc` | `StopFinal_SpellOff_NoBlock` |

### 16.6 Updated test (behavior change)

| ID | Old behavior | New behavior | Test Name |
|----|-------------|-------------|-----------|
| SF-U1 | `ocrr` → `ocr` | `ocrr` → `ocrr` | `StopFinal_OCRR_BothLiteral` |

> **Rationale:** With fix, first `r` is literal (not a tone). No pending tone exists,
> so second `r` is also literal. The old test `ToneEscape_WithCoda_SpellCheckOn` must
> be updated.

---
