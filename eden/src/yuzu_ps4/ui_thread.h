// SPDX-License-Identifier: GPL-3.0-or-later
// A thread with an explicit stack size (the PS4 default is small; Eden's file loaders and crypto
// need more) and an alternate signal stack, so a crash on it still reaches crash.log.
#pragma once

#include <functional>
#include <pthread.h>

extern "C" void ps4_crashlog_thread_init();

namespace OrbisFrontend {

class StackThread {
public:
    ~StackThread() { Join(); }
    bool Start(std::function<void()> function, size_t stack_size) {
        Join();
        body = std::move(function);
        pthread_attr_t attributes;
        pthread_attr_init(&attributes);
        pthread_attr_setstacksize(&attributes, stack_size);
        running = pthread_create(&thread, &attributes, &Entry, this) == 0;
        pthread_attr_destroy(&attributes);
        return running;
    }
    void Join() {
        if (!running) return;
        pthread_join(thread, nullptr);
        running = false;
    }
    bool Joinable() const { return running; }

private:
    static void* Entry(void* self) {
        ps4_crashlog_thread_init();
        static_cast<StackThread*>(self)->body();
        return nullptr;
    }
    std::function<void()> body;
    pthread_t thread{};
    bool running{};
};

} // namespace OrbisFrontend
