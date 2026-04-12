# AV False Positive Reduction — Design Spec

## Problem

Windows Defender quarantines NexusKey EXE (likely Wacatac ML heuristic). SmartScreen blocks download/first run. No code signing available.

## Constraints

- No Authenticode code signing (cost prohibitive, repo too small for free programs)
- Releases are frequent — manual Microsoft submission per release not feasible
- App uses keyboard hooks — inherently suspicious to AV ML scanners

## Solution: PE Metadata Hardening + CI Signing & Attestation

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

### Part 2: CI Signing & Attestation (Cosign + GitHub Attestation)

#### 2a. Cosign keyless signing (`sign-blob`)

Sign release artifacts with `cosign sign-blob` — creates detached `.sigstore.json` bundles shipped alongside binaries.

- Keyless: uses GitHub OIDC identity, no keys to manage
- Certificate identity encodes exact workflow + repo + ref
- Signatures recorded in Rekor public transparency log

```yaml
- name: Install Cosign
  uses: sigstore/cosign-installer@v3

- name: Sign NexusKey.zip
  continue-on-error: true    # Don't block release if Sigstore infra is down
  run: |
    cosign sign-blob NexusKey.zip \
      --bundle NexusKey.zip.sigstore.json --yes

- name: Sign NexusKeyClassic.zip
  continue-on-error: true
  run: |
    cosign sign-blob NexusKeyClassic.zip \
      --bundle NexusKeyClassic.zip.sigstore.json --yes
```

#### 2b. GitHub native attestation

Complementary to cosign — stores structured SLSA provenance in GitHub's attestation registry.

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

#### 2c. Required permissions

```yaml
permissions:
  contents: write
  id-token: write       # OIDC token for cosign keyless signing
  attestations: write   # GitHub native attestation
```

#### 2d. Caveats

- **Rekor is public**: artifact hashes, repo name, workflow path are publicly logged
- **Certificate identity tied to workflow path + ref**: renaming `build.yml` or changing branch invalidates old verification commands
- **Ship `.sigstore.json` alongside binaries**: user needs both files to verify
- **`continue-on-error: true`**: Sigstore infra down must not block releases
- **No Defender/SmartScreen impact**: Fulcio root cert not in Windows trust store, signatures are detached (not Authenticode)

#### 2e. User verification

```bash
# Via GitHub CLI
gh attestation verify NexusKey.zip --repo PhatMT/NexusKey

# Via cosign
cosign verify-blob NexusKey.zip \
  --bundle NexusKey.zip.sigstore.json \
  --certificate-identity=https://github.com/PhatMT/NexusKey/.github/workflows/build.yml@refs/tags/v2.1.12 \
  --certificate-oidc-issuer=https://token.actions.githubusercontent.com
```

Add a "Verify Release" section to README after implementation.

## Files Changed

| File | Change |
|------|--------|
| `src/app/NexusKey.exe.manifest` | Add `trustInfo` block |
| `src/app/classic/NexusKeyLite.exe.manifest` | Add `trustInfo` block |
| `src/tsf/NexusKeyTSF.rc` | Add `VS_VERSION_INFO` block |
| `CMakeLists.txt` | Add `/RELEASE`, `/SUBSYSTEM:WINDOWS,6.01` linker flags |
| `.github/workflows/build.yml` | Change Release→RelWithDebInfo, add cosign signing + attestation steps + permissions |
| `README.md` | Add "Verify Release" section |

## What This Does NOT Solve

- SmartScreen reputation is primarily download-count based — new hashes always start with low reputation
- Defender ML detection is probabilistic — metadata helps but keyboard hook behavior may still trigger
- Cosign/attestation does not replace Authenticode for Windows trust
- For complete solution, Authenticode code signing is needed (future consideration)

## Expected Impact

- **Defender quarantine**: Reduced — richer PE metadata + symbols = lower ML suspicion score
- **SmartScreen**: Marginal improvement from metadata; attestation badge builds user trust but reputation still needs downloads
- **Supply chain integrity**: High — cryptographic proof that binaries come from repo, verifiable by anyone
- **Cost**: Zero (code changes + free GitHub Actions features)
