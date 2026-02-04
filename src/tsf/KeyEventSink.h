// NexusKey - Key Event Sink Header
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "stdafx.h"

namespace NextKey {
namespace TSF {

class TextService;
class EngineController;

/// Handles keyboard input events from TSF
class KeyEventSink : public ITfKeyEventSink {
public:
    KeyEventSink(TextService* pTextService, EngineController* pEngineController);
    virtual ~KeyEventSink();

    // Connect/disconnect from ITfKeystrokeMgr
    bool Advise(ITfThreadMgr* pThreadMgr);
    void Unadvise();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // ITfKeyEventSink
    IFACEMETHODIMP OnSetFocus(BOOL fForeground) override;
    IFACEMETHODIMP OnTestKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    IFACEMETHODIMP OnTestKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    IFACEMETHODIMP OnKeyDown(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    IFACEMETHODIMP OnKeyUp(ITfContext* pContext, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    IFACEMETHODIMP OnPreservedKey(ITfContext* pContext, REFGUID rguid, BOOL* pfEaten) override;

private:
    ULONG refCount_ = 1;
    TextService* pTextService_ = nullptr;
    EngineController* pEngineController_ = nullptr;
    ITfKeystrokeMgr* pKeystrokeMgr_ = nullptr;
};

}  // namespace TSF
}  // namespace NextKey
