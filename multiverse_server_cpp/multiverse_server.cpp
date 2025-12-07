// Copyright (c) 2023, Giang Hoang Nguyen - Institute for Artificial Intelligence, University Bremen

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#define _USE_MATH_DEFINES
#include <set>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <thread>
#include <zmq_addon.hpp>
#include "transport/server_transport.hpp"
#include "transport/tcp_server_transport.hpp"
#include "transport/udp_server_transport.hpp"
#include "transport/zmq_server_transport.hpp"
#include <atomic>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#define CLOSESOCK(s) closesocket(s)
#define GET_LAST_ERR() WSAGetLastError()
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define CLOSESOCK(s) close(s)
#define GET_LAST_ERR() errno
#endif

#include "utils/general.hpp"
#include "utils/log_utils.hpp"
#include "multiverse_server.h"

#define STRING_SIZE 2000

using namespace std::chrono_literals;
std::map<std::string, bool> sockets_need_clean_up;
zmq::context_t server_context{1};

std::set<std::string> cumulative_attribute_names = {"force", "torque"};

std::map<std::string, std::pair<EAttribute, std::vector<double>>> attribute_map_double = {
    {"time", {EAttribute::Time, {0.0}}}, {"scalar", {EAttribute::Scalar, {std::numeric_limits<double>::quiet_NaN()}}},
    {"position", {EAttribute::Position, std::vector<double>(3, std::numeric_limits<double>::quiet_NaN())}},
    {"quaternion", {EAttribute::Quaternion, std::vector<double>(4, std::numeric_limits<double>::quiet_NaN())}},
    {"linear_velocity", {EAttribute::LinearVelocity, std::vector<double>(3, 0.0)}},
    {"angular_velocity", {EAttribute::AngularVelocity, std::vector<double>(3, 0.0)}},
    {"linear_acceleration", {EAttribute::LinearAcceleration, std::vector<double>(3, 0.0)}},
    {"angular_acceleration", {EAttribute::AngularAcceleration, std::vector<double>(3, 0.0)}},
    {"odometric_velocity", {EAttribute::OdometricVelocity, std::vector<double>(6, 0.0)}},
    {"joint_linear_position", {EAttribute::JointLinearPosition, {std::numeric_limits<double>::quiet_NaN()}}},
    {"joint_angular_position", {EAttribute::JointAngularPosition, {std::numeric_limits<double>::quiet_NaN()}}},
    {"joint_linear_velocity", {EAttribute::JointLinearVelocity, {std::numeric_limits<double>::quiet_NaN()}}},
    {"joint_angular_velocity", {EAttribute::JointAngularVelocity, {std::numeric_limits<double>::quiet_NaN()}}},
    {"joint_linear_acceleration", {EAttribute::JointLinearAcceleration, {std::numeric_limits<double>::quiet_NaN()}}},
    {"joint_angular_acceleration", {EAttribute::JointAngularAcceleration, {std::numeric_limits<double>::quiet_NaN()}}},
    {"joint_force", {EAttribute::JointForce, {std::numeric_limits<double>::quiet_NaN()}}},
    {"joint_torque", {EAttribute::JointTorque, {std::numeric_limits<double>::quiet_NaN()}}},
    {"cmd_joint_linear_position", {EAttribute::CmdJointLinearPosition, {std::numeric_limits<double>::quiet_NaN()}}},
    {"cmd_joint_angular_position", {EAttribute::CmdJointAngularPosition, {std::numeric_limits<double>::quiet_NaN()}}},
    {"cmd_joint_linear_velocity", {EAttribute::CmdJointLinearVelocity, {std::numeric_limits<double>::quiet_NaN()}}},
    {"cmd_joint_angular_velocity", {EAttribute::CmdJointAngularVelocity, {std::numeric_limits<double>::quiet_NaN()}}},
    {"cmd_joint_linear_acceleration",
        {EAttribute::CmdJointLinearAcceleration, {std::numeric_limits<double>::quiet_NaN()}}},
    {"cmd_joint_angular_acceleration",
        {EAttribute::CmdJointAngularAcceleration, {std::numeric_limits<double>::quiet_NaN()}}},
    {"cmd_joint_force", {EAttribute::CmdJointForce, {std::numeric_limits<double>::quiet_NaN()}}},
    {"cmd_joint_torque", {EAttribute::CmdJointTorque, {std::numeric_limits<double>::quiet_NaN()}}},
    {"joint_position", {EAttribute::JointPosition, std::vector<double>(3, std::numeric_limits<double>::quiet_NaN())}},
    {"joint_quaternion",
        {EAttribute::JointQuaternion, std::vector<double>(4, std::numeric_limits<double>::quiet_NaN())}},
    {"force", {EAttribute::Force, std::vector<double>(3, 0.0)}},
    {"torque", {EAttribute::Torque, std::vector<double>(3, 0.0)}}};

std::map<std::string, std::pair<EAttribute, std::vector<uint8_t>>> attribute_map_uint8_t = {
    {"rgb_3840_2160",
        {EAttribute::RGB_3840_2160, std::vector<uint8_t>(3840 * 2160 * 3, std::numeric_limits<uint8_t>::quiet_NaN())}},
    {"rgb_1280_1024",
        {EAttribute::RGB_1280_1024, std::vector<uint8_t>(1280 * 1024 * 3, std::numeric_limits<uint8_t>::quiet_NaN())}},
    {"rgb_640_480",
        {EAttribute::RGB_640_480, std::vector<uint8_t>(640 * 480 * 3, std::numeric_limits<uint8_t>::quiet_NaN())}},
    {"rgb_128_128",
        {EAttribute::RGB_128_128, std::vector<uint8_t>(128 * 128 * 3, std::numeric_limits<uint8_t>::quiet_NaN())}}};

std::map<std::string, std::pair<EAttribute, std::vector<uint16_t>>> attribute_map_uint16_t = {
    {"depth_3840_2160",
        {EAttribute::Depth_3840_2160, std::vector<uint16_t>(3840 * 2160, std::numeric_limits<uint16_t>::quiet_NaN())}},
    {"depth_1280_1024",
        {EAttribute::Depth_1280_1024, std::vector<uint16_t>(1280 * 1024, std::numeric_limits<uint16_t>::quiet_NaN())}},
    {"depth_640_480",
        {EAttribute::Depth_640_480, std::vector<uint16_t>(640 * 480, std::numeric_limits<uint16_t>::quiet_NaN())}},
    {"depth_128_128",
        {EAttribute::Depth_128_128, std::vector<uint16_t>(128 * 128, std::numeric_limits<uint16_t>::quiet_NaN())}}};

std::map<std::string, double> unit_scale = {{"s", 1.0}, {"ms", 0.001}, {"us", 0.00001}, {"m", 1.0}, {"cm", 0.01},
    {"rad", 1.0}, {"deg", M_PI / 180.0}, {"mg", 0.00001}, {"g", 0.001}, {"kg", 1.0}};

std::map<EAttribute, std::map<std::string, std::vector<double>>> handedness_scale = {
    {EAttribute::Time, {{"rhs", {1.0}}, {"lhs", {1.0}}}}, {EAttribute::Scalar, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::Position, {{"rhs", {1.0, 1.0, 1.0}}, {"lhs", {1.0, -1.0, 1.0}}}},
    {EAttribute::Quaternion, {{"rhs", {1.0, 1.0, 1.0, 1.0}}, {"lhs", {-1.0, 1.0, -1.0, 1.0}}}},
    {EAttribute::LinearVelocity, {{"rhs", {1.0, 1.0, 1.0}}, {"lhs", {1.0, 1.0, 1.0}}}},
    {EAttribute::AngularVelocity, {{"rhs", {1.0, 1.0, 1.0}}, {"lhs", {1.0, -1.0, 1.0}}}},
    {EAttribute::LinearAcceleration, {{"rhs", {1.0, 1.0, 1.0}}, {"lhs", {1.0, 1.0, 1.0}}}},
    {EAttribute::AngularAcceleration, {{"rhs", {1.0, 1.0, 1.0}}, {"lhs", {1.0, 1.0, 1.0}}}},
    {EAttribute::OdometricVelocity, {{"rhs", {1.0, 1.0, 1.0, 1.0, 1.0, 1.0}}, {"lhs", {1.0, 1.0, 1.0, 1.0, 1.0, 1.0}}}},
    {EAttribute::JointLinearPosition, {{"rhs", {1.0}}, {"lhs", {-1.0}}}},
    {EAttribute::JointAngularPosition, {{"rhs", {1.0}}, {"lhs", {-1.0}}}},
    {EAttribute::JointLinearVelocity, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::JointAngularVelocity, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::JointLinearAcceleration, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::JointAngularAcceleration, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::JointForce, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::JointTorque, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::CmdJointLinearPosition, {{"rhs", {1.0}}, {"lhs", {-1.0}}}},
    {EAttribute::CmdJointAngularPosition, {{"rhs", {1.0}}, {"lhs", {-1.0}}}},
    {EAttribute::CmdJointLinearVelocity, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::CmdJointAngularVelocity, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::CmdJointLinearAcceleration, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::CmdJointAngularAcceleration, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::CmdJointForce, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::CmdJointTorque, {{"rhs", {1.0}}, {"lhs", {1.0}}}},
    {EAttribute::JointPosition, {{"rhs", {1.0, 1.0, 1.0}}, {"lhs", {1.0, -1.0, 1.0}}}},
    {EAttribute::JointQuaternion, {{"rhs", {1.0, 1.0, 1.0, 1.0}}, {"lhs", {1.0, 1.0, -1.0, 1.0}}}},
    {EAttribute::Force, {{"rhs", {1.0, 1.0, 1.0}}, {"lhs", {1.0, -1.0, 1.0}}}},
    {EAttribute::Torque, {{"rhs", {1.0, 1.0, 1.0}}, {"lhs", {-1.0, 1.0, -1.0}}}}};

enum class EMetaDataState : unsigned char
{
    Normal,
    Reset,
    WaitAfterSendReceiveData,
    WaitAfterOtherBindSendData,
    WaitAfterOtherSendRequestMetaData,
    WaitAfterOtherNormal
};

std::mutex mtx;

template<class T>
struct TypedAttribute
{
    std::vector<T> data;
    std::map<std::string, std::vector<T>> simulation_data;
    bool is_sent = false;
};

struct Attribute
{
    TypedAttribute<double> attribute_double;
    TypedAttribute<uint8_t> attribute_uint8_t;
    TypedAttribute<uint16_t> attribute_uint16_t;
};

struct Object
{
    std::map<std::string, Attribute> attributes;
};

