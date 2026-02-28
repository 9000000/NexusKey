// NexusKey - TSF Profile Notification Sink Implementation
// SPDX-License-Identifier: GPL-3.0-only

#include "ProfileNotifySink.h"
#include "core/Debug.h"

namespace NextKey {

// NexusKey TextService CLSID — must match src/tsf/Globals.cpp
// {D84D1E5B-8F2C-4B1A-9D3E-6F7A8B9C0D1E}
static const GUID CLSID_NexusKeyTextService = {
    0xD84D1E5B, 0x8F2C, 0x4B1A,
    {0x9D, 0x3E, 0x6F, 0x7A, 0x8B, 0x9C, 0x0D, 0x1E}
};

ProfileNotifySink::ProfileNotifySink(ProfileChangeCallback callback)
    : callback_(std::move(callback)) {
}

ProfileNotifySink::~ProfileNotifySink() {
    Shutdown();
}

IFACEMETHODIMP ProfileNotifySink::QueryInterface(REFIID riid, void** ppvObj) {
    if (ppvObj == nullptr) return E_INVALIDARG;
    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfActiveLanguageProfileNotifySink)) {
        *ppvObj = static_cast<ITfActiveLanguageProfileNotifySink*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) ProfileNotifySink::AddRef() {
    return ++refCount_;
}

IFACEMETHODIMP_(ULONG) ProfileNotifySink::Release() {
    ULONG count = --refCount_;
    if (count == 0) {
        delete this;
    }
    return count;
}

IFACEMETHODIMP ProfileNotifySink::OnActivated(REFCLSID clsid, REFGUID /*guidProfile*/,
                                               BOOL fActivated) {
    if (IsEqualCLSID(clsid, CLSID_NexusKeyTextService)) {
        bool active = (fActivated != FALSE);
        NEXTKEY_LOG(L"ProfileNotifySink::OnActivated: NexusKey %s",
                    active ? L"ACTIVATED" : L"DEACTIVATED");
        if (callback_) {
            callback_(active);
        }
    }
    return S_OK;
}

bool ProfileNotifySink::Initialize() {
    // Create thread manager
    HRESULT hr = CoCreateInstance(
        CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfThreadMgr, reinterpret_cast<void**>(&pThreadMgr_));
    if (FAILED(hr) || !pThreadMgr_) {
        NEXTKEY_LOG(L"ProfileNotifySink: Failed to create ITfThreadMgr (hr=0x%08X)", hr);
        return false;
    }

    // Activate (required to receive notifications)
    hr = pThreadMgr_->Activate(&clientId_);
    if (FAILED(hr)) {
        NEXTKEY_LOG(L"ProfileNotifySink: ITfThreadMgr::Activate failed (hr=0x%08X)", hr);
        pThreadMgr_->Release();
        pThreadMgr_ = nullptr;
        return false;
    }

    // Get ITfSource for advising
    hr = pThreadMgr_->QueryInterface(
        IID_ITfSource, reinterpret_cast<void**>(&pSource_));
    if (FAILED(hr) || !pSource_) {
        NEXTKEY_LOG(L"ProfileNotifySink: QI for ITfSource failed (hr=0x%08X)", hr);
        pThreadMgr_->Deactivate();
        pThreadMgr_->Release();
        pThreadMgr_ = nullptr;
        return false;
    }

    // Advise our sink
    hr = pSource_->AdviseSink(
        IID_ITfActiveLanguageProfileNotifySink,
        static_cast<ITfActiveLanguageProfileNotifySink*>(this),
        &adviseCookie_);
    if (FAILED(hr)) {
        NEXTKEY_LOG(L"ProfileNotifySink: AdviseSink failed (hr=0x%08X)", hr);
        pSource_->Release();
        pSource_ = nullptr;
        pThreadMgr_->Deactivate();
        pThreadMgr_->Release();
        pThreadMgr_ = nullptr;
        return false;
    }

    NEXTKEY_LOG(L"ProfileNotifySink initialized (clientId=%u, cookie=%u)",
                clientId_, adviseCookie_);
    return true;
}

void ProfileNotifySink::SyncCurrentState() {
    // Use ITfInputProcessorProfileMgr (Vista+) to query the active keyboard TIP
    ITfInputProcessorProfileMgr* pProfileMgr = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
        IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void**>(&pProfileMgr));
    if (FAILED(hr) || !pProfileMgr) return;

    TF_INPUTPROCESSORPROFILE profile = {};
    hr = pProfileMgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile);
    if (SUCCEEDED(hr)) {
        bool isNexusKey = IsEqualCLSID(profile.clsid, CLSID_NexusKeyTextService);
        NEXTKEY_LOG(L"Initial profile: %s (langid=0x%04X)",
                    isNexusKey ? L"NexusKey" : L"Other", profile.langid);
        if (callback_) {
            callback_(isNexusKey);
        }
    }

    pProfileMgr->Release();
}

void ProfileNotifySink::Shutdown() {
    if (pSource_ && adviseCookie_ != 0) {
        pSource_->UnadviseSink(adviseCookie_);
        adviseCookie_ = 0;
    }
    if (pSource_) {
        pSource_->Release();
        pSource_ = nullptr;
    }
    if (pThreadMgr_) {
        pThreadMgr_->Deactivate();
        pThreadMgr_->Release();
        pThreadMgr_ = nullptr;
    }
    clientId_ = TF_CLIENTID_NULL;
}

}  // namespace NextKey
