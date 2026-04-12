# AV False Positive Reduction — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce Windows Defender false positives and add supply chain attestation by hardening PE metadata and adding cosign + GitHub attestation to CI.

**Architecture:** Two independent parts: (1) PE metadata changes in manifest/RC/CMake files that improve the binary's legitimacy signals for AV ML scanners, (2) CI workflow changes adding cosign signing and GitHub attestation for release artifacts.

**Tech Stack:** Windows PE manifests (XML), RC resource files, CMake, GitHub Actions, sigstore/cosign

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `src/app/NexusKey.exe.manifest` | Modify | Add `trustInfo` block |
| `src/app/classic/NexusKeyLite.exe.manifest` | Modify | Add `trustInfo` block |
| `src/tsf/NexusKeyTSF.rc` | Modify | Add `VS_VERSION_INFO` block |
| `CMakeLists.txt` | Modify | Add `/RELEASE` and `/SUBSYSTEM:WINDOWS,6.01` linker flags |
| `.github/workflows/build.yml` | Modify | RelWithDebInfo, cosign signing, attestation, permissions |
| `README.md` | Modify | Add "Verify Release" section |

---

### Task 1: Add `trustInfo` to NexusKey manifest

**Files:**
- Modify: `src/app/NexusKey.exe.manifest`

- [ ] **Step 1: Add `trustInfo` block before closing `</assembly>` tag**

In `src/app/NexusKey.exe.manifest`, add the `trustInfo` block after the `<application>` block (before `</assembly>`):

```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32" name="NexusKey" version="2.1.9.0" processorArchitecture="*"/>
  <dependency>
    <dependentAssembly>
      <assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls"
        version="6.0.0.0" processorArchitecture="*" publicKeyToken="6595b64144ccf1df" language="*"/>
    </dependentAssembly>
  </dependency>
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
    </windowsSettings>
  </application>
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level="asInvoker" uiAccess="false"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
</assembly>
```

- [ ] **Step 2: Commit**

```bash
git add src/app/NexusKey.exe.manifest
git commit -m "fix: add trustInfo to NexusKey manifest for AV compatibility"
```

---

### Task 2: Add `trustInfo` to NexusKey Classic manifest

**Files:**
- Modify: `src/app/classic/NexusKeyLite.exe.manifest`

- [ ] **Step 1: Add `trustInfo` block before closing `</assembly>` tag**

In `src/app/classic/NexusKeyLite.exe.manifest`, add the `trustInfo` block after the `<application>` block (before `</assembly>`):

```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32" name="NexusKey.Classic" version="2.1.8.0" processorArchitecture="*"/>
  <dependency>
    <dependentAssembly>
      <assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls"
        version="6.0.0.0" processorArchitecture="*" publicKeyToken="6595b64144ccf1df" language="*"/>
    </dependentAssembly>
  </dependency>
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
    </windowsSettings>
  </application>
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security>
      <requestedPrivileges>
        <requestedExecutionLevel level="asInvoker" uiAccess="false"/>
      </requestedPrivileges>
    </security>
  </trustInfo>
</assembly>
```

- [ ] **Step 2: Commit**

```bash
git add src/app/classic/NexusKeyLite.exe.manifest
git commit -m "fix: add trustInfo to Classic manifest for AV compatibility"
```

---

### Task 3: Add VERSIONINFO to TSF DLL resource file

**Files:**
- Modify: `src/tsf/NexusKeyTSF.rc`

- [ ] **Step 1: Add `VS_VERSION_INFO` block to `NexusKeyTSF.rc`**

The current file only has icon entries. Add the version info block. The file should become:

```rc
// NexusKey TSF DLL - Resources
// SPDX-License-Identifier: GPL-3.0-only

#include "resource.h"
#include <windows.h>
#include "../core/Version.h"

IDI_VIET_ON  ICON "../app/resources/status/StatusViet.ico"
IDI_VIET_OFF ICON "../app/resources/status/StatusEng.ico"

// Version Information
VS_VERSION_INFO VERSIONINFO
    FILEVERSION    NEXUSKEY_VERSION_RC
    PRODUCTVERSION NEXUSKEY_VERSION_RC
    FILEFLAGSMASK  VS_FFI_FILEFLAGSMASK
#ifdef _DEBUG
    FILEFLAGS      VS_FF_DEBUG
#else
    FILEFLAGS      0x0L
#endif
    FILEOS         VOS_NT_WINDOWS32
    FILETYPE       VFT_DLL
    FILESUBTYPE    VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904B0"
        BEGIN
            VALUE "CompanyName",      "PhatMT97"
            VALUE "FileDescription",  "NexusKey TSF Input Processor"
            VALUE "FileVersion",      NEXUSKEY_VERSION_STR
            VALUE "InternalName",     "NextKeyTSF"
            VALUE "LegalCopyright",   "GPL-3.0-only"
            VALUE "OriginalFilename", "NextKeyTSF.dll"
            VALUE "ProductName",      "NexusKey"
            VALUE "ProductVersion",   NEXUSKEY_VERSION_STR
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 1200
    END
END
```