struct Simulation
{
    std::map<std::string, Object*> objects;
    Json::Value request_meta_data_json;
    EMetaDataState meta_data_state;
    std::vector<std::map<std::string, std::vector<std::string>>> api_callbacks;
    std::vector<std::map<std::string, std::vector<std::string>>> api_callbacks_response;
};

struct World
{
    std::map<std::string, Object> objects;
    std::map<std::string, Simulation> simulations;
    double time = 0.0;
};

std::map<std::string, World> worlds;

static double get_time_now()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
               .count() /
           1000000.0;
}

static Json::Value sort_json_array(const Json::Value& original)
{
    std::vector<std::string> vec;
    for (const auto& item : original)
    {
        vec.push_back(item.asString());
    }

    std::sort(vec.begin(), vec.end());

    Json::Value sorted;
    for (const auto& item : vec)
    {
        sorted.append(item);
    }

    return sorted;
}

static Json::Value sort_json_by_key(const Json::Value& original)
{
    std::vector<std::string> keys;
    for (const std::string& key : original.getMemberNames())
    {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end()); // Sort keys alphabetically

    Json::Value sorted(Json::objectValue);
    for (const std::string& key : keys)
    {
        sorted[key] = original[key].isObject()  ? sort_json_by_key(original[key])
                      : original[key].isArray() ? sort_json_array(original[key])
                                                : original[key];
    }
    return sorted;
}

static Json::Value sort_meta_data_json(const Json::Value& original)
{
    Json::Value sorted(original);
    sorted["send"] = sort_json_by_key(sorted["send"]);
    sorted["receive"] = sort_json_by_key(sorted["receive"]);
    return sorted;
}

MultiverseServer::MultiverseServer(const std::string& zmq_endpoint)
    : protocol_(ServerTransportType::Zmq)
{
#if USE_ZMQ
    socket_addr = zmq_endpoint;

    auto zmq_transport = std::make_unique<ZmqServerTransport>();
    try
    {
        zmq_transport->listen(socket_addr);
    }
    catch (const zmq::error_t& e)
    {
        throw std::runtime_error(std::string("[Server] ZMQ bind failed on ") + socket_addr + ": " + e.what());
    }
    transport_ = std::move(zmq_transport);
    sockets_need_clean_up[socket_addr] = false;
    instance_shutdown = false;
    std::printf("[Server] Bind to socket %s.\n", socket_addr.c_str());
#else
    throw std::runtime_error("[Server] ZMQ support is not enabled in this build.");
#endif
}

MultiverseServer::MultiverseServer(const std::string& host, const std::string& port, ServerTransportType t)
    : protocol_(t)
{
    tcp_host = host;
    tcp_port = port;

    if (t == ServerTransportType::Tcp)
    {
#if USE_TCP
        socket_addr = "rawtcp://" + host + ":" + port;
        printf("[Server] (TCP) Listen on %s:%s.\n", host.c_str(), port.c_str());

        auto tcp_transport = std::make_unique<TcpServerTransport>();
        tcp_transport->listen(host + ":" + port);
        if (!tcp_transport->accept())
        {
            throw std::runtime_error("[Server] Failed to accept raw TCP connection on " + host + ":" + port);
        }
        transport_ = std::move(tcp_transport);
        sockets_need_clean_up[socket_addr] = false;
        instance_shutdown = false;
#else
        throw std::runtime_error("[Server] Raw TCP support is not enabled in this build.");
#endif
    }
    else if (t == ServerTransportType::Udp)
    {
#if USE_UDP
        socket_addr = "rawudp://" + host + ":" + port;
        printf("[Server] (UDP) Bind on %s:%s.\n", host.c_str(), port.c_str());

        auto udp_transport = std::make_unique<UdpServerTransport>();
        udp_transport->listen(host + ":" + port);
        transport_ = std::move(udp_transport);
        sockets_need_clean_up[socket_addr] = false;
        instance_shutdown = false;
#else
        throw std::runtime_error("[Server] Raw UDP support is not enabled in this build.");
#endif
    }
    else
    {
        throw std::runtime_error("[Server] Unsupported TransportType for server");
    }
}

bool MultiverseServer::recv_message(int& message_spec_int, std::vector<std::vector<uint8_t>>& payloads)
{
    payloads.clear();

    std::vector<std::string> frames;
    sockets_need_clean_up[socket_addr] = false;
    if (!transport_->recv_multipart(frames))
    {
        mv_log("[SERVER] recv_multipart failed or peer closed");
        return false;
    }
    sockets_need_clean_up[socket_addr] = true;
    if (frames.empty())
    {
        mv_log("[SERVER] empty message");
        return false;
    }

    if (frames[0].size() != sizeof(int))
    {
        mv_log("[SERVER] bad spec frame size=%zu", frames[0].size());
        return false;
    }
    std::memcpy(&message_spec_int, frames[0].data(), sizeof(int));
    mv_log("[SERVER] spec=%d, total frames=%zu", message_spec_int, frames.size());

    payloads.clear();
    for (size_t i = 1; i < frames.size(); ++i)
    {
        payloads.emplace_back(frames[i].begin(), frames[i].end());
    }

    return true;
}

bool MultiverseServer::send_message(const void* data, size_t len, bool more)
{
    transport_->send(data, len, more);
    return true;
}

MultiverseServer::~MultiverseServer()
{
    printf("[Server] Close socket %s.\n", socket_addr.c_str());

    if (send_buffer.buffer_double.data != nullptr)
    {
        free(send_buffer.buffer_double.data);
    }
    if (send_buffer.buffer_uint8_t.data != nullptr)
    {
        free(send_buffer.buffer_uint8_t.data);
    }
    if (send_buffer.buffer_uint16_t.data != nullptr)
    {
        free(send_buffer.buffer_uint16_t.data);
    }
    if (receive_buffer.buffer_double.data != nullptr)
    {
        free(receive_buffer.buffer_double.data);
    }
    if (receive_buffer.buffer_uint8_t.data != nullptr)
    {
        free(receive_buffer.buffer_uint8_t.data);
    }
    if (receive_buffer.buffer_uint16_t.data != nullptr)
    {
        free(receive_buffer.buffer_uint16_t.data);
    }

    printf("[Server] Clean up socket %s.\n", socket_addr.c_str());
    worlds[request_world_name].simulations.erase(request_simulation_name);
    request_simulation_name.clear();
    simulation_name.clear();
    request_world_name.clear();
    world_name.clear();
    sockets_need_clean_up[socket_addr] = false;
}

void MultiverseServer::stop()
{
    instance_shutdown = true;
    transport_->disconnect();
}

void MultiverseServer::start()
{
    while (!instance_shutdown)
    {
        switch (flag)
        {
        case EMultiverseServerState::ReceiveRequestMetaData:
        {
            send_buffer.buffer_double.size = 0;
            send_buffer.buffer_uint8_t.size = 0;
            send_buffer.buffer_uint16_t.size = 0;
            receive_buffer.buffer_double.size = 0;
            receive_buffer.buffer_uint8_t.size = 0;
            receive_buffer.buffer_uint16_t.size = 0;

            is_receive_data_sent = false;

            flag = receive_data();
            break;
        }

        case EMultiverseServerState::BindObjects:
        {
            printf("[Server] Received meta data at socket %s:\n%s", socket_addr.c_str(),
                request_meta_data_json.toStyledString().c_str());
            bind_meta_data();

            mtx.lock();
            bind_send_objects();
            validate_meta_data();
            mtx.unlock();
            wait_for_objects();

            if (ShutdownManager::is_shutdown())
            {
                mv_log("[Server] Shutdown signal received, closing server on socket %s.", socket_addr.c_str());
                break;
            }

            mtx.lock();
            bind_receive_objects();
            mtx.unlock();

            if (request_meta_data_json.isMember("api_callbacks") && !request_meta_data_json["api_callbacks"].empty())
            {
                wait_for_api_callbacks_response();
            }

            flag = EMultiverseServerState::SendResponseMetaData;
        }

        case EMultiverseServerState::SendResponseMetaData:
        {
            send_response_meta_data();
            init_send_and_receive_data();
            // printf("[Server] Sent meta data to socket %s:\n%s", socket_addr.c_str(), response_meta_data_json.toStyledString().c_str());

            flag = EMultiverseServerState::ReceiveSendData;
            break;
        }

        case EMultiverseServerState::ReceiveSendData:
        {
            flag = receive_data();
            break;
        }

        case EMultiverseServerState::BindSendData:
        {
            if (worlds[world_name].time == 0.0)
            {
                printf("[Server] Reset all simulations in world %s.\n", world_name.c_str());
                for (std::pair<const std::string, Simulation>& simulation : worlds[world_name].simulations)
                {
                    printf("[Server] Reset simulation %s.\n", simulation.first.c_str());
                    simulation.second.meta_data_state = EMetaDataState::Reset;
                }
            }

            if ((strcmp(request_world_name.c_str(), world_name.c_str()) != 0 ||
                    strcmp(request_simulation_name.c_str(), simulation_name.c_str()) != 0) &&
                worlds[request_world_name].simulations.count(request_simulation_name) > 0)
            {
                wait_for_other_send_data();
            }

            mtx.lock();
            bind_send_data();
            mtx.unlock();

            flag = EMultiverseServerState::BindReceiveData;
            break;
        }

        case EMultiverseServerState::BindReceiveData:
        {
            wait_for_receive_data();

            mtx.lock();
            compute_cumulative_data();
            mtx.unlock();

            bind_receive_data();

            flag = EMultiverseServerState::SendReceiveData;
            break;
        }

        case EMultiverseServerState::SendReceiveData:
        {
            Simulation& simulation = worlds[world_name].simulations[simulation_name];
            if (simulation.meta_data_state == EMetaDataState::WaitAfterSendReceiveData)
            {
                receive_new_request_meta_data();

                flag = EMultiverseServerState::BindObjects;
            }
            else
            {
                if ((strcmp(request_world_name.c_str(), world_name.c_str()) != 0 ||
                        strcmp(request_simulation_name.c_str(), simulation_name.c_str()) != 0) &&
                    worlds[request_world_name].simulations.count(request_simulation_name) > 0)
                {
                    wait_for_other_send_data();
                }
                send_receive_data();

                flag = EMultiverseServerState::ReceiveSendData;

                simulation.meta_data_state = EMetaDataState::Normal;
            }

            break;
        }

        default:
            break;
        }
    }

    if (sockets_need_clean_up[socket_addr])
    {
        if (flag == EMultiverseServerState::BindSendData)
        {
            receive_data();
        }
        if (flag != EMultiverseServerState::ReceiveSendData && flag != EMultiverseServerState::ReceiveRequestMetaData)
        {
            try
            {
                send_receive_data();
            }
            catch (const zmq::error_t& e)
            {
                printf("[Server] %s, socket %s is terminated.\n", e.what(), socket_addr.c_str());
                return;
            }
        }

        printf("[Server] Unbind socket %s.\n", socket_addr.c_str());
        try
        {
            if (transport_->type() == ServerTransportType::Tcp)
            {
                printf("[Server] Disconnected the tcp socket client %s\n", socket_addr.c_str());
            }
            else
            {
                transport_->disconnect();
            }
        }
        catch (const zmq::error_t& e)
        {
            printf("[Server] %s, socket %s can not be unbinded.\n", e.what(), socket_addr.c_str());
        }
    }
}

