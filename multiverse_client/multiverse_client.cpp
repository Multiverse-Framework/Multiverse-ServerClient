#include <chrono>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include "utils/general.hpp"
#include "utils/socket_utils.hpp"
#include "utils/log_utils.hpp"
#if USE_ZMQ
#include "transport/zmq_client_transport.hpp"
#endif
#if USE_TCP
#include "transport/tcp_client_transport.hpp"
#endif
#if USE_UDP
#include "transport/udp_client_transport.hpp"
#endif
#include "multiverse_client.h"

enum class EMultiverseClientState : unsigned char {
    None,
    StartConnection,
    BindRequestMetaData,
    SendRequestMetaData,
    ReceiveResponseMetaData,
    BindResponseMetaData,
    InitSendAndReceiveData,
    BindSendData,
    SendData,
    ReceiveData,
    BindReceiveData
};

void MultiverseClient::set_transport(ClientTransportType t) {
    transport_type_ = t;
}

void MultiverseClient::ensure_transport_allocated() {
    if (transport_) return;
    switch (transport_type_) {
#if USE_ZMQ
    case ClientTransportType::Zmq:
        transport_ = new ZmqClientTransport();
        break;
#endif
#if USE_TCP
    case ClientTransportType::Tcp:
        transport_ = new TcpClientTransport();
        break;
#endif
#if USE_UDP
    case ClientTransportType::Udp:
        transport_ = new UdpClientTransport();
        break;
#endif
    default:
        throw std::runtime_error("Unknown transport type");
    }
}

void MultiverseClient::connect_to_server() {
    if (!transport_) { printf("[Client %s] Transport not initialized.\n", client_port.c_str()); return; }

    // Disconnect from previous socket_addr if any
    transport_->disconnect();

    if (ShutdownManager::is_shutdown()) return;

    auto current_flag = flag.load();
    if (current_flag == EMultiverseClientState::ReceiveData || current_flag == EMultiverseClientState::ReceiveResponseMetaData) {
        sleep_ms(1000);
    }

    const std::string server_socket_addr = host + ":" + server_port;

    // 1) Connect to the server "broker" port
    transport_->connect(server_socket_addr);

    // 2) Send our desired socket_addr as a text frame; expect echo
    printf("[Client %s] Sending request %s to %s.\n", client_port.c_str(), socket_addr.c_str(), server_socket_addr.c_str());
    transport_->send_text(socket_addr, /*more*/false);
    printf("[Client %s] Sent request %s to %s.\n", client_port.c_str(), socket_addr.c_str(), server_socket_addr.c_str());

    std::string receive_socket_addr;
    try {
        receive_socket_addr = transport_->recv_text();
        printf("[Client %s] Received response %s from %s.\n", client_port.c_str(), receive_socket_addr.c_str(), server_socket_addr.c_str());
    } catch (const std::exception& e) {
        ShutdownManager::request_shutdown();
        printf("[Client %s] %s, prepares to disconnect from server socket %s.", client_port.c_str(), e.what(), server_socket_addr.c_str());
    }

    // 3) Close broker connection
    transport_->disconnect();

    if (socket_addr.compare(receive_socket_addr) != 0) {
        flag = EMultiverseClientState::None;
    } else if (current_flag == EMultiverseClientState::None || current_flag == EMultiverseClientState::ReceiveData) {
        flag = EMultiverseClientState::StartConnection;
        printf("[Client %s] Opened the socket %s.\n", client_port.c_str(), socket_addr.c_str());
    } else if (current_flag == EMultiverseClientState::ReceiveResponseMetaData) {
        transport_->connect(socket_addr);
        flag = EMultiverseClientState::SendRequestMetaData;
    }
}

void MultiverseClient::connect(const std::string &in_host, const std::string &in_server_port, const std::string &in_client_port) {
    host = in_host;
    server_port = in_server_port;
    client_port = in_client_port;
    connect();
}

void MultiverseClient::start() {
    const auto current_flag = flag.load();
    if (current_flag == EMultiverseClientState::StartConnection) {
        printf("[Client %s] Start.\n", client_port.c_str());
        run();
    }
}

void MultiverseClient::connect() {
    flag = EMultiverseClientState::None;
    socket_addr = host + ":" + client_port;
    ShutdownManager::reset();
    clean_up();
    if (!init_objects()) return;

    ensure_transport_allocated();

    // Start/ensure broker handshake thread
    wait_for_connect_to_server_thread_finish();
    start_connect_to_server_thread();
}

