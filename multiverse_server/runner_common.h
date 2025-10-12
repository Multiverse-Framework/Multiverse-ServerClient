#pragma once
#include <atomic>
#include <chrono>
#include <thread>

inline void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Global shutdown flag that all runners can check
inline std::atomic<bool> g_should_shutdown{false};