EMultiverseServerState MultiverseServer::receive_data()
{
    try
    {
        int message_spec_int = -1;
        std::vector<std::vector<uint8_t>> payloads;
        if (!recv_message(message_spec_int, payloads))
        {
            instance_shutdown = true;
            sockets_need_clean_up[socket_addr] = true;
            return EMultiverseServerState::ReceiveRequestMetaData;
        }

        if (message_spec_int == 0 && payloads.size() == 0)
        {
            printf("[Server] Received close signal at socket %s.", socket_addr.c_str());
            send_response_meta_data();
            worlds[world_name].simulations[simulation_name].meta_data_state = EMetaDataState::Normal;
            return EMultiverseServerState::ReceiveRequestMetaData;
        }
        else if (message_spec_int == 1 && payloads.size() == 1)
        {
            std::string json_str(reinterpret_cast<const char*>(payloads[0].data()), payloads[0].size());
            if (reader.parse(json_str, request_meta_data_json) && !request_meta_data_json.empty())
            {
                request_meta_data_json = sort_meta_data_json(request_meta_data_json);
                send_buffer.buffer_double.data_vec.clear();
                send_buffer.buffer_uint8_t.data_vec.clear();
                send_buffer.buffer_uint16_t.data_vec.clear();
                receive_buffer.buffer_double.data_vec.clear();
                receive_buffer.buffer_uint8_t.data_vec.clear();
                receive_buffer.buffer_uint16_t.data_vec.clear();
                return EMultiverseServerState::BindObjects;
            }
            else
            {
                throw std::invalid_argument(
                    "[Server] Received invalid message [" + json_str + "] at socket " + socket_addr + ".");
            }
        }
        else if (message_spec_int >= 2)
        {
            if (payloads.empty() || payloads[0].size() != sizeof(double))
            {
                throw std::invalid_argument("[Server] Received invalid time payload at socket " + socket_addr + ".");
            }
            std::memcpy(&worlds[world_name].time, payloads[0].data(), sizeof(double));

            if (worlds[world_name].time < 0.0)
            {
                throw std::invalid_argument(
                    "[Server] Received invalid message [time = " + std::to_string(worlds[world_name].time) +
                    "] at socket " + socket_addr + ".");
            }

            size_t idx = 1;
            if (message_spec_int == 3 && payloads.size() == 2)
            {
                if (send_buffer.buffer_double.size > 0 && send_buffer.buffer_uint8_t.size == 0 &&
                    send_buffer.buffer_uint16_t.size == 0)
                {
                    std::memcpy(send_buffer.buffer_double.data, payloads[idx].data(),
                        send_buffer.buffer_double.size * sizeof(double));
                }
                else if (send_buffer.buffer_double.size == 0 && send_buffer.buffer_uint8_t.size > 0 &&
                         send_buffer.buffer_uint16_t.size == 0)
                {
                    std::memcpy(send_buffer.buffer_uint8_t.data, payloads[idx].data(),
                        send_buffer.buffer_uint8_t.size * sizeof(uint8_t));
                }
                else if (send_buffer.buffer_double.size == 0 && send_buffer.buffer_uint8_t.size == 0 &&
                         send_buffer.buffer_uint16_t.size > 0)
                {
                    std::memcpy(send_buffer.buffer_uint16_t.data, payloads[idx].data(),
                        send_buffer.buffer_uint16_t.size * sizeof(uint16_t));
                }
                else
                {
                    throw std::invalid_argument("[Server] Received invalid message [message_spec_int = " +
                                                std::to_string(message_spec_int) + "] at socket " + socket_addr + ".");
                }
            }
            else if (message_spec_int == 4 && payloads.size() == 3)
            {
                if (send_buffer.buffer_double.size > 0 && send_buffer.buffer_uint8_t.size > 0 &&
                    send_buffer.buffer_uint16_t.size == 0)
                {
                    std::memcpy(send_buffer.buffer_double.data, payloads[idx + 0].data(),
                        send_buffer.buffer_double.size * sizeof(double));
                    std::memcpy(send_buffer.buffer_uint8_t.data, payloads[idx + 1].data(),
                        send_buffer.buffer_uint8_t.size * sizeof(uint8_t));
                }
                else if (send_buffer.buffer_double.size > 0 && send_buffer.buffer_uint8_t.size == 0 &&
                         send_buffer.buffer_uint16_t.size > 0)
                {
                    std::memcpy(send_buffer.buffer_double.data, payloads[idx + 0].data(),
                        send_buffer.buffer_double.size * sizeof(double));
                    std::memcpy(send_buffer.buffer_uint16_t.data, payloads[idx + 1].data(),
                        send_buffer.buffer_uint16_t.size * sizeof(uint16_t));
                }
                else if (send_buffer.buffer_double.size == 0 && send_buffer.buffer_uint8_t.size > 0 &&
                         send_buffer.buffer_uint16_t.size > 0)
                {
                    std::memcpy(send_buffer.buffer_uint8_t.data, payloads[idx + 0].data(),
                        send_buffer.buffer_uint8_t.size * sizeof(uint8_t));
                    std::memcpy(send_buffer.buffer_uint16_t.data, payloads[idx + 1].data(),
                        send_buffer.buffer_uint16_t.size * sizeof(uint16_t));
                }
                else
                {
                    throw std::invalid_argument("[Server] Received invalid message [message_spec_int = " +
                                                std::to_string(message_spec_int) + "] at socket " + socket_addr + ".");
                }
            }
            else if (message_spec_int == 5 && payloads.size() == 4)
            {
                std::memcpy(send_buffer.buffer_double.data, payloads[idx + 0].data(),
                    send_buffer.buffer_double.size * sizeof(double));
                std::memcpy(send_buffer.buffer_uint8_t.data, payloads[idx + 1].data(),
                    send_buffer.buffer_uint8_t.size * sizeof(uint8_t));
                std::memcpy(send_buffer.buffer_uint16_t.data, payloads[idx + 2].data(),
                    send_buffer.buffer_uint16_t.size * sizeof(uint16_t));
            }
            return EMultiverseServerState::BindSendData;
        }
        else
        {
            throw std::invalid_argument("[Server] Received invalid message [message_spec_int = " +
                                        std::to_string(message_spec_int) + "] at socket " + socket_addr + ".");
        }
    }
    catch (const zmq::error_t& e)
    {
        ShutdownManager::request_shutdown();
        printf("[Server] %s, socket %s prepares to close.\n", e.what(), socket_addr.c_str());
        return EMultiverseServerState::ReceiveRequestMetaData;
    }
}

