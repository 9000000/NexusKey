// src/core/pipeline/OutputChannel.h
//
// Production IntentSink — accumulates feature-emitted intents per keystroke,
// then flushes as a single batch. Wave 1 exposes the batch via TakeBatch()
// for tests. Wave 2 will replace TakeBatch with FlushToInjector(IOutputInjector&)
// that issues `Replace(bsCount, text)` + `SendKey(reinjectVk)` in one go.
//
// Pattern B resolution: features never call SendInput; OutputChannel is the
// single serialization point.
#pragma once

#include <vector>
#include "core/pipeline/Intent.h"
#include "core/pipeline/IntentSink.h"

namespace NextKey::Pipeline {

class OutputChannel final : public IntentSink {
public:
    OutputChannel()  = default;
    ~OutputChannel() = default;

    OutputChannel(const OutputChannel&)            = delete;
    OutputChannel& operator=(const OutputChannel&) = delete;

    void Emit(Intent intent) override;

    // Consumes the current batch. After this call, the channel is empty.
    [[nodiscard]] std::vector<Intent> TakeBatch();

    [[nodiscard]] bool Empty() const noexcept { return batch_.empty(); }

private:
    std::vector<Intent> batch_;
};

}  // namespace NextKey::Pipeline
