// src/core/brain/OutputChannel.cpp
#include "core/brain/OutputChannel.h"

#include <utility>

namespace NextKey::Brain {

void OutputChannel::Emit(Intent intent) {
    batch_.push_back(std::move(intent));
}

std::vector<Intent> OutputChannel::TakeBatch() {
    return std::exchange(batch_, std::vector<Intent>{});
}

}  // namespace NextKey::Brain