Key differences from EXE RC:
- `FILETYPE` is `VFT_DLL` (not `VFT_APP`)
- `FileDescription` is "NexusKey TSF Input Processor"
- `OriginalFilename` is "NextKeyTSF.dll"

- [ ] **Step 2: Commit**

```bash
git add src/tsf/NexusKeyTSF.rc
git commit -m "fix: add VERSIONINFO to TSF DLL for AV compatibility"
```

---

### Task 4: Add linker flags for PE legitimacy

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add `/RELEASE` and `/SUBSYSTEM:WINDOWS,6.01` linker flags**

In `CMakeLists.txt`, inside the existing `if(MSVC)` block (around line 15-20), add the linker flags after the existing `add_link_options` line:

Find:
```cmake
    # Linker security flags: ASLR, DEP, High-Entropy ASLR
    add_link_options(/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
```

Replace with:
```cmake
    # Linker security flags: ASLR, DEP, High-Entropy ASLR
    add_link_options(/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
    # PE legitimacy: checksum + explicit subsystem version (reduces AV false positives)
    add_link_options(/RELEASE /SUBSYSTEM:WINDOWS,6.01)
```

`/RELEASE` computes a valid PE checksum (legitimate software always has one).
`/SUBSYSTEM:WINDOWS,6.01` declares target Win7+ explicitly (default omission is suspicious to ML scanners).

- [ ] **Step 2: Verify CMake syntax by reconfiguring on Linux**

```bash
cd /home/phatmt/code/NexusKey
cmake -B build-linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug 2>&1 | head -20
```

Expected: no CMake errors (MSVC flags are inside `if(MSVC)` guard so Linux ignores them).

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "fix: add PE checksum and subsystem version linker flags"
```

---

### Task 5: Change CI build config to RelWithDebInfo

**Files:**
- Modify: `.github/workflows/build.yml`

- [ ] **Step 1: Replace `--config Release` with `--config RelWithDebInfo` for app/TSF/classic builds**

In `.github/workflows/build.yml`, change the build steps. Find and replace each occurrence:

Line 34 — Build NexusKey:
```yaml
      - name: Build NexusKey
        run: cmake --build build --config RelWithDebInfo --target NextKeyApp
```

Line 37 — Build TSF DLL:
```yaml
      - name: Build TSF DLL
        run: cmake --build build --config RelWithDebInfo --target NextKeyTSF
```

Line 49 — Build Classic UI:
```yaml
      - name: Build Classic UI
        run: cmake --build build-classic --config RelWithDebInfo --target NextKeyLite
```

Keep tests on `Release` (no debug symbols needed for tests):
```yaml
      - name: Build tests
        run: cmake --build build --config Release --target NextKeyTests

      - name: Run tests
        run: ctest --test-dir build --build-config Release --output-on-failure
```

- [ ] **Step 2: Update zip step to use RelWithDebInfo output directory**

The Release output goes to `build/Release/`. RelWithDebInfo goes to `build/RelWithDebInfo/`. Update the zip steps:

Find:
```yaml
      - name: Zip Release
        if: startsWith(github.ref, 'refs/tags/')
        shell: powershell
        run: |
          Get-ChildItem "build/Release/*" -Include NexusKey.exe,NextKeyTSF.dll,sciter.dll |
            Compress-Archive -DestinationPath NexusKey.zip
```

Replace with:
```yaml
      - name: Zip Release
        if: startsWith(github.ref, 'refs/tags/')
        shell: powershell
        run: |
          Get-ChildItem "build/RelWithDebInfo/*" -Include NexusKey.exe,NextKeyTSF.dll,sciter.dll |
            Compress-Archive -DestinationPath NexusKey.zip
```

Find:
```yaml
      - name: Zip Classic Release
        if: startsWith(github.ref, 'refs/tags/')
        shell: powershell
        run: |
          Compress-Archive -Path "build-classic/Release/NexusKeyClassic.exe" -DestinationPath NexusKeyClassic.zip
```

Replace with:
```yaml
      - name: Zip Classic Release
        if: startsWith(github.ref, 'refs/tags/')
        shell: powershell
        run: |
          Compress-Archive -Path "build-classic/RelWithDebInfo/NexusKeyClassic.exe" -DestinationPath NexusKeyClassic.zip
