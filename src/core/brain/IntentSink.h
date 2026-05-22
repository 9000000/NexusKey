// src/core/brain/IntentSink.h
//
// Sink interface that features write into. Production impl is OutputChannel
// (wraps IOutputInjector). Test impl is RecordingSink that buffers intents.
#pragma once

#include "core/brain/Intent.h"

namespace NextKey::Brain {

class IntentSink {
public:
    virtual ~IntentSink() = default;
    virtual void Emit(Intent intent) = 0;
};

}  // namespace NextKey::Brain