void MultiverseServer::bind_meta_data()
{
    if (!request_meta_data_json.isMember("meta_data") || request_meta_data_json["meta_data"].empty())
    {
        throw std::invalid_argument("[Server] Request meta data at socket " + socket_addr + " doesn't have meta data.");
    }

    Json::Value& meta_data = request_meta_data_json["meta_data"];
    if (!meta_data.isMember("world_name") || meta_data["world_name"].asString().empty())
    {
        throw std::invalid_argument(
            "[Server] Request meta data at socket " + socket_addr + " doesn't have a world name.");
    }
    request_world_name = meta_data["world_name"].asString();

    if (!meta_data.isMember("simulation_name") || meta_data["simulation_name"].asString().empty())
    {
        throw std::invalid_argument(
            "[Server] Request meta data at socket " + socket_addr + " doesn't have a simulation name.");
    }
    request_simulation_name = meta_data["simulation_name"].asString();
    if (simulation_name.empty() && worlds[request_world_name].simulations.count(request_simulation_name) > 0)
    {
        throw std::invalid_argument("[Server] Request meta data at socket " + socket_addr +
                                    " requires an existing simulation name (" + request_simulation_name + "). ");
    }

    if (!simulation_name.empty() && worlds[request_world_name].simulations.count(request_simulation_name) == 0)
    {
        printf("[Server] Socket %s requests a non-existing simulation (%s).\n", socket_addr.c_str(),
            request_simulation_name.c_str());
    }

    if (request_simulation_name != simulation_name && !simulation_name.empty() &&
        worlds[request_world_name].simulations.count(request_simulation_name) > 0)
    {
        if (request_meta_data_json.isMember("api_callbacks") && !request_meta_data_json["api_callbacks"].empty())
        {
            throw std::invalid_argument("[Server] Request meta data at socket " + socket_addr +
                                        " has API callbacks while requesting a different simulation.");
        }
        printf("[Server] Socket %s (%s) requests a different simulation (%s).\n", socket_addr.c_str(),
            simulation_name.c_str(), request_simulation_name.c_str());
        Simulation& request_simulation = worlds[request_world_name].simulations[request_simulation_name];

        double start = get_time_now();
        double now = start;
        while (!ShutdownManager::is_shutdown() && request_simulation.meta_data_state != EMetaDataState::Normal)
        {
            now = get_time_now();
            if (now - start > 1)
            {
                printf("[Server] Socket %s is waiting for %s to be in the normal state.\n", socket_addr.c_str(),
                    request_simulation_name.c_str());
                start = now;
            }
        }

        for (const char* const& type_str : {"send", "receive"})
        {
            for (const std::string& object_name : request_meta_data_json[type_str].getMemberNames())
            {
                if (object_name.empty())
                {
                    break;
                }

                Json::Value& attributes = request_simulation.request_meta_data_json[type_str][object_name];

                if (request_meta_data_json[type_str][object_name].empty())
                {
                    attributes = Json::Value(Json::arrayValue);
                    continue;
                }

                for (const Json::Value& attribute : request_meta_data_json[type_str][object_name])
                {
                    if (std::find(attributes.begin(), attributes.end(), attribute) == attributes.end())
                    {
                        attributes.append(attribute);
                    }
                }
            }
        }
        request_simulation.meta_data_state = EMetaDataState::WaitAfterSendReceiveData;
        world_name = request_world_name;
    }
    else
    {
        world_name = request_world_name;
        simulation_name = request_simulation_name;
    }

    if (request_meta_data_json.isMember("api_callbacks") && !request_meta_data_json["api_callbacks"].empty())
    {
        const Json::Value api_callbacks = request_meta_data_json["api_callbacks"];
        for (const std::string& called_simulation_name : api_callbacks.getMemberNames())
        {
            Simulation& simulation = worlds[world_name].simulations[called_simulation_name];
            simulation.meta_data_state = EMetaDataState::WaitAfterSendReceiveData;
        }
    }

    worlds[world_name].simulations[simulation_name].request_meta_data_json = request_meta_data_json;
    EMetaDataState& meta_data_state = worlds[world_name].simulations[simulation_name].meta_data_state;
    if (request_simulation_name == simulation_name &&
        meta_data_state == EMetaDataState::WaitAfterOtherSendRequestMetaData)
    {
        meta_data_state = EMetaDataState::WaitAfterOtherNormal;
    }
    else if (request_simulation_name != simulation_name || meta_data_state != EMetaDataState::WaitAfterOtherNormal)
    {
        meta_data_state = EMetaDataState::Normal;
    }

    const std::string length_unit = meta_data.isMember("length_unit") ? meta_data["length_unit"].asString() : "m";
    const std::string angle_unit = meta_data.isMember("angle_unit") ? meta_data["angle_unit"].asString() : "rad";
    const std::string handedness = meta_data.isMember("handedness") ? meta_data["handedness"].asString() : "rhs";
    const std::string mass_unit = meta_data.isMember("mass_unit") ? meta_data["mass_unit"].asString() : "kg";
    const std::string time_unit = meta_data.isMember("time_unit") ? meta_data["time_unit"].asString() : "s";

    std::map<EAttribute, std::vector<double>>& conversion_map_double = conversion_map.conversion_map_double;
    for (const std::pair<const std::string, std::pair<EAttribute, std::vector<double>>>& attribute :
        attribute_map_double)
    {
        conversion_map_double.emplace(attribute.second);
    }

    std::for_each(conversion_map_double[EAttribute::Time].begin(), conversion_map_double[EAttribute::Time].end(),
        [time_unit](double& time) { time = unit_scale[time_unit]; });

    std::for_each(conversion_map_double[EAttribute::Scalar].begin(), conversion_map_double[EAttribute::Scalar].end(),
        [](double& scalar) { scalar = 1.0; });

    std::for_each(conversion_map_double[EAttribute::Position].begin(),
        conversion_map_double[EAttribute::Position].end(),
        [length_unit](double& position) { position = unit_scale[length_unit]; });

    std::for_each(conversion_map_double[EAttribute::Quaternion].begin(),
        conversion_map_double[EAttribute::Quaternion].end(), [](double& quaternion) { quaternion = 1.0; });

    std::for_each(conversion_map_double[EAttribute::LinearVelocity].begin(),
        conversion_map_double[EAttribute::LinearVelocity].end(), [length_unit, time_unit](double& linear_velocity) {
            linear_velocity = unit_scale[length_unit] / unit_scale[time_unit];
        });

    std::for_each(conversion_map_double[EAttribute::AngularVelocity].begin(),
        conversion_map_double[EAttribute::AngularVelocity].end(), [angle_unit, time_unit](double& angular_velocity) {
            angular_velocity = unit_scale[angle_unit] / unit_scale[time_unit];
        });

    std::for_each(conversion_map_double[EAttribute::LinearAcceleration].begin(),
        conversion_map_double[EAttribute::LinearAcceleration].end(),
        [length_unit, time_unit](double& linear_acceleration) {
            linear_acceleration = unit_scale[length_unit] / (unit_scale[time_unit] * unit_scale[time_unit]);
        });

    std::for_each(conversion_map_double[EAttribute::AngularAcceleration].begin(),
        conversion_map_double[EAttribute::AngularAcceleration].end(),
        [angle_unit, time_unit](double& angular_acceleration) {
            angular_acceleration = unit_scale[angle_unit] / (unit_scale[time_unit] * unit_scale[time_unit]);
        });

    std::for_each(conversion_map_double[EAttribute::JointLinearPosition].begin(),
        conversion_map_double[EAttribute::JointLinearPosition].end(),
        [length_unit](double& joint_linear_position) { joint_linear_position = unit_scale[length_unit]; });

    std::for_each(conversion_map_double[EAttribute::JointAngularPosition].begin(),
        conversion_map_double[EAttribute::JointAngularPosition].end(),
        [angle_unit](double& joint_angular_position) { joint_angular_position = unit_scale[angle_unit]; });

    std::for_each(conversion_map_double[EAttribute::JointLinearVelocity].begin(),
        conversion_map_double[EAttribute::JointLinearVelocity].end(),
        [length_unit, time_unit](double& joint_linear_velocity) {
            joint_linear_velocity = unit_scale[length_unit] / unit_scale[time_unit];
        });

    std::for_each(conversion_map_double[EAttribute::JointAngularVelocity].begin(),
        conversion_map_double[EAttribute::JointAngularVelocity].end(),
        [angle_unit, time_unit](double& joint_angular_velocity) {
            joint_angular_velocity = unit_scale[angle_unit] / unit_scale[time_unit];
        });

    std::for_each(conversion_map_double[EAttribute::JointLinearAcceleration].begin(),
        conversion_map_double[EAttribute::JointLinearAcceleration].end(),
        [length_unit, time_unit](double& joint_linear_acceleration) {
            joint_linear_acceleration = unit_scale[length_unit] / (unit_scale[time_unit] * unit_scale[time_unit]);
        });

    std::for_each(conversion_map_double[EAttribute::JointAngularAcceleration].begin(),
        conversion_map_double[EAttribute::JointAngularAcceleration].end(),
        [angle_unit, time_unit](double& joint_angular_acceleration) {
            joint_angular_acceleration = unit_scale[angle_unit] / (unit_scale[time_unit] * unit_scale[time_unit]);
        });

    std::for_each(conversion_map_double[EAttribute::JointForce].begin(),
        conversion_map_double[EAttribute::JointForce].end(), [mass_unit, length_unit, time_unit](double& force) {
            force = unit_scale[mass_unit] * unit_scale[length_unit] / (unit_scale[time_unit] * unit_scale[time_unit]);
        });

    std::for_each(conversion_map_double[EAttribute::JointTorque].begin(),
        conversion_map_double[EAttribute::JointTorque].end(), [mass_unit, length_unit, time_unit](double& torque) {
            torque = unit_scale[mass_unit] * unit_scale[length_unit] * unit_scale[length_unit] /
                     (unit_scale[time_unit] * unit_scale[time_unit]);
        });

    std::for_each(conversion_map_double[EAttribute::JointPosition].begin(),
        conversion_map_double[EAttribute::JointPosition].end(),
        [length_unit](double& joint_position) { joint_position = unit_scale[length_unit]; });

    std::for_each(conversion_map_double[EAttribute::JointQuaternion].begin(),
        conversion_map_double[EAttribute::JointQuaternion].end(),
        [](double& joint_quaternion) { joint_quaternion = 1.0; });

    std::for_each(conversion_map_double[EAttribute::Force].begin(), conversion_map_double[EAttribute::Force].end(),
        [mass_unit, length_unit, time_unit](double& force) {
            force = unit_scale[mass_unit] * unit_scale[length_unit] / (unit_scale[time_unit] * unit_scale[time_unit]);
        });

    std::for_each(conversion_map_double[EAttribute::Torque].begin(), conversion_map_double[EAttribute::Torque].end(),
        [mass_unit, length_unit, time_unit](double& torque) {
            torque = unit_scale[mass_unit] * unit_scale[length_unit] * unit_scale[length_unit] /
                     (unit_scale[time_unit] * unit_scale[time_unit]);
        });

    conversion_map_double[EAttribute::CmdJointAngularPosition] =
        conversion_map_double[EAttribute::JointAngularPosition];

    conversion_map_double[EAttribute::CmdJointLinearPosition] = conversion_map_double[EAttribute::JointLinearPosition];

    conversion_map_double[EAttribute::CmdJointLinearVelocity] = conversion_map_double[EAttribute::JointLinearVelocity];

    conversion_map_double[EAttribute::CmdJointAngularVelocity] =
        conversion_map_double[EAttribute::JointAngularVelocity];

    conversion_map_double[EAttribute::CmdJointLinearAcceleration] =
        conversion_map_double[EAttribute::JointLinearAcceleration];

    conversion_map_double[EAttribute::CmdJointAngularAcceleration] =
        conversion_map_double[EAttribute::JointAngularAcceleration];

    conversion_map_double[EAttribute::CmdJointForce] = conversion_map_double[EAttribute::Force];

    conversion_map_double[EAttribute::CmdJointTorque] = conversion_map_double[EAttribute::Torque];

    for (size_t i = 0; i < 3; i++)
    {
        conversion_map_double[EAttribute::OdometricVelocity][i] = unit_scale[length_unit] / unit_scale[time_unit];
    }
    for (size_t i = 3; i < 6; i++)
    {
        conversion_map_double[EAttribute::OdometricVelocity][i] = unit_scale[angle_unit] / unit_scale[time_unit];
    }

    for (std::pair<const EAttribute, std::vector<double>>& conversion_scale : conversion_map_double)
    {
        std::vector<double>::iterator conversion_scale_it = conversion_scale.second.begin();
        std::vector<double>::iterator handedness_scale_it =
            handedness_scale[conversion_scale.first][handedness].begin();
        for (size_t i = 0; i < conversion_scale.second.size(); i++)
        {
            *(conversion_scale_it++) *= *(handedness_scale_it++);
        }
    }

    std::map<EAttribute, std::vector<uint8_t>>& conversion_map_uint8_t = conversion_map.conversion_map_uint8_t;
    for (const std::pair<const std::string, std::pair<EAttribute, std::vector<uint8_t>>>& attribute :
        attribute_map_uint8_t)
    {
        conversion_map_uint8_t.emplace(attribute.second);
    }

    std::map<EAttribute, std::vector<uint16_t>>& conversion_map_uint16_t = conversion_map.conversion_map_uint16_t;
    for (const std::pair<const std::string, std::pair<EAttribute, std::vector<uint16_t>>>& attribute :
        attribute_map_uint16_t)
    {
        conversion_map_uint16_t.emplace(attribute.second);
    }

    response_meta_data_json.clear();
    response_meta_data_json["meta_data"] = meta_data;
    response_meta_data_json["time"] = worlds[world_name].time * unit_scale[time_unit];
}

