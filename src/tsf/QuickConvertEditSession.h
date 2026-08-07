// VKey - TSF-native quick-convert edit session
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "EditSession.h"
#include "core/QuickConvertLogic.h"

#include <algorithm>
#include <array>
#include <string>

namespace NextKey::TSF {

/// State retained between presses for the optional sequential conversion mode.
/// The selected range is a TSF range, so it tracks host edits without relying on
/// HWND/EM_GETSEL or synthetic Shift+Left keystrokes.
struct TsfQuickConvertSequenceState {
    CComPtr<ITfContext> context;
    CComPtr<ITfRange> selectedRange;
    std::wstring originText;
    std::wstring lastResult;
    int currentIndex = 0;
    DWORD lastConvertTime = 0;

    void Reset() noexcept {
        context.Release();
        selectedRange.Release();
        originText.clear();
        lastResult.clear();
        currentIndex = 0;
        lastConvertTime = 0;
    }
};

/// Reads the current TSF selection and performs quick conversion. Document
/// replacement happens only inside this synchronous read/write edit session.
/// When autoPaste is off, Result() is returned to the caller for clipboard-only
/// output and the document is left untouched.
class QuickConvertEditSession final : public EditSession {
public:
    static constexpr std::size_t kMaxSelectionChars = 1024 * 1024;
    static constexpr DWORD kSequentialTimeoutMs = 5000;

    QuickConvertEditSession(ITfContext* context,
                            ConvertConfig config,
                            TsfQuickConvertSequenceState* sequenceState)
        : EditSession(context),
          config_(config),
          sequenceState_(sequenceState) {}

