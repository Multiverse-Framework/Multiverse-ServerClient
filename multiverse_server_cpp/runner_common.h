#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

inline void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}