void MultiverseServer::bind_send_objects()
{
    send_objects_json = request_meta_data_json["send"];
    std::map<std::string, Object>& objects = worlds[world_name].objects;
    Simulation& simulation = worlds[world_name].simulations[simulation_name];

    for (const std::string& object_name : send_objects_json.getMemberNames())
    {
        response_meta_data_json["send"][object_name] = Json::objectValue;
        Object& object = objects[object_name];
        simulation.objects[object_name] = &object;
        for (const Json::Value& attribute_json : send_objects_json[object_name])
        {
            const std::string& attribute_name = attribute_json.asString();
            Attribute& attribute = object.attributes[attribute_name];
            if (cumulative_attribute_names.count(attribute_name) == 0)
            {
                if (attribute.attribute_double.data.size() == 0)
                {
                    attribute.attribute_double.data = attribute_map_double[attribute_name].second;
                    for (size_t i = 0; i < attribute.attribute_double.data.size(); i++)
                    {
                        double* data = &attribute.attribute_double.data[i];
                        const double conversion =
                            conversion_map.conversion_map_double[attribute_map_double[attribute_name].first][i];
                        send_buffer.buffer_double.data_vec.emplace_back(data, conversion);
                        response_meta_data_json["send"][object_name][attribute_name].append(*data * conversion);
                    }
                }
                else
                {
                    printf("[Server] Continue state [%s - %s] on socket %s\n", object_name.c_str(),
                        attribute_name.c_str(), socket_addr.c_str());
                    continue_state = true;
                    attribute.attribute_double.is_sent = true;

                    for (size_t i = 0; i < attribute.attribute_double.data.size(); i++)
                    {
                        double* data = &attribute.attribute_double.data[i];
                        const double conversion =
                            conversion_map.conversion_map_double[attribute_map_double[attribute_name].first][i];
                        send_buffer.buffer_double.data_vec.emplace_back(data, conversion);
                        response_meta_data_json["send"][object_name][attribute_name].append(*data * conversion);
                    }
                }
                if (attribute.attribute_uint8_t.data.size() == 0)
                {
                    attribute.attribute_uint8_t.data = attribute_map_uint8_t[attribute_name].second;
                    for (size_t i = 0; i < attribute.attribute_uint8_t.data.size(); i++)
                    {
                        uint8_t* data = &attribute.attribute_uint8_t.data[i];
                        const uint8_t conversion =
                            conversion_map.conversion_map_uint8_t[attribute_map_uint8_t[attribute_name].first][i];
                        send_buffer.buffer_uint8_t.data_vec.emplace_back(data, conversion);
                        response_meta_data_json["send"][object_name][attribute_name].append(*data >> conversion);
                    }
                }
                else
                {
                    printf("[Server] Continue state [%s - %s] on socket %s\n", object_name.c_str(),
                        attribute_name.c_str(), socket_addr.c_str());
                    continue_state = true;
                    attribute.attribute_uint8_t.is_sent = true;

                    for (size_t i = 0; i < attribute.attribute_uint8_t.data.size(); i++)
                    {
                        uint8_t* data = &attribute.attribute_uint8_t.data[i];
                        const uint8_t conversion =
                            conversion_map.conversion_map_uint8_t[attribute_map_uint8_t[attribute_name].first][i];
                        send_buffer.buffer_uint8_t.data_vec.emplace_back(data, conversion);
                        response_meta_data_json["send"][object_name][attribute_name].append(*data >> conversion);
                    }
                }
                if (attribute.attribute_uint16_t.data.size() == 0)
                {
                    attribute.attribute_uint16_t.data = attribute_map_uint16_t[attribute_name].second;
                    for (size_t i = 0; i < attribute.attribute_uint16_t.data.size(); i++)
                    {
                        uint16_t* data = &attribute.attribute_uint16_t.data[i];
                        const uint16_t conversion =
                            conversion_map.conversion_map_uint16_t[attribute_map_uint16_t[attribute_name].first][i];
                        send_buffer.buffer_uint16_t.data_vec.emplace_back(data, conversion);
                        response_meta_data_json["send"][object_name][attribute_name].append(*data >> conversion);
                    }
                }
                else
                {
                    printf("[Server] Continue state [%s - %s] on socket %s\n", object_name.c_str(),
                        attribute_name.c_str(), socket_addr.c_str());
                    continue_state = true;
                    attribute.attribute_uint16_t.is_sent = true;

                    for (size_t i = 0; i < attribute.attribute_uint16_t.data.size(); i++)
                    {
                        uint16_t* data = &attribute.attribute_uint16_t.data[i];
                        const uint16_t conversion =
                            conversion_map.conversion_map_uint16_t[attribute_map_uint16_t[attribute_name].first][i];
                        send_buffer.buffer_uint16_t.data_vec.emplace_back(data, conversion);
                        response_meta_data_json["send"][object_name][attribute_name].append(*data >> conversion);
                    }
                }
            }
            else
            {
                std::vector<double>& simulation_data_double =
                    attribute.attribute_double.simulation_data[simulation_name];
                if (simulation_data_double.size() == 0)
                {
                    simulation_data_double = attribute_map_double[attribute_name].second;
                }
                for (size_t i = 0; i < simulation_data_double.size(); i++)
                {
                    double* data = &simulation_data_double[i];
                    const double conversion =
                        conversion_map.conversion_map_double[attribute_map_double[attribute_name].first][i];
                    send_buffer.buffer_double.data_vec.emplace_back(data, conversion);
                    response_meta_data_json["send"][object_name][attribute_name].append(*data * conversion);
                }

                std::vector<uint8_t>& simulation_data_uint8_t =
                    attribute.attribute_uint8_t.simulation_data[simulation_name];
                if (simulation_data_uint8_t.size() == 0)
                {
                    simulation_data_uint8_t = attribute_map_uint8_t[attribute_name].second;
                }
                for (size_t i = 0; i < simulation_data_uint8_t.size(); i++)
                {
                    uint8_t* data = &simulation_data_uint8_t[i];
                    const uint8_t conversion =
                        conversion_map.conversion_map_uint8_t[attribute_map_uint8_t[attribute_name].first][i];
                    send_buffer.buffer_uint8_t.data_vec.emplace_back(data, conversion);
                    response_meta_data_json["send"][object_name][attribute_name].append(*data >> conversion);
                }

                std::vector<uint16_t>& simulation_data_uint16_t =
                    attribute.attribute_uint16_t.simulation_data[simulation_name];
                if (simulation_data_uint16_t.size() == 0)
                {
                    simulation_data_uint16_t = attribute_map_uint16_t[attribute_name].second;
                }
                for (size_t i = 0; i < simulation_data_uint16_t.size(); i++)
                {
                    uint16_t* data = &simulation_data_uint16_t[i];
                    const uint16_t conversion =
                        conversion_map.conversion_map_uint16_t[attribute_map_uint16_t[attribute_name].first][i];
                    send_buffer.buffer_uint16_t.data_vec.emplace_back(data, conversion);
                    response_meta_data_json["send"][object_name][attribute_name].append(*data >> conversion);
                }
            }
        }
    }
}

void MultiverseServer::validate_meta_data()
{
    receive_objects_json = request_meta_data_json["receive"];

    if (receive_objects_json.isMember("") && std::find(receive_objects_json[""].begin(), receive_objects_json[""].end(),
                                                 "") != receive_objects_json[""].end())
    {
        receive_objects_json = Json::objectValue;
        for (const std::pair<const std::string, Object>& object : worlds[world_name].objects)
        {
            for (const std::pair<const std::string, Attribute>& attribute_pair : object.second.attributes)
            {
                receive_objects_json[object.first].append(attribute_pair.first);
            }
        }
        return;
    }

    for (const std::string& object_name : request_meta_data_json["receive"].getMemberNames())
    {
        if (!object_name.empty())
        {
            for (const Json::Value& attribute_json : request_meta_data_json["receive"][object_name])
            {
                const std::string& attribute_name = attribute_json.asString();
                if (!attribute_name.empty())
                {
                    continue;
                }

                receive_objects_json[object_name] = Json::arrayValue;
                for (const std::pair<const std::string, Attribute>& attribute_pair :
                    worlds[world_name].objects[object_name].attributes)
                {
                    receive_objects_json[object_name].append(attribute_pair.first);
                }
                break;
            }
        }
        else
        {
            for (const Json::Value& attribute_json : request_meta_data_json["receive"][object_name])
            {
                const std::string& attribute_name = attribute_json.asString();
                for (const std::pair<const std::string, Object>& object_pair : worlds[world_name].objects)
                {
                    if (object_pair.second.attributes.count(attribute_name) > 0)
                    {
                        receive_objects_json[object_pair.first].append(attribute_name);
                    }
                }
            }
            receive_objects_json.removeMember(object_name);
            break;
        }
    }
}

void MultiverseServer::wait_for_objects()
{
    double start = get_time_now();
    double now = get_time_now();
    bool found_all_objects = true;
    do
    {
        found_all_objects = true;
        now = get_time_now();
        for (const std::string& object_name : receive_objects_json.getMemberNames())
        {
            for (const Json::Value& attribute_json : receive_objects_json[object_name])
            {
                const std::string& attribute_name = attribute_json.asString();
                if ((worlds[world_name].objects.count(object_name) == 0 ||
                        worlds[world_name].objects[object_name].attributes.count(attribute_name) == 0))
                {
                    found_all_objects = false;
                    if (now - start > 1)
                    {
                        printf("[Server] Socket %s is waiting for [%s][%s][%s] to be declared.\n", socket_addr.c_str(),
                            world_name.c_str(), object_name.c_str(), attribute_name.c_str());
                    }
                }
            }
        }
        if (now - start > 1)
        {
            start = now;
        }
    } while (!ShutdownManager::is_shutdown() && !found_all_objects);
}

