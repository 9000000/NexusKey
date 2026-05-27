// src/core/pipeline/OutputChannel.cpp
#include "core/pipeline/OutputChannel.h"

#include <utility>

namespace NextKey::Pipeline {

void OutputChannel::Emit(Intent intent) {
    batch_.push_back(std::move(intent));
}

std::vector<Intent> OutputChannel::TakeBatch() {
    return std::exchange(batch_, std::vector<Intent>{});
}

}  // namespace NextKey::Pipeline
