#pragma once
#include <memory>
#include <string>
#include "runner_common.h"

enum class TransportSel { Zmq, Tcp, Udp };

struct IServerRunner {
    virtual ~IServerRunner() = default;
    virtual void run() = 0;             // blocking loop
    virtual const char* name() const = 0;
};

// Factory
std::unique_ptr<IServerRunner> make_runner(TransportSel sel,
                                           const std::string& bind_or_host,
                                           const std::string& port_opt);