void MultiverseServer::bind_receive_objects()
{
    std::map<std::string, Object>& objects = worlds[world_name].objects;
    Simulation& simulation = worlds[world_name].simulations[simulation_name];

    for (const std::string& object_name : receive_objects_json.getMemberNames())
    {
        Object& object = objects[object_name];
        simulation.objects[object_name] = &object;
        response_meta_data_json["receive"][object_name] = Json::objectValue;
        for (const Json::Value& attribute_json : receive_objects_json[object_name])
        {
            const std::string attribute_name = attribute_json.asString();
            Attribute& attribute = worlds[world_name].objects[object_name].attributes[attribute_name];
            if (cumulative_attribute_names.count(attribute_name) > 0)
            {
                if (attribute.attribute_double.data.size() == 0)
                {
                    attribute.attribute_double.data = attribute_map_double[attribute_name].second;
                    attribute.attribute_double.is_sent = true;
                }
                if (attribute.attribute_uint8_t.data.size() == 0)
                {
                    attribute.attribute_uint8_t.data = attribute_map_uint8_t[attribute_name].second;
                    attribute.attribute_uint8_t.is_sent = true;
                }
                if (attribute.attribute_uint16_t.data.size() == 0)
                {
                    attribute.attribute_uint16_t.data = attribute_map_uint16_t[attribute_name].second;
                    attribute.attribute_uint16_t.is_sent = true;
                }
            }

            for (size_t i = 0; i < attribute.attribute_double.data.size(); i++)
            {
                double* data = &attribute.attribute_double.data[i];
                const double conversion =
                    1.0 / conversion_map.conversion_map_double[attribute_map_double[attribute_name].first][i];
                receive_buffer.buffer_double.data_vec.emplace_back(data, conversion);
                response_meta_data_json["receive"][object_name][attribute_name].append(*data * conversion);
            }
            for (size_t i = 0; i < attribute.attribute_uint8_t.data.size(); i++)
            {
                uint8_t* data = &attribute.attribute_uint8_t.data[i];
                const uint8_t conversion =
                    conversion_map.conversion_map_uint8_t[attribute_map_uint8_t[attribute_name].first][i];
                receive_buffer.buffer_uint8_t.data_vec.emplace_back(data, conversion);
                response_meta_data_json["receive"][object_name][attribute_name].append(*data >> conversion);
            }
            for (size_t i = 0; i < attribute.attribute_uint16_t.data.size(); i++)
            {
                uint16_t* data = &attribute.attribute_uint16_t.data[i];
                const uint16_t conversion =
                    conversion_map.conversion_map_uint16_t[attribute_map_uint16_t[attribute_name].first][i];
                receive_buffer.buffer_uint16_t.data_vec.emplace_back(data, conversion);
                response_meta_data_json["receive"][object_name][attribute_name].append(*data >> conversion);
            }
        }
    }
}

void MultiverseServer::wait_for_api_callbacks_response()
{
    const Json::Value api_callbacks = request_meta_data_json["api_callbacks"];

    for (const std::string& called_simulation_name : api_callbacks.getMemberNames())
    {
        Simulation& simulation = worlds[world_name].simulations[called_simulation_name];
        simulation.request_meta_data_json["api_callbacks"] = api_callbacks[called_simulation_name];
        for (const Json::Value& api_callback : api_callbacks[called_simulation_name])
        {
            for (const std::string& callback_key : api_callback.getMemberNames())
            {
                std::map<std::string, std::vector<std::string>> api_callback_map;
                api_callback_map[callback_key] = std::vector<std::string>{};
                for (const Json::Value& param : api_callback[callback_key])
                {
                    api_callback_map[callback_key].push_back(param.asString());
                }
                simulation.api_callbacks.push_back(api_callback_map);
            }
        }
        simulation.meta_data_state = EMetaDataState::WaitAfterSendReceiveData;
    }

    double start = get_time_now();
    double now = get_time_now();
    bool stop = true;
    while (!ShutdownManager::is_shutdown())
    {
        now = get_time_now();
        stop = true;
        for (const std::string& called_simulation_name : api_callbacks.getMemberNames())
        {
            Simulation& simulation = worlds[world_name].simulations[called_simulation_name];
            if (simulation.meta_data_state != EMetaDataState::Normal)
            {
                stop = false;
                if (now - start > 1)
                {
                    if (simulation.api_callbacks.size() != 0)
                    {
                        printf("[Server] Socket %s is waiting for %s to send API callbacks response data.\n",
                            socket_addr.c_str(), called_simulation_name.c_str());
                    }
                    else
                    {
                        printf("[Server] Socket %s is waiting for %s to send data.\n", socket_addr.c_str(),
                            called_simulation_name.c_str());
                    }
                }
            }
        }
        if (now - start > 1)
        {
            start = now;
        }
        if (stop)
        {
            break;
        }
    }

    for (const std::string& called_simulation_name : api_callbacks.getMemberNames())
    {
        Simulation& simulation = worlds[world_name].simulations[called_simulation_name];
        response_meta_data_json["api_callbacks_response"][called_simulation_name] = Json::arrayValue;
        for (const std::map<std::string, std::vector<std::string>>& api_callbacks_response :
            simulation.api_callbacks_response)
        {
            for (const std::pair<const std::string, std::vector<std::string>>& api_callback_response :
                api_callbacks_response)
            {
                Json::Value api_callback_response_json;
                api_callback_response_json[api_callback_response.first] = Json::arrayValue;
                for (const std::string& param : api_callback_response.second)
                {
                    api_callback_response_json[api_callback_response.first].append(param);
                }
                response_meta_data_json["api_callbacks_response"][called_simulation_name].append(
                    api_callback_response_json);
            }
        }
    }
}

void MultiverseServer::send_response_meta_data()
{
    if (continue_state)
    {
        continue_state = false;
    }

    if (ShutdownManager::is_shutdown())
    {
        const int message_int = 0;
        send_message(&message_int, sizeof(message_int), /*more*/ false);
    }
    else
    {
        const int message_int = 1;
        send_message(&message_int, sizeof(message_int), /*more*/ true);
        const std::string message_str = response_meta_data_json.toStyledString();
        // printf("message str %s\n", message_str.c_str());
        send_message(message_str.c_str(), strlen(message_str.c_str()), false);
    }
}

void MultiverseServer::init_send_and_receive_data()
{
    send_buffer.buffer_double.size = send_buffer.buffer_double.data_vec.size();
    send_buffer.buffer_double.data = (double*)calloc(send_buffer.buffer_double.size, sizeof(double));
    send_buffer.buffer_uint8_t.size = send_buffer.buffer_uint8_t.data_vec.size();
    send_buffer.buffer_uint8_t.data = (uint8_t*)calloc(send_buffer.buffer_uint8_t.size, sizeof(uint8_t));
    send_buffer.buffer_uint16_t.size = send_buffer.buffer_uint16_t.data_vec.size();
    send_buffer.buffer_uint16_t.data = (uint16_t*)calloc(send_buffer.buffer_uint16_t.size, sizeof(uint16_t));
    receive_buffer.buffer_double.size = receive_buffer.buffer_double.data_vec.size();
    receive_buffer.buffer_double.data = (double*)calloc(receive_buffer.buffer_double.size, sizeof(double));
    receive_buffer.buffer_uint8_t.size = receive_buffer.buffer_uint8_t.data_vec.size();
    receive_buffer.buffer_uint8_t.data = (uint8_t*)calloc(receive_buffer.buffer_uint8_t.size, sizeof(uint8_t));
    receive_buffer.buffer_uint16_t.size = receive_buffer.buffer_uint16_t.data_vec.size();
    receive_buffer.buffer_uint16_t.data = (uint16_t*)calloc(receive_buffer.buffer_uint16_t.size, sizeof(uint16_t));
}

void MultiverseServer::wait_for_other_send_data()
{
    double start = get_time_now();
    double now = get_time_now();
    EMetaDataState& request_meta_data_state =
        worlds[request_world_name].simulations[request_simulation_name].meta_data_state;
    while (!ShutdownManager::is_shutdown())
    {
        if (request_meta_data_state == EMetaDataState::WaitAfterOtherBindSendData ||
            request_meta_data_state == EMetaDataState::Normal)
        {
            break;
        }
        now = get_time_now();
        if (now - start > 1)
        {
            printf("[Server] Socket %s is waiting for %s to send data.\n", socket_addr.c_str(),
                request_simulation_name.c_str());
            start = now;
        }
    }

    request_meta_data_state = EMetaDataState::WaitAfterOtherSendRequestMetaData;
}

void MultiverseServer::bind_send_data()
{
    for (size_t i = 0; i < send_buffer.buffer_double.size; i++)
    {
        *send_buffer.buffer_double.data_vec[i].first =
            send_buffer.buffer_double.data[i] * send_buffer.buffer_double.data_vec[i].second;
    }
    for (size_t i = 0; i < send_buffer.buffer_uint8_t.size; i++)
    {
        *send_buffer.buffer_uint8_t.data_vec[i].first =
            send_buffer.buffer_uint8_t.data[i] >> send_buffer.buffer_uint8_t.data_vec[i].second;
    }
    for (size_t i = 0; i < send_buffer.buffer_uint16_t.size; i++)
    {
        *send_buffer.buffer_uint16_t.data_vec[i].first =
            send_buffer.buffer_uint16_t.data[i] >> send_buffer.buffer_uint16_t.data_vec[i].second;
    }
}

void MultiverseServer::wait_for_receive_data()
{
    for (const std::string& object_name : send_objects_json.getMemberNames())
    {
        for (const Json::Value& attribute_json : send_objects_json[object_name])
        {
            const std::string attribute_name = attribute_json.asString();
            worlds[world_name].objects[object_name].attributes[attribute_name].attribute_double.is_sent = true;
            worlds[world_name].objects[object_name].attributes[attribute_name].attribute_uint8_t.is_sent = true;
            worlds[world_name].objects[object_name].attributes[attribute_name].attribute_uint16_t.is_sent = true;
        }
    }

    if (!is_receive_data_sent)
    {
        for (const std::string& object_name : receive_objects_json.getMemberNames())
        {
            for (const Json::Value& attribute_json : receive_objects_json[object_name])
            {
                const std::string attribute_name = attribute_json.asString();
                double start = get_time_now();
                while (
                    (worlds[world_name].objects.count(object_name) == 0 ||
                        worlds[world_name].objects[object_name].attributes.count(attribute_name) == 0 ||
                        !worlds[world_name].objects[object_name].attributes[attribute_name].attribute_double.is_sent ||
                        !worlds[world_name].objects[object_name].attributes[attribute_name].attribute_uint8_t.is_sent ||
                        !worlds[world_name]
                            .objects[object_name]
                            .attributes[attribute_name]
                            .attribute_uint16_t.is_sent) &&
                    !ShutdownManager::is_shutdown())
                {
                    const double now = get_time_now();
                    if (now - start > 1)
                    {
                        printf("[Server] Socket %s is waiting for data of [%s][%s][%s] to be sent.\n",
                            socket_addr.c_str(), world_name.c_str(), object_name.c_str(), attribute_name.c_str());
                        start = now;
                    }
                }
            }
        }

        is_receive_data_sent = true;
    }
}