```

- [ ] **Step 3: Update upload-artifact paths**

Find the artifact upload path `build/Release/` and replace with `build/RelWithDebInfo/`:

```yaml
      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: NexusKey-x64
          path: |
            build/RelWithDebInfo/NexusKey.exe
            build/RelWithDebInfo/NextKeyTSF.dll
            build/RelWithDebInfo/sciter.dll
```

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/build.yml
git commit -m "build: switch to RelWithDebInfo for debug symbols in release binaries"
```

---

### Task 6: Add cosign signing to CI workflow

**Files:**
- Modify: `.github/workflows/build.yml`

- [ ] **Step 1: Add `id-token: write` and `attestations: write` to permissions**

Find:
```yaml
permissions:
  contents: write
```

Replace with:
```yaml
permissions:
  contents: write
  id-token: write       # OIDC token for cosign keyless signing
  attestations: write   # GitHub native attestation
```

- [ ] **Step 2: Add cosign install + signing steps after zip steps, before SHA-256 checksums**

Insert these steps after "Zip Classic Release" and before "Generate SHA-256 checksums":

```yaml
      # --- Cosign keyless signing (Sigstore) ---
      - name: Install Cosign
        if: startsWith(github.ref, 'refs/tags/')
        uses: sigstore/cosign-installer@v3

      - name: Sign NexusKey.zip
        if: startsWith(github.ref, 'refs/tags/')
        continue-on-error: true
        run: |
          cosign sign-blob NexusKey.zip --bundle NexusKey.zip.sigstore.json --yes

      - name: Sign NexusKeyClassic.zip
        if: startsWith(github.ref, 'refs/tags/')
        continue-on-error: true
        run: |
          cosign sign-blob NexusKeyClassic.zip --bundle NexusKeyClassic.zip.sigstore.json --yes
```

`continue-on-error: true` ensures Sigstore infra outage doesn't block releases.

- [ ] **Step 3: Add GitHub native attestation steps after cosign signing**

Insert after the cosign signing steps:

```yaml
      # --- GitHub build provenance attestation ---
      - name: Attest NexusKey
        if: startsWith(github.ref, 'refs/tags/')
        uses: actions/attest-build-provenance@v2
        with:
          subject-path: NexusKey.zip

      - name: Attest NexusKey Classic
        if: startsWith(github.ref, 'refs/tags/')
        uses: actions/attest-build-provenance@v2
        with:
          subject-path: NexusKeyClassic.zip
```

- [ ] **Step 4: Add `.sigstore.json` bundles to release assets**

Find the `files` list in the Release step:

```yaml
          files: |
            NexusKey.zip
            NexusKey.zip.sha256
            NexusKeyClassic.zip
            NexusKeyClassic.zip.sha256
```

Replace with:
```yaml
          files: |
            NexusKey.zip
            NexusKey.zip.sha256
            NexusKey.zip.sigstore.json
            NexusKeyClassic.zip
            NexusKeyClassic.zip.sha256
            NexusKeyClassic.zip.sigstore.json
```

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/build.yml
git commit -m "build: add cosign signing and GitHub attestation for releases"
```

---

### Task 7: Add "Verify Release" section to README

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add verification section**

Add a new section after the existing installation/download section. Find the line with `## Build từ mã nguồn` and insert before it:

```markdown
## Xác minh bản tải (Verify Release)

Mỗi bản phát hành đều được ký bằng [Sigstore](https://sigstore.dev) và đính kèm [build attestation](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations/using-artifact-attestations-to-establish-provenance-for-builds) từ GitHub Actions — chứng minh file được build từ mã nguồn trong repo này.

```bash
# Xác minh bằng GitHub CLI
gh attestation verify NexusKey.zip --repo PhatMT97/NexusKey

# Xác minh bằng cosign
cosign verify-blob NexusKey.zip \
  --bundle NexusKey.zip.sigstore.json \
  --certificate-oidc-issuer=https://token.actions.githubusercontent.com \
  --certificate-identity-regexp="https://github.com/phatMT97/NexusKey/"
```

> **Lưu ý:** Đây không phải code signing truyền thống (Authenticode). Windows SmartScreen vẫn có thể cảnh báo khi chạy lần đầu — đây là hành vi bình thường với phần mềm mã nguồn mở chưa có chứng chỉ ký số.

```

Note: Use `--certificate-identity-regexp` instead of exact `--certificate-identity` so it works across all tags without specifying a version.

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: add release verification instructions to README"
```
