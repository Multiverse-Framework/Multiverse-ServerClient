#pragma once
#include <atomic>
#include <csignal>
#include <mutex>

/**
 * @brief Global shutdown manager to control termination state across the app.
 */
class ShutdownManager {
public:
    static void install_signal_handlers() {
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
    }

    static void request_shutdown() {
        get_flag().store(true, std::memory_order_relaxed);
    }

    static bool is_shutdown() {
        return get_flag().load(std::memory_order_relaxed);
    }

    static void reset() {
        get_flag().store(false, std::memory_order_relaxed);
    }

private:
    static std::atomic<bool>& get_flag() {
        static std::atomic<bool> flag{false};
        return flag;
    }

    static void signal_handler(int signum) {
        (void)signum;
        request_shutdown();
    }
};