void MultiverseServer::compute_cumulative_data()
{
    for (const std::string& object_name : receive_objects_json.getMemberNames())
    {
        for (const std::string& attribute_name : cumulative_attribute_names)
        {
            if (std::find(receive_objects_json[object_name].begin(), receive_objects_json[object_name].end(),
                    attribute_name) == receive_objects_json[object_name].end())
            {
                continue;
            }

            std::vector<double>& double_data =
                worlds[world_name].objects[object_name].attributes[attribute_name].attribute_double.data;
            double_data = attribute_map_double[attribute_name].second;
            for (size_t i = 0; i < double_data.size(); i++)
            {
                for (std::pair<const std::string, Simulation>& simulation_pair : worlds[world_name].simulations)
                {
                    if (simulation_pair.second.objects.count(object_name) == 0)
                    {
                        continue;
                    }

                    const std::string& simulation_name = simulation_pair.first;
                    const std::vector<double>& simulation_data = (*simulation_pair.second.objects[object_name])
                                                                     .attributes[attribute_name]
                                                                     .attribute_double.simulation_data[simulation_name];
                    if (simulation_data.size() != double_data.size())
                    {
                        continue;
                    }

                    double_data[i] += simulation_data[i];
                }
            }

            std::vector<uint8_t>& uint8_t_data =
                worlds[world_name].objects[object_name].attributes[attribute_name].attribute_uint8_t.data;
            uint8_t_data = attribute_map_uint8_t[attribute_name].second;
            for (size_t i = 0; i < uint8_t_data.size(); i++)
            {
                for (std::pair<const std::string, Simulation>& simulation_pair : worlds[world_name].simulations)
                {
                    if (simulation_pair.second.objects.count(object_name) == 0)
                    {
                        continue;
                    }

                    const std::string& simulation_name = simulation_pair.first;
                    const std::vector<uint8_t>& simulation_data =
                        (*simulation_pair.second.objects[object_name])
                            .attributes[attribute_name]
                            .attribute_uint8_t.simulation_data[simulation_name];
                    if (simulation_data.size() != uint8_t_data.size())
                    {
                        continue;
                    }

                    uint8_t_data[i] += simulation_data[i];
                }
            }

            std::vector<uint16_t>& uint16_t_data =
                worlds[world_name].objects[object_name].attributes[attribute_name].attribute_uint16_t.data;
            uint16_t_data = attribute_map_uint16_t[attribute_name].second;
            for (size_t i = 0; i < uint16_t_data.size(); i++)
            {
                for (std::pair<const std::string, Simulation>& simulation_pair : worlds[world_name].simulations)
                {
                    if (simulation_pair.second.objects.count(object_name) == 0)
                    {
                        continue;
                    }

                    const std::string& simulation_name = simulation_pair.first;
                    const std::vector<uint16_t>& simulation_data =
                        (*simulation_pair.second.objects[object_name])
                            .attributes[attribute_name]
                            .attribute_uint16_t.simulation_data[simulation_name];
                    if (simulation_data.size() != uint16_t_data.size())
                    {
                        continue;
                    }

                    uint16_t_data[i] += simulation_data[i];
                }
            }
        }
    }
}

void MultiverseServer::bind_receive_data()
{
    for (size_t i = 0; i < receive_buffer.buffer_double.size; i++)
    {
        receive_buffer.buffer_double.data[i] =
            *receive_buffer.buffer_double.data_vec[i].first * receive_buffer.buffer_double.data_vec[i].second;
    }
    for (size_t i = 0; i < receive_buffer.buffer_uint8_t.size; i++)
    {
        receive_buffer.buffer_uint8_t.data[i] =
            *receive_buffer.buffer_uint8_t.data_vec[i].first >> receive_buffer.buffer_uint8_t.data_vec[i].second;
    }
    for (size_t i = 0; i < receive_buffer.buffer_uint16_t.size; i++)
    {
        receive_buffer.buffer_uint16_t.data[i] =
            *receive_buffer.buffer_uint16_t.data_vec[i].first >> receive_buffer.buffer_uint16_t.data_vec[i].second;
    }
}

void MultiverseServer::receive_new_request_meta_data()
{
    printf("[Server] Socket %s has received new request meta data.\n", socket_addr.c_str());

    Simulation& simulation = worlds[world_name].simulations[simulation_name];
    simulation.meta_data_state = EMetaDataState::WaitAfterOtherBindSendData;
    double start = get_time_now();
    double now = get_time_now();
    while (!ShutdownManager::is_shutdown())
    {
        if (simulation.meta_data_state == EMetaDataState::WaitAfterOtherSendRequestMetaData)
        {
            break;
        }

        if (simulation.api_callbacks.size() != 0)
        {
            response_meta_data_json["api_callbacks"] = simulation.request_meta_data_json["api_callbacks"];
            send_response_meta_data();

            receive_data();
            init_send_and_receive_data();

            response_meta_data_json.removeMember("api_callbacks");
            simulation.api_callbacks.clear();
            simulation.request_meta_data_json.removeMember("api_callbacks");
            simulation.api_callbacks_response.clear();
            simulation.request_meta_data_json["send"] = request_meta_data_json["send"];
            simulation.request_meta_data_json["receive"] = request_meta_data_json["receive"];

            const Json::Value api_callbacks_response = request_meta_data_json["api_callbacks_response"];
            for (const Json::Value& api_callback_response : api_callbacks_response)
            {
                for (const std::string& callback_key : api_callback_response.getMemberNames())
                {
                    std::map<std::string, std::vector<std::string>> api_callback_map;
                    api_callback_map[callback_key] = std::vector<std::string>{};
                    for (const Json::Value& param : api_callback_response[callback_key])
                    {
                        api_callback_map[callback_key].push_back(param.asString());
                    }
                    simulation.api_callbacks_response.push_back(api_callback_map);
                }
            }

            break;
        }

        now = get_time_now();
        if (now - start > 1)
        {
            printf("[Server] Socket %s is waiting for send data to be sent.\n", socket_addr.c_str());
            start = now;
        }
    }

    request_meta_data_json = simulation.request_meta_data_json;

    send_buffer.buffer_double.data_vec.clear();
    send_buffer.buffer_uint8_t.data_vec.clear();
    send_buffer.buffer_uint16_t.data_vec.clear();
    receive_buffer.buffer_double.data_vec.clear();
    receive_buffer.buffer_uint8_t.data_vec.clear();
    receive_buffer.buffer_uint16_t.data_vec.clear();
}

void MultiverseServer::send_receive_data()
{
    if (ShutdownManager::is_shutdown())
    {
        const int message_spec_int = 0;
        send_message(&message_spec_int, sizeof(message_spec_int), /*more*/ false);
    }
    else
    {
        const int message_spec_int = 2 + (receive_buffer.buffer_double.size > 0) +
                                     (receive_buffer.buffer_uint8_t.size > 0) +
                                     (receive_buffer.buffer_uint16_t.size > 0);
        send_message(&message_spec_int, sizeof(message_spec_int), /*more*/ true);
    }

    double message_time;
    if (worlds[world_name].simulations[simulation_name].meta_data_state == EMetaDataState::Reset)
    {
        worlds[world_name].simulations[simulation_name].meta_data_state = EMetaDataState::Normal;
        const double world_time = 0.0;
        memcpy(&message_time, &world_time, sizeof(double));
    }
    else
    {
        memcpy(&message_time, &worlds[world_name].time, sizeof(double));
    }

    if (receive_buffer.buffer_double.size > 0 || receive_buffer.buffer_uint8_t.size > 0 ||
        receive_buffer.buffer_uint16_t.size > 0)
    {
        send_message(&message_time, sizeof(message_time), /*more*/ true);
        if (receive_buffer.buffer_double.size > 0)
        {
            if (receive_buffer.buffer_uint8_t.size > 0 || receive_buffer.buffer_uint16_t.size > 0)
            {
                send_message(receive_buffer.buffer_double.data, receive_buffer.buffer_double.size * sizeof(double),
                    /*more*/ true);
            }
            else
            {
                send_message(receive_buffer.buffer_double.data, receive_buffer.buffer_double.size * sizeof(double),
                    /*more*/ false);
            }
        }

        if (receive_buffer.buffer_uint8_t.size > 0)
        {
            if (receive_buffer.buffer_uint16_t.size > 0)
            {
                send_message(receive_buffer.buffer_uint8_t.data, receive_buffer.buffer_uint8_t.size * sizeof(double),
                    /*more*/ true);
            }
            else
            {
                send_message(receive_buffer.buffer_uint8_t.data, receive_buffer.buffer_uint8_t.size * sizeof(double),
                    /*more*/ false);
            }
        }

        if (receive_buffer.buffer_uint16_t.size > 0)
        {
            send_message(receive_buffer.buffer_uint16_t.data, receive_buffer.buffer_uint16_t.size * sizeof(double),
                /*more*/ false);
        }
    }
    else
    {
        send_message(&message_time, sizeof(message_time), /*more*/ false);
    }
}