    IFACEMETHODIMP DoEditSession(TfEditCookie ec) override {
        if (pContext_ == nullptr) return E_INVALIDARG;

        TF_SELECTION selection{};
        ULONG fetched = 0;
        HRESULT hr = pContext_->GetSelection(
            ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
        if (FAILED(hr) || fetched != 1 || selection.range == nullptr) {
            if (selection.range != nullptr) selection.range->Release();
            return FAILED(hr) ? hr : E_FAIL;
        }

        CComPtr<ITfRange> selectedRange;
        selectedRange.Attach(selection.range);
        BOOL isEmpty = TRUE;
        if (FAILED(selectedRange->IsEmpty(ec, &isEmpty)) || isEmpty) {
            ResetSequence();
            return S_OK;
        }

        std::wstring selectedText;
        hr = ReadRangeText(ec, selectedRange, selectedText);
        if (FAILED(hr) || selectedText.empty()) {
            ResetSequence();
            return hr;
        }

        const auto options = GetEnabledQuickConvertOptions(config_);
        if (options.empty()) {
            ResetSequence();
            return S_OK;
        }

        if (config_.sequential && config_.autoPaste && sequenceState_ != nullptr) {
            const DWORD now = GetTickCount();
            const bool continuing = IsSameSequentialSelection(ec, selectedRange, selectedText)
                && now - sequenceState_->lastConvertTime <= kSequentialTimeoutMs;
            if (!continuing) {
                sequenceState_->originText = selectedText;
                sequenceState_->currentIndex = 0;
            } else {
                ++sequenceState_->currentIndex;
                if (sequenceState_->currentIndex >= static_cast<int>(options.size())) {
                    sequenceState_->currentIndex = -1;
                }
            }

            result_ = sequenceState_->currentIndex < 0
                ? sequenceState_->originText
                : ApplyQuickConvertOption(
                    sequenceState_->originText,
                    options[static_cast<std::size_t>(sequenceState_->currentIndex)],
                    config_);
            sequenceState_->lastConvertTime = now;
        } else {
            ResetSequence();
            result_ = ApplyQuickConvertConfig(selectedText, config_);
        }

        hasResult_ = result_ != selectedText;
        if (!hasResult_) {
            // A step that changes nothing (Upper on already-upper text) still
            // has to leave a cycle marker behind. Without it the next press
            // sees an empty selectedRange, reads that as a fresh selection,
            // resets currentIndex to 0 and locks the cycle on this option.
            if (config_.sequential && config_.autoPaste && sequenceState_ != nullptr) {
                sequenceState_->context = pContext_;
                sequenceState_->selectedRange = selectedRange;
                sequenceState_->lastResult = result_;
            }
            return S_OK;
        }
        if (!config_.autoPaste) return S_OK;

        // Preserve the original start with backward gravity. SetText may resize
        // or collapse the source range differently between hosts; rebuilding
        // the selection from this marker gives consistent re-selection.
        CComPtr<ITfRange> replacementRange;
        hr = selectedRange->Clone(&replacementRange);
        if (FAILED(hr) || !replacementRange) return FAILED(hr) ? hr : E_FAIL;
        hr = replacementRange->Collapse(ec, TF_ANCHOR_START);
        if (FAILED(hr)) return hr;
        (void)replacementRange->SetGravity(
            ec, TF_GRAVITY_BACKWARD, TF_GRAVITY_FORWARD);

        hr = selectedRange->SetText(
            ec, 0, result_.data(), static_cast<LONG>(result_.size()));
        if (FAILED(hr)) return hr;

        LONG shifted = 0;
        hr = replacementRange->ShiftEnd(
            ec, static_cast<LONG>(result_.size()), &shifted, nullptr);
        if (FAILED(hr) || shifted != static_cast<LONG>(result_.size())) {
            return FAILED(hr) ? hr : E_FAIL;
        }

        selection.range = replacementRange;
        selection.style.fInterimChar = FALSE;
        hr = pContext_->SetSelection(ec, 1, &selection);
        if (FAILED(hr)) return hr;

        if (config_.sequential && sequenceState_ != nullptr) {
            sequenceState_->context = pContext_;
            sequenceState_->selectedRange = replacementRange;
            sequenceState_->lastResult = result_;
        }
        replaced_ = true;
        return S_OK;
    }

    [[nodiscard]] bool HasResult() const noexcept { return hasResult_; }
    [[nodiscard]] bool Replaced() const noexcept { return replaced_; }
    [[nodiscard]] const std::wstring& Result() const noexcept { return result_; }

private:
    static HRESULT ReadRangeText(TfEditCookie ec,
                                 ITfRange* range,
                                 std::wstring& text) {
        CComPtr<ITfRange> scan;
        HRESULT hr = range->Clone(&scan);
        if (FAILED(hr) || !scan) return FAILED(hr) ? hr : E_FAIL;

        std::array<wchar_t, 4096> buffer{};
        while (text.size() < kMaxSelectionChars) {
            const ULONG remaining = static_cast<ULONG>(
                (std::min)(kMaxSelectionChars - text.size(), buffer.size()));
            ULONG retrieved = 0;
            hr = scan->GetText(
                ec, TF_TF_MOVESTART, buffer.data(), remaining, &retrieved);
            if (FAILED(hr)) return hr;
            if (retrieved == 0) break;
            text.append(buffer.data(), retrieved);
        }

        BOOL exhausted = TRUE;
        hr = scan->IsEmpty(ec, &exhausted);
        if (FAILED(hr)) return hr;
        return exhausted ? S_OK : HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    }

    [[nodiscard]] bool IsSameSequentialSelection(
        TfEditCookie ec,
        ITfRange* currentRange,
        const std::wstring& currentText) const noexcept {
        if (sequenceState_ == nullptr || sequenceState_->context != pContext_
            || !sequenceState_->selectedRange
            || sequenceState_->lastResult != currentText) {
            return false;
        }

        LONG startComparison = 1;
        LONG endComparison = 1;
        return SUCCEEDED(currentRange->CompareStart(
                   ec, sequenceState_->selectedRange, TF_ANCHOR_START,
                   &startComparison))
            && SUCCEEDED(currentRange->CompareEnd(
                   ec, sequenceState_->selectedRange, TF_ANCHOR_END,
                   &endComparison))
            && startComparison == 0 && endComparison == 0;
    }

    void ResetSequence() noexcept {
        if (sequenceState_ != nullptr) sequenceState_->Reset();
    }

    ConvertConfig config_;
    TsfQuickConvertSequenceState* sequenceState_ = nullptr;
    std::wstring result_;
    bool hasResult_ = false;
    bool replaced_ = false;
};

}  // namespace NextKey::TSF
