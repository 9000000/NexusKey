// NexusKey - TSF Profile Notification Sink
// Monitors active language profile changes to sync tray icon.
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <Windows.h>
#include <msctf.h>
#include <functional>

namespace NextKey {

/// Callback invoked when NexusKey's TIP profile is activated or deactivated.
using ProfileChangeCallback = std::function<void(bool isNexusKeyActive)>;

/// COM sink that monitors TSF active language profile changes.
/// The EXE hosts its own ITfThreadMgr and advises this sink to receive
/// notifications for all profile switches (hotkey, language bar, API).
class ProfileNotifySink : public ITfActiveLanguageProfileNotifySink {
public:
    explicit ProfileNotifySink(ProfileChangeCallback callback);
    ~ProfileNotifySink();

    // Non-copyable
    ProfileNotifySink(const ProfileNotifySink&) = delete;
    ProfileNotifySink& operator=(const ProfileNotifySink&) = delete;

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // ITfActiveLanguageProfileNotifySink
    IFACEMETHODIMP OnActivated(REFCLSID clsid, REFGUID guidProfile,
                               BOOL fActivated) override;

    /// Create ITfThreadMgr, activate, and advise sink.
    [[nodiscard]] bool Initialize();

    /// Query current active profile and fire callback with initial state.
    void SyncCurrentState();

    /// Unadvise and release all COM resources.
    void Shutdown();

private:
    ULONG refCount_ = 1;
    ProfileChangeCallback callback_;

    ITfThreadMgr* pThreadMgr_ = nullptr;
    ITfSource* pSource_ = nullptr;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD adviseCookie_ = 0;
};

}  // namespace NextKey
