# AV False Positive Reduction — Design Spec

## Problem

Windows Defender quarantines NexusKey EXE (likely Wacatac ML heuristic). SmartScreen blocks download/first run. No code signing available.

## Constraints

- No Authenticode code signing (cost prohibitive, repo too small for free programs)
- Releases are frequent — manual Microsoft submission per release not feasible
- App uses keyboard hooks — inherently suspicious to AV ML scanners

## Solution: PE Metadata Hardening + CI Attestation

### Part 1: PE Metadata Hardening

#### 1a. Add `trustInfo` to manifest files

Both `src/app/NexusKey.exe.manifest` and `src/app/classic/NexusKeyLite.exe.manifest` are missing the `trustInfo` block. A missing `requestedExecutionLevel` is a red flag for AV heuristics.

Add to both manifests:

```xml
<trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
  <security>
    <requestedPrivileges>
      <requestedExecutionLevel level="asInvoker" uiAccess="false"/>
    </requestedPrivileges>
  </security>
</trustInfo>
```

#### 1b. Add VERSIONINFO to TSF DLL

`src/tsf/NexusKeyTSF.rc` currently has only icon entries — no `VS_VERSION_INFO` block. A DLL without version metadata is a red flag for AV scanners.

Add a full `VS_VERSION_INFO` block matching the EXE format:
- CompanyName: "PhatMT97"
- FileDescription: "NexusKey TSF Input Processor"
- ProductName: "NexusKey"
- FileType: VFT_DLL

#### 1c. Linker flags for PE legitimacy

Add to `CMakeLists.txt` for MSVC Release builds:

- `/RELEASE` — computes PE checksum. Legitimate software always has a valid checksum; malware/packed binaries typically don't.
- `/SUBSYSTEM:WINDOWS,6.01` — explicit subsystem version (target Win7+). Default omission looks suspicious.

#### 1d. Build with RelWithDebInfo instead of Release

Change CI from `--config Release` to `--config RelWithDebInfo`:
- Keeps PDB debug directory entry in binary (proves it's developed software)
- Still has `/O2` optimization
- Stripped/no-symbols binaries are a strong ML heuristic signal for malware

Ship PDB alongside EXE in release artifacts (optional but beneficial).

### Part 2: GitHub Actions Attestation (Sigstore)

#### 2a. Add attestation steps to `build.yml`

After building release zips, before publishing:

```yaml
- name: Attest NexusKey
  uses: actions/attest-build-provenance@v2
  with:
    subject-path: NexusKey.zip

- name: Attest NexusKey Classic
  uses: actions/attest-build-provenance@v2
  with:
    subject-path: NexusKeyClassic.zip
```

#### 2b. Add required permissions

```yaml
permissions:
  contents: write
  id-token: write
  attestations: write
```

#### 2c. Impact

- Creates cryptographic proof: binary was built from specific repo/commit by CI
- GitHub shows attestation badge on release page
- Users can verify: `gh attestation verify NexusKey.zip --repo PhatMT/NexusKey`
- SmartScreen/Defender don't check sigstore yet, but Microsoft is evaluating integration

## Files Changed

| File | Change |
|------|--------|
| `src/app/NexusKey.exe.manifest` | Add `trustInfo` block |
| `src/app/classic/NexusKeyLite.exe.manifest` | Add `trustInfo` block |
| `src/tsf/NexusKeyTSF.rc` | Add `VS_VERSION_INFO` block |
| `CMakeLists.txt` | Add `/RELEASE`, `/SUBSYSTEM:WINDOWS,6.01` linker flags |
| `.github/workflows/build.yml` | Change Release→RelWithDebInfo, add attestation steps + permissions |

## What This Does NOT Solve

- SmartScreen reputation is primarily download-count based — new hashes always start with low reputation
- Defender ML detection is probabilistic — metadata helps but keyboard hook behavior may still trigger
- For complete solution, code signing is needed (future consideration)

## Expected Impact

- **Defender quarantine**: Reduced — richer PE metadata + symbols = lower ML suspicion score
- **SmartScreen**: Marginal improvement — attestation badge builds user trust, but reputation still needs downloads
- **Cost**: Zero (code changes + free GitHub Actions feature)