#if USE_ZMQ
void start_multiverse_server(const std::string& server_socket_addr)
{
    mv_log("[Server-ZMQ] Dispatcher started at %s", server_socket_addr.c_str());

    zmq::socket_t server_socket(server_context, zmq::socket_type::rep);
    server_socket.set(zmq::sockopt::rcvtimeo, 100);
    server_socket.set(zmq::sockopt::linger, 0);
    server_socket.bind(server_socket_addr);

    std::map<std::string, std::shared_ptr<MultiverseServer>> servers;
    std::map<std::string, std::thread> workers;
    std::mutex worker_mutex;

    while (!ShutdownManager::is_shutdown())
    {
        zmq::message_t request;
        auto res = server_socket.recv(request, zmq::recv_flags::none);

        if (!res)
        {
            if (ShutdownManager::is_shutdown())
                break;
            continue;
        }

        std::string receive_addr = request.to_string();
        mv_log("[Server-ZMQ] Received handshake request: %s", receive_addr.c_str());

        std::thread old_thread;
        std::shared_ptr<MultiverseServer> old_server;

        {
            std::lock_guard<std::mutex> lock(worker_mutex);

            auto it = workers.find(receive_addr);
            if (it != workers.end())
            {
                mv_log("[Server-ZMQ] Found existing worker on %s — stopping…", receive_addr.c_str());
                old_thread = std::move(it->second);
                old_server = servers[receive_addr];

                workers.erase(it);
                servers.erase(receive_addr);
            }
        }

        if (old_server)
        {
            mv_log("[Server-ZMQ] Stopping worker %s", receive_addr.c_str());
            old_server->stop();

            if (old_thread.joinable())
                old_thread.join();

            mv_log("[Server-ZMQ] Old worker stopped: %s", receive_addr.c_str());
        }

        auto server = std::make_shared<MultiverseServer>(receive_addr);

        {
            std::lock_guard<std::mutex> lock(worker_mutex);

            servers[receive_addr] = server;

            workers[receive_addr] = std::thread([server, receive_addr]() {
                try
                {
                    mv_log("[Server-ZMQ-Worker] Starting worker on %s", receive_addr.c_str());
                    server->start();
                    mv_log("[Server-ZMQ-Worker] Worker %s exited", receive_addr.c_str());
                }
                catch (const std::exception& e)
                {
                    mv_log("[Server-ZMQ-Worker] Exception on %s: %s", receive_addr.c_str(), e.what());
                }
            });
        }

        zmq::message_t response(receive_addr.size());
        memcpy(response.data(), receive_addr.data(), receive_addr.size());
        server_socket.send(response, zmq::send_flags::none);
    }

    mv_log("[Server-ZMQ] Shutdown requested. Stopping workers...");

    {
        std::lock_guard<std::mutex> lock(worker_mutex);
        for (auto& [addr, srv] : servers)
        {
            mv_log("[Server-ZMQ] Stopping worker server: %s", addr.c_str());
            srv->stop();
        }
    }

    for (auto& [addr, t] : workers)
    {
        mv_log("[Server-ZMQ] Joining thread: %s", addr.c_str());
        if (t.joinable())
            t.join();
    }

    server_socket.set(zmq::sockopt::linger, 0);
    server_socket.close();
    server_context.shutdown();
    mv_log("[Server-ZMQ] Dispatcher stopped cleanly.");
}
#endif

#if USE_TCP
void start_multiverse_server_tcp(const std::string& host, const std::string& port)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        fprintf(stderr, "WSAStartup failed\n");
        return;
    }
#endif

    mv_log("[Server-TCP] Dispatcher listening on %s:%s", host.c_str(), port.c_str());

    SOCKET listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == INVALID_SOCKET)
    {
        perror("socket");
        return;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(std::stoi(port)));
    server_addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (::bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        CLOSESOCK(listen_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (::listen(listen_fd, 8) < 0)
    {
        perror("listen");
        CLOSESOCK(listen_fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    std::map<std::string, std::thread> workers;

    while (!ShutdownManager::is_shutdown())
    {
        mv_log("[Server-TCP] Waiting for client connection on %s:%s", host.c_str(), port.c_str());

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);

        // Wait for up to 1 second for a connection
        timeval tv{};
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int retval = select(listen_fd + 1, &readfds, nullptr, nullptr, &tv);

        if (retval < 0)
        {
            // Error or interrupted by signal
            if (ShutdownManager::is_shutdown())
                break;
#ifdef _WIN32
            mv_log("[Server-TCP] select() error: %d", WSAGetLastError());
#else
            perror("select");
#endif
            continue;
        }
        else if (retval == 0)
        {
            // Timeout → just loop again to check shutdown
            continue;
        }

        if (!FD_ISSET(listen_fd, &readfds))
            continue;

        // If we’re here, a client is ready to connect
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        SOCKET client_fd = ::accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == INVALID_SOCKET)
        {
            if (ShutdownManager::is_shutdown())
                break;
#ifdef _WIN32
            mv_log("[Server-TCP] accept() error: %d", WSAGetLastError());
#else
            perror("accept");
#endif
            continue;
        }

        mv_log("[Server-TCP] Client connected! %s:%d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        std::vector<std::string> parts;
        if (!rawtcp::recv_parts(client_fd, parts))
        {
            mv_log("[Server-TCP] Failed to receive handshake (protocol error).");
            CLOSESOCK(client_fd);
            continue;
        }

        if (parts.empty() || parts[0].empty())
        {
            mv_log("[Server-TCP] Empty handshake request (no socket_addr).");
            CLOSESOCK(client_fd);
            continue;
        }

        std::string request = parts[0];
        mv_log("[Server-TCP] Received handshake request: \"%s\"", request.c_str());

        std::string client_host = host;
        uint16_t requested_port = 0;
        auto pos = request.find(':');
        if (pos != std::string::npos)
        {
            client_host = request.substr(0, pos);
            try
            {
                requested_port = static_cast<uint16_t>(std::stoi(request.substr(pos + 1)));
            }
            catch (...)
            {
                requested_port = 0;
            }
        }

        uint16_t worker_port = requested_port;
        std::string worker_addr = host + ":" + std::to_string(worker_port);
        mv_log("[Server-TCP] Launching worker for %s", request.c_str());

        if (workers.count(worker_addr))
        {
            mv_log("[Server-TCP] Cleaning up old worker on %s", worker_addr.c_str());
            if (workers[worker_addr].joinable())
            {
                workers[worker_addr].join();
            }
            workers.erase(worker_addr);
        }

        workers[worker_addr] = std::thread([worker_addr, host]() {
            try
            {
                mv_log("[Server-TCP-Worker] Starting at thread %s", worker_addr.c_str());
                MultiverseServer server(host, worker_addr.substr(worker_addr.find(':') + 1), ServerTransportType::Tcp);
                server.start();
            }
            catch (const std::exception& e)
            {
                mv_log("[Server-TCP-Worker] Exception on %s: %s", worker_addr.c_str(), e.what());
            }
        });

        std::vector<std::string> resp = {worker_addr};
        if (!rawtcp::send_parts(client_fd, resp))
        {
            mv_log("[Server-TCP] Failed to send handshake response to client.");
        }

        CLOSESOCK(client_fd);
        mv_log("[Server-TCP] Connection closed. %s:%d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    }

    mv_log("[Server-TCP] Dispatcher shutting down, waiting for workers...");
    for (auto& [addr, t] : workers)
        if (t.joinable())
            t.join();

    CLOSESOCK(listen_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    mv_log("[Server-TCP] Dispatcher stopped.");
}
#endif

// ===========================================================================
// UDP SERVER
// ===========================================================================
#if USE_UDP
void start_multiverse_server_udp(const std::string& host, const std::string& port)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        fprintf(stderr, "WSAStartup failed\n");
        return;
    }
#endif

    mv_log("[Server-UDP] Dispatcher binding on %s:%s", host.c_str(), port.c_str());

    SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
    {
        perror("socket");
        return;
    }

    int opt = 1;
#ifdef _WIN32
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(static_cast<uint16_t>(std::stoi(port)));
    srv.sin_addr.s_addr = inet_addr(host.c_str());
    if (::bind(sock, reinterpret_cast<sockaddr*>(&srv), sizeof(srv)) < 0)
    {
        perror("bind");
        CLOSESOCK(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    std::map<std::string, std::shared_ptr<MultiverseServer>> servers;
    std::map<std::string, std::thread> workers;
    std::mutex workers_mutex;

    mv_log("[Server-UDP] Dispatcher started, waiting for handshakes...");

    while (!ShutdownManager::is_shutdown())
    {
        std::vector<std::string> parts;
        sockaddr_storage from{};
        socklen_t fromlen = sizeof(from);

        if (!rawudp::recv_parts_from(sock, parts, (sockaddr*)&from, &fromlen, 1000))
        {
            if (ShutdownManager::is_shutdown())
                break;
            continue;
        }

        if (parts.empty() || parts[0].empty())
        {
            mv_log("[Server-UDP] Empty handshake request.");
            continue;
        }

        std::string request = parts[0];
        mv_log("[Server-UDP] Received handshake request: \"%s\"", request.c_str());

        std::string client_host = host;
        uint16_t requested_port = 0;
        auto pos = request.find(':');
        if (pos != std::string::npos)
        {
            client_host = request.substr(0, pos);
            try
            {
                requested_port = static_cast<uint16_t>(std::stoi(request.substr(pos + 1)));
            }
            catch (...)
            {
                requested_port = 0;
            }
        }

        uint16_t worker_port = requested_port;
        std::string worker_addr = host + ":" + std::to_string(worker_port);

        std::thread old_thread;
        std::shared_ptr<MultiverseServer> old_server;

        {
            std::unique_lock<std::mutex> lock(workers_mutex);
            auto it = workers.find(worker_addr);
            if (it != workers.end())
            {
                mv_log("[Server-UDP] Stopping old worker on %s", worker_addr.c_str());
                old_thread = std::move(it->second);
                old_server = servers[worker_addr];
                workers.erase(it);
                servers.erase(worker_addr);
            }
        }

        if (old_server)
        {
            old_server->stop();
            mv_log("[Server-UDP] Joining old worker thread on %s", worker_addr.c_str());
            if (old_thread.joinable())
                old_thread.join();
            mv_log("[Server-UDP] Old worker on %s stopped and cleaned up.", worker_addr.c_str());
        }

        mv_log("[Server-UDP] Launching new worker for %s", worker_addr.c_str());
        auto server = std::make_shared<MultiverseServer>(host, std::to_string(worker_port), ServerTransportType::Udp);

        {
            std::lock_guard<std::mutex> lock(workers_mutex);
            servers[worker_addr] = server;
            workers[worker_addr] = std::thread([server, worker_addr]() {
                try
                {
                    mv_log("[Server-UDP-Worker] Starting worker on %s", worker_addr.c_str());
                    server->start();
                    mv_log("[Server-UDP-Worker] Worker %s exited cleanly.", worker_addr.c_str());
                }
                catch (const std::exception& e)
                {
                    mv_log("[Server-UDP-Worker] Exception on %s: %s", worker_addr.c_str(), e.what());
                }
            });
        }

        std::vector<std::string> resp = {worker_addr};
        if (!rawudp::send_parts_to(sock, resp, (sockaddr*)&from, fromlen))
            mv_log("[Server-UDP] Failed to send handshake response to client.");
    }

    mv_log("[Server-UDP] Shutdown requested — stopping all workers...");

    {
        std::unique_lock<std::mutex> lock(workers_mutex);
        for (auto& [addr, srv] : servers)
        {
            if (srv)
            {
                mv_log("[Server-UDP] Stopping worker server: %s", addr.c_str());
                srv->stop();
            }
        }
        lock.unlock();
        
        for (auto& [addr, t] : workers)
        {
            if (t.joinable())
            {
                mv_log("[Server-UDP] Joining worker thread: %s", addr.c_str());
                t.join();
            }
        }
    }

    CLOSESOCK(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    mv_log("[Server-UDP] Dispatcher stopped cleanly.");
}

#endif