double MultiverseClient::get_time_now() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() / 1e6;
}

void MultiverseClient::run() {
    while (!ShutdownManager::is_shutdown()) {
        auto current_flag = flag.load();
        // ---- State transition log ----
        const char* state_name = "UNKNOWN";
        switch (current_flag) {
        case EMultiverseClientState::StartConnection:        state_name = "StartConnection"; break;
        case EMultiverseClientState::BindRequestMetaData:    state_name = "BindRequestMetaData"; break;
        case EMultiverseClientState::SendRequestMetaData:    state_name = "SendRequestMetaData"; break;
        case EMultiverseClientState::ReceiveResponseMetaData:state_name = "ReceiveResponseMetaData"; break;
        case EMultiverseClientState::BindResponseMetaData:   state_name = "BindResponseMetaData"; break;
        case EMultiverseClientState::InitSendAndReceiveData: state_name = "InitSendAndReceiveData"; break;
        case EMultiverseClientState::BindSendData:           state_name = "BindSendData"; break;
        case EMultiverseClientState::SendData:               state_name = "SendData"; break;
        case EMultiverseClientState::ReceiveData:            state_name = "ReceiveData"; break;
        case EMultiverseClientState::BindReceiveData:        state_name = "BindReceiveData"; break;
        default:                                             state_name = "Unknown"; break;
        }

        mv_log("[Client %s] State -> %s (socket: %s)",
               client_port.c_str(), state_name, socket_addr.c_str());
        switch (current_flag) {
        case EMultiverseClientState::StartConnection:
            transport_->disconnect();
            transport_->connect(socket_addr);
            if (transport_type_ == ClientTransportType::Udp) {
                // UDP "connect" is a no-op, so we wait a bit to ensure the server is ready
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            flag = EMultiverseClientState::BindRequestMetaData;
            break;

        case EMultiverseClientState::BindRequestMetaData:
            bind_request_meta_data();
            wait_for_meta_data_thread_finish();
            start_meta_data_thread();
            return;

        case EMultiverseClientState::SendRequestMetaData:
            send_request_meta_data();
            flag = EMultiverseClientState::ReceiveResponseMetaData;
            break;

        case EMultiverseClientState::ReceiveResponseMetaData:
            receive_data();
            check_response_meta_data();
            break;

        case EMultiverseClientState::BindResponseMetaData:
            bind_response_meta_data();
            flag = EMultiverseClientState::InitSendAndReceiveData;
            return;

        case EMultiverseClientState::InitSendAndReceiveData:
            wait_for_connect_to_server_thread_finish();
            wait_for_meta_data_thread_finish();
            clean_up();
            init_send_and_receive_data();

            printf("[Client %s] Starting the communication (send: [%zu - %zu - %zu], receive: [%zu - %zu - %zu]).\n",
                   client_port.c_str(),
                   send_buffer.buffer_double.size,
                   send_buffer.buffer_uint8_t.size,
                   send_buffer.buffer_uint16_t.size,
                   receive_buffer.buffer_double.size,
                   receive_buffer.buffer_uint8_t.size,
                   receive_buffer.buffer_uint16_t.size);

            flag = EMultiverseClientState::BindSendData;
            break;

        case EMultiverseClientState::BindSendData:
            bind_send_data();
            flag = EMultiverseClientState::SendData;
            break;

        case EMultiverseClientState::SendData:
            send_send_data();
            flag = EMultiverseClientState::ReceiveData;
            break;

        case EMultiverseClientState::ReceiveData:
            receive_data();
            break;

        case EMultiverseClientState::BindReceiveData:
            bind_receive_data();
            flag = EMultiverseClientState::BindSendData;
            return;

        default:
            return;
        }
    }

    auto current_flag2 = flag.load();
    if (current_flag2 != EMultiverseClientState::ReceiveResponseMetaData &&
        current_flag2 != EMultiverseClientState::ReceiveData) {

        printf("[Client %s] Closing the socket %s.\n", client_port.c_str(), socket_addr.c_str());

        if (current_flag2 == EMultiverseClientState::BindRequestMetaData ||
            current_flag2 == EMultiverseClientState::SendRequestMetaData ||
            current_flag2 == EMultiverseClientState::BindResponseMetaData ||
            current_flag2 == EMultiverseClientState::InitSendAndReceiveData ||
            current_flag2 == EMultiverseClientState::BindSendData ||
            current_flag2 == EMultiverseClientState::SendData ||
            current_flag2 == EMultiverseClientState::BindReceiveData) {

            int message_int = 0;
            transport_->send(&message_int, sizeof(message_int), /*more*/false);

            free(send_buffer.buffer_double.data);   send_buffer.buffer_double.data = nullptr;
            free(send_buffer.buffer_uint8_t.data);  send_buffer.buffer_uint8_t.data = nullptr;
            free(send_buffer.buffer_uint16_t.data); send_buffer.buffer_uint16_t.data = nullptr;
            free(receive_buffer.buffer_double.data);   receive_buffer.buffer_double.data = nullptr;
            free(receive_buffer.buffer_uint8_t.data);  receive_buffer.buffer_uint8_t.data = nullptr;
            free(receive_buffer.buffer_uint16_t.data); receive_buffer.buffer_uint16_t.data = nullptr;
        }

        clean_up();
        transport_->disconnect();
    }
}

void MultiverseClient::send_and_receive_meta_data() {
    flag = EMultiverseClientState::SendRequestMetaData;
    run();
}

void MultiverseClient::send_request_meta_data() {
    if (ShutdownManager::is_shutdown()) {
        const int message_spec_int = 0;
        transport_->send(&message_spec_int, sizeof(int), false);
    } else {
        const int message_spec_int = 1;
        transport_->send(&message_spec_int, sizeof(int), true);
        transport_->send_text(request_meta_data_str, /*more*/false);
    }
}

void MultiverseClient::send_send_data() {
    const int message_spec_int = 2 + (send_buffer.buffer_double.size > 0) +
                                      (send_buffer.buffer_uint8_t.size > 0) +
                                      (send_buffer.buffer_uint16_t.size > 0);
    // header
    transport_->send(&message_spec_int, sizeof(int), true);

    // world_time
    const bool more_after_time = (message_spec_int != 2);
    transport_->send(world_time, sizeof(double), more_after_time);

    if (message_spec_int > 2) {
        auto send_more = [&](bool last) -> bool { return !last; };

        size_t remain = (send_buffer.buffer_double.size > 0) + (send_buffer.buffer_uint8_t.size > 0) + (send_buffer.buffer_uint16_t.size > 0);

        if (send_buffer.buffer_double.size > 0) {
            bool last = (--remain == 0);
            transport_->send(send_buffer.buffer_double.data, send_buffer.buffer_double.size * sizeof(double), send_more(last));
        }
        if (send_buffer.buffer_uint8_t.size > 0) {
            bool last = (--remain == 0);
            transport_->send(send_buffer.buffer_uint8_t.data, send_buffer.buffer_uint8_t.size * sizeof(uint8_t), send_more(last));
        }
        if (send_buffer.buffer_uint16_t.size > 0) {
            bool last = (--remain == 0);
            transport_->send(send_buffer.buffer_uint16_t.data, send_buffer.buffer_uint16_t.size * sizeof(uint16_t), send_more(last));
        }
    }
}

void MultiverseClient::receive_data() {
    int message_spec_int = 0;
    try {
        transport_->recv(&message_spec_int, sizeof(int));
    } catch (...) {
        ShutdownManager::request_shutdown();
        return;
    }

    if (message_spec_int == 0) {
        ShutdownManager::request_shutdown();
        return;
    } else if (message_spec_int == 1) {
        // meta-data string
        response_meta_data_str = transport_->recv_text();
        const auto current_flag = flag.load();
        if (current_flag == EMultiverseClientState::ReceiveResponseMetaData) {
            flag = EMultiverseClientState::BindResponseMetaData;
        } else if (current_flag == EMultiverseClientState::ReceiveData) {
            printf("[Client %s] The socket %s from the server has received new meta data.\n",
                   client_port.c_str(), socket_addr.c_str());
            check_response_meta_data();
            bind_api_callbacks();
            init_objects(true);
            bind_api_callbacks_response();
            flag = EMultiverseClientState::BindRequestMetaData;
        } else {
            throw std::runtime_error("[Client " + client_port + "] The client is in the wrong state.");
        }
        return;
    } else if (message_spec_int >= 2) {
        // world_time
        transport_->recv(world_time, sizeof(*world_time));

        if (message_spec_int == 3) {
            if (receive_buffer.buffer_double.size > 0 && receive_buffer.buffer_uint8_t.size == 0 && receive_buffer.buffer_uint16_t.size == 0) {
                transport_->recv(receive_buffer.buffer_double.data, receive_buffer.buffer_double.size * sizeof(double));
            } else if (receive_buffer.buffer_double.size == 0 && receive_buffer.buffer_uint8_t.size > 0 && receive_buffer.buffer_uint16_t.size == 0) {
                transport_->recv(receive_buffer.buffer_uint8_t.data, receive_buffer.buffer_uint8_t.size * sizeof(uint8_t));
            } else if (receive_buffer.buffer_double.size == 0 && receive_buffer.buffer_uint8_t.size == 0 && receive_buffer.buffer_uint16_t.size > 0) {
                transport_->recv(receive_buffer.buffer_uint16_t.data, receive_buffer.buffer_uint16_t.size * sizeof(uint16_t));
            } else {
                throw std::runtime_error("The receive buffer is not initialized correctly.");
            }
        } else if (message_spec_int == 4) {
            if (receive_buffer.buffer_uint16_t.size == 0) {
                transport_->recv(receive_buffer.buffer_double.data, receive_buffer.buffer_double.size * sizeof(double));
                transport_->recv(receive_buffer.buffer_uint8_t.data, receive_buffer.buffer_uint8_t.size * sizeof(uint8_t));
            } else if (receive_buffer.buffer_double.size == 0) {
                transport_->recv(receive_buffer.buffer_uint8_t.data, receive_buffer.buffer_uint8_t.size * sizeof(uint8_t));
                transport_->recv(receive_buffer.buffer_uint16_t.data, receive_buffer.buffer_uint16_t.size * sizeof(uint16_t));
            } else if (receive_buffer.buffer_uint8_t.size == 0) {
                transport_->recv(receive_buffer.buffer_double.data, receive_buffer.buffer_double.size * sizeof(double));
                transport_->recv(receive_buffer.buffer_uint16_t.data, receive_buffer.buffer_uint16_t.size * sizeof(uint16_t));
            } else {
                throw std::runtime_error("The receive buffer is not initialized correctly.");
            }
        } else if (message_spec_int == 5) {
            transport_->recv(receive_buffer.buffer_double.data, receive_buffer.buffer_double.size * sizeof(double));
            transport_->recv(receive_buffer.buffer_uint8_t.data, receive_buffer.buffer_uint8_t.size * sizeof(uint8_t));
            transport_->recv(receive_buffer.buffer_uint16_t.data, receive_buffer.buffer_uint16_t.size * sizeof(uint16_t));
        } else if (message_spec_int != 2) {
            throw std::runtime_error("The message type [" + std::to_string(message_spec_int) + "] is not recognized.");
        }
    } else {
        throw std::runtime_error("The message type [" + std::to_string(message_spec_int) + "] is not recognized.");
    }

    if (!ShutdownManager::is_shutdown() && *world_time == 0.0) {
        const double time_now = get_time_now();
        if (time_now - reset_time > reset_cool_down) {
            printf("[Client %s] The socket %s from the server has received reset command.\n",
                   client_port.c_str(), socket_addr.c_str());
            reset_time = time_now;
            reset();
        }
    }
    flag = EMultiverseClientState::BindReceiveData;
}

void MultiverseClient::check_response_meta_data() {
    if (ShutdownManager::is_shutdown()) {
        flag = EMultiverseClientState::BindResponseMetaData;
    } else if (compute_request_and_response_meta_data() && check_buffer_size()) {
        init_buffer();
        flag = EMultiverseClientState::BindResponseMetaData;
    } else {
        throw std::runtime_error("[Client " + client_port + "] The client failed to check the response meta data.");
    }
}

bool MultiverseClient::check_buffer_size() {
    std::map<std::string, std::map<std::string, size_t>> req =
        {{"send", {{"double",0},{"uint8",0},{"uint16",0}}}, {"receive", {{"double",0},{"uint8",0},{"uint16",0}}}};
    compute_request_buffer_sizes(req["send"], req["receive"]);

    std::map<std::string, std::map<std::string, size_t>> rsp =
        {{"send", {{"double",0},{"uint8",0},{"uint16",0}}}, {"receive", {{"double",0},{"uint8",0},{"uint16",0}}}};
    compute_response_buffer_sizes(rsp["send"], rsp["receive"]);

    if ((int)req["receive"]["double"] != -1 && (int)req["receive"]["uint8"] != -1 && (int)req["receive"]["uint16"] != -1 &&
        (req["send"]["double"]    != rsp["send"]["double"] ||
         req["send"]["uint8"]     != rsp["send"]["uint8"]  ||
         req["send"]["uint16"]    != rsp["send"]["uint16"] ||
         req["receive"]["double"] != rsp["receive"]["double"] ||
         req["receive"]["uint8"]  != rsp["receive"]["uint8"]  ||
         req["receive"]["uint16"] != rsp["receive"]["uint16"])) {

        printf("[Client %s] Failed to initialize the buffers %s: send_buffer_size(server = [%zu - %zu - %zu], client = [%zu - %zu - %zu]), "
               "receive_buffer_size(server = [%zu - %zu - %zu], client = [%zu - %zu - %zu]).\n",
               client_port.c_str(),
               socket_addr.c_str(),
               req["send"]["double"], req["send"]["uint8"], req["send"]["uint16"],
               rsp["send"]["double"], rsp["send"]["uint8"], rsp["send"]["uint16"],
               req["receive"]["double"], req["receive"]["uint8"], req["receive"]["uint16"],
               rsp["receive"]["double"], rsp["receive"]["uint8"], rsp["receive"]["uint16"]);
        return false;
    }

    send_buffer.buffer_double.size   = rsp["send"]["double"];
    send_buffer.buffer_uint8_t.size  = rsp["send"]["uint8"];
    send_buffer.buffer_uint16_t.size = rsp["send"]["uint16"];

    receive_buffer.buffer_double.size   = rsp["receive"]["double"];
    receive_buffer.buffer_uint8_t.size  = rsp["receive"]["uint8"];
    receive_buffer.buffer_uint16_t.size = rsp["receive"]["uint16"];
    return true;
}

void MultiverseClient::init_buffer() {
#define SAFE_CALLOC(count, type) \
    ((count) > 0 ? (type*)calloc((count), sizeof(type)) : nullptr)

    send_buffer.buffer_double.data   = SAFE_CALLOC(send_buffer.buffer_double.size, double);
    send_buffer.buffer_uint8_t.data  = SAFE_CALLOC(send_buffer.buffer_uint8_t.size, uint8_t);
    send_buffer.buffer_uint16_t.data = SAFE_CALLOC(send_buffer.buffer_uint16_t.size, uint16_t);
    receive_buffer.buffer_double.data   = SAFE_CALLOC(receive_buffer.buffer_double.size, double);
    receive_buffer.buffer_uint8_t.data  = SAFE_CALLOC(receive_buffer.buffer_uint8_t.size, uint8_t);
    receive_buffer.buffer_uint16_t.data = SAFE_CALLOC(receive_buffer.buffer_uint16_t.size, uint16_t);

    // Debug safety
    if (send_buffer.buffer_double.size == 0) {
        printf("[init_buffer] WARNING: send_buffer.buffer_double.size == 0 (no doubles allocated)\n");
    }
}

bool MultiverseClient::communicate(const bool resend_request_meta_data) {
    const auto current_flag = flag.load();
    if (ShutdownManager::is_shutdown() || current_flag == EMultiverseClientState::None) return false;

    if (current_flag == EMultiverseClientState::StartConnection) {
        run();
        return true;
    }

    if (resend_request_meta_data) {
        wait_for_meta_data_thread_finish();
        if (current_flag == EMultiverseClientState::BindSendData) {
            init_objects();
        }
        clean_up();
        flag = EMultiverseClientState::BindRequestMetaData;
        run();
        return true;
    }

    if (current_flag == EMultiverseClientState::BindSendData || current_flag == EMultiverseClientState::InitSendAndReceiveData) {
        run();
        return true;
    }
    return false;
}

void MultiverseClient::disconnect() {
    if (!transport_) { printf("[Client %s] The client transport is not initialized.\n", client_port.c_str()); return; }
    ShutdownManager::request_shutdown();

    run();

    wait_for_meta_data_thread_finish();
    wait_for_connect_to_server_thread_finish();

    delete transport_;
    transport_ = nullptr;
}
