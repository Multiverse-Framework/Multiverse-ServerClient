#include <map>
#include <set>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <csignal>
#include <cmath>
#include <numeric>
#include <limits>
#include <json/json.h>

#ifndef LOG_ERROR
#define LOG_ERROR(x) std::cerr << "[ERROR] " << x << std::endl
#endif
#ifndef LOG_INFO
#define LOG_INFO(x) std::cout << "[INFO] " << x << std::endl
#endif

#include "multiverse_client_json.h"

// ---- attribute sizes ----
static const std::map<std::string, size_t> attribute_map_double = {
    {"", 0},
    {"time", 1},
    {"position", 3},
    {"quaternion", 4},
    {"joint_angular_position", 1},
    {"joint_linear_position", 1},
    {"force", 3},
    {"torque", 3},
    {"rgb_3840_2160", 0},
    {"rgb_1280_1024", 0},
    {"rgb_640_480", 0},
    {"rgb_128_128", 0},
};

static const std::map<std::string, size_t> attribute_map_uint8_t = {
    {"", 0},
    {"time", 0},
    {"position", 0},
    {"quaternion", 0},
    {"joint_angular_position", 0},
    {"joint_linear_position", 0},
    {"force", 0},
    {"torque", 0},
    {"rgb_3840_2160", 3840 * 2160 * 3},
    {"rgb_1280_1024", 1280 * 1024 * 3},
    {"rgb_640_480", 640 * 480 * 3},
    {"rgb_128_128", 128 * 128 * 3},
};

// ---- transport name helper (for logs) ----
static const char* transport_name(TransportType t) {
    switch (t) {
#if USE_ZMQ
        case TransportType::Zmq: return "zmq";
#endif
#if USE_TCP
        case TransportType::Tcp: return "tcp";
#endif
#if USE_UDP
        case TransportType::Udp: return "udp";
#endif
        default: break;
    }
    return "?";
}

// ---- compile-time helpers ----
static bool is_transport_enabled(const std::string& name) {
#if USE_ZMQ
    if (name == "zmq") return true;
#endif
#if USE_TCP
    if (name == "tcp") return true;
#endif
#if USE_UDP
    if (name == "udp") return true;
#endif
    return false;
}

static const char* default_enabled_transport() {
#if USE_ZMQ
    return "zmq";
#elif USE_TCP
    return "tcp";
#elif USE_UDP
    return "udp";
#else
    return "?";
#endif
}

// ---------------- Connector ----------------
class MyConnector : public MultiverseClientJson {
public:
    enum class Mode { Sender, Receiver, Both1, Both2, ImageSender, ImageReceiver };

    MyConnector(const std::string& world,
                               const std::string& sim,
                               const std::string& host_,
                               const std::string& server_port_,
                               const std::string& client_port_,
                               Mode mode,
                               TransportType transport)
    : mode_(mode)
    {
        meta_data["world_name"]      = world;
        meta_data["simulation_name"] = sim;
        meta_data["length_unit"]     = "m";
        meta_data["angle_unit"]      = "rad";
        meta_data["mass_unit"]       = "kg";
        meta_data["time_unit"]       = "s";
        meta_data["handedness"]      = "rhs";

        host        = host_;
        server_port = server_port_;
        client_port = client_port_;
        
        switch (mode_) {
        case Mode::Sender:
            send_objects = {
                {"object1", {"position", "quaternion"}},
                {"object2", {"position", "quaternion"}},
                {"joint1", {"joint_angular_position"}}
            };
            receive_objects.clear();
            break;
        case Mode::Receiver:
            send_objects.clear();
            receive_objects = {
                {"object1", {"position", "quaternion"}},
                {"object2", {"position", "quaternion"}},
                {"joint1", {"joint_angular_position"}}
            };
            break;
        case Mode::Both1:
            send_objects = {
                {"object3", {"position"}},
                {"object4", {"quaternion"}}
            };
            receive_objects = {
                {"object3", {"quaternion"}},
                {"object4", {"position"}},
                {"joint2", {"joint_angular_position"}}
            };
            break;
        case Mode::Both2:
            send_objects = {
                {"object3", {"quaternion"}},
                {"object4", {"position"}},
                {"joint2", {"joint_angular_position"}}
            };
            receive_objects = {
                {"object3", {"position"}},
                {"object4", {"quaternion"}},
            };
            break;
        case Mode::ImageSender:
            send_objects = {
                {"image1", {"rgb_3840_2160"}},
            };
            receive_objects.clear();
            break;
        case Mode::ImageReceiver:
            send_objects.clear();
            receive_objects = {
                {"image1", {"rgb_3840_2160"}},
            };
            break;
        }

        set_transport(transport);

        std::cout << "[Init] Connecting to server...\n";
        connect();
        std::cout << "[Init] Connected to server.\n";

        if (world_time) *world_time = 0.0;
        reset(); 

        std::cout << "[Init] mode=" << (mode_==Mode::Sender ? "Sender" : (mode_==Mode::Receiver ? "Receiver" : (mode_==Mode::Both1 ? "Both1" : (mode_==Mode::Both2 ? "Both2" : (mode_==Mode::ImageSender ? "ImageSender" : "ImageReceiver")))))
                  << " transport=" << transport_name(transport)
                  << " host=" << host 
                  << " server=" << server_port << " client=" << client_port << "\n";
    }

    ~MyConnector() { stop(); }

    void start() {
        communicate(true);
        communicate();
        stop_flag_ = false;
        th_ = std::thread([this](){
            while (!stop_flag_) {
                if (!communicate()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }

    void stop() {
        stop_flag_ = true;
        if (th_.joinable()) th_.join();
    }

    void set_send_object(const std::string &obj,
                         const std::string &attr,
                         const double *data,
                         const size_t data_size)
    {
        auto it = send_objects_double_data.find(obj);
        if (it == send_objects_double_data.end())
        {
            LOG_ERROR("Object '" << obj << "' not found in send_objects_double_data");
            return;
        }

        auto &amap = it->second;
        auto ip = amap.find(attr);
        if (ip == amap.end())
        {
            LOG_ERROR("Attribute '" << attr << "' not found in send_objects_double_data['" << obj << "']");
            return;
        }

        if (ip->second.size() != data_size)
        {
            LOG_ERROR("Size mismatch for " << obj << "." << attr
                                           << " expected=" << ip->second.size() << " got=" << data_size);
            return;
        }

        for (size_t i = 0; i < data_size; ++i)
        {
            if (!ip->second[i])
            {
                LOG_ERROR("Null pointer in send_objects_double_data[" << obj << "][" << attr << "][" << i << "]");
                return;
            }
            *(ip->second[i]) = data[i];
        }
    }

    void set_send_object(const std::string &obj,
                         const std::string &attr,
                         const uint8_t *data,
                         const size_t data_size)
    {
        auto it = send_objects_uint8_t_data.find(obj);
        if (it == send_objects_uint8_t_data.end())
        {
            LOG_ERROR("Object '" << obj << "' not found in send_objects_uint8_t_data");
            return;
        }

        auto &amap = it->second;
        auto ip = amap.find(attr);
        if (ip == amap.end())
        {
            LOG_ERROR("Attribute '" << attr << "' not found in send_objects_uint8_t_data['" << obj << "']");
            return;
        }

        if (ip->second.size() != data_size)
        {
            LOG_ERROR("Size mismatch for " << obj << "." << attr
                                           << " expected=" << ip->second.size() << " got=" << data_size);
            return;
        }

        for (size_t i = 0; i < data_size; ++i)
        {
            if (!ip->second[i])
            {
                LOG_ERROR("Null pointer in send_objects_uint8_t_data[" << obj << "][" << attr << "][" << i << "]");
                return;
            }
            *(ip->second[i]) = data[i];
        }
    }

    void log_receive_snapshot(std::chrono::milliseconds every = std::chrono::milliseconds(250)) const {
        using clock = std::chrono::steady_clock;
        static thread_local auto last = clock::time_point{};
        auto now = clock::now();
        if (last != clock::time_point{} && (now - last) < every)
            return;
        last = now;

        double t = (world_time ? *world_time : -1.0);
        std::cout << "[recv] t=" << t << " s\n";

        for (const auto &[obj, attrs] : receive_objects_double_data) {
            std::cout << "  [" << (obj.empty() ? "(default)" : obj) << "]";
            if (attrs.empty()) {
                std::cout << " <no-attrs>\n";
                continue;
            }
            std::cout << "\n";
            for (const auto &[name, vec] : attrs) {
                std::cout << "    " << name << " = [";
                for (size_t i = 0; i < vec.size(); ++i) {
                    double v = vec[i] ? *vec[i] : std::numeric_limits<double>::quiet_NaN();
                    std::cout << v << (i + 1 < vec.size() ? ", " : "");
                }
                std::cout << "]\n";
            }
        }
        for (const auto &[obj, attrs] : receive_objects_uint8_t_data) {
            std::cout << "  [" << (obj.empty() ? "(default)" : obj) << "]";
            if (attrs.empty()) {
                std::cout << " <no-attrs>\n";
                continue;
            }
            std::cout << "\n";
            for (const auto &[name, vec] : attrs) {
                std::cout << "    " << name << " = [";
                for (size_t i = 0; i < 10; ++i) {
                    uint8_t v = vec[i] ? *vec[i] : 1;
                    std::cout << static_cast<int>(v) << (i + 1 < vec.size() ? ", " : "");
                }
                std::cout << "]\n";
            }
        }
    }

private:
    // ---- MultiverseClientJson overrides ----
    void start_connect_to_server_thread() override { connect_to_server(); }
    void wait_for_connect_to_server_thread_finish() override {}
    void start_meta_data_thread() override { send_and_receive_meta_data(); }
    void wait_for_meta_data_thread_finish() override {}
    bool init_objects(bool) override { return !send_objects.empty() || !receive_objects.empty(); }

    void bind_request_meta_data() override {
        request_meta_data_json = Json::Value(Json::objectValue);

        auto &md = request_meta_data_json["meta_data"];
        md["world_name"] = meta_data["world_name"];
        md["simulation_name"] = meta_data["simulation_name"];
        md["length_unit"] = meta_data["length_unit"];
        md["angle_unit"] = meta_data["angle_unit"];
        md["mass_unit"] = meta_data["mass_unit"];
        md["time_unit"] = meta_data["time_unit"];
        md["handedness"] = meta_data["handedness"];

        request_meta_data_json["send"]    = Json::Value(Json::objectValue);
        request_meta_data_json["receive"] = Json::Value(Json::objectValue);

        for (const auto &p : send_objects)
            for (const auto &a : p.second)
                request_meta_data_json["send"][p.first].append(a);

        for (const auto &p : receive_objects)
            for (const auto &a : p.second)
                request_meta_data_json["receive"][p.first].append(a);

        request_meta_data_str = request_meta_data_json.toStyledString();

        std::cout << "[Meta] " << request_meta_data_str << "\n";
    }

    void bind_response_meta_data() override {
        std::cout << "[Client " << client_port.c_str() << "] Received response meta data.\n";
        // std::cout << "[Meta] " << response_meta_data_str << "\n";

        auto accept = [](const Json::Value& root,
                         std::map<std::string, std::set<std::string>>& out) {
            out.clear();
            if (!root.isObject()) return;
            for (const auto& obj : root.getMemberNames()) {
                std::set<std::string> attrs;
                const Json::Value& entry = root[obj];
                if (entry.isObject())
                    for (const auto& attr : entry.getMemberNames()) attrs.insert(attr);
                else if (entry.isArray())
                    for (const auto& v : entry) if (v.isString()) attrs.insert(v.asString());
                if (!attrs.empty()) out[obj] = std::move(attrs);
            }
        };
        accept(response_meta_data_json["send"],    send_objects);
        accept(response_meta_data_json["receive"], receive_objects);
    }

    void init_send_and_receive_data() override {
        std::cout << "[Init] Initializing double send/receive data buffers...\n";
        const size_t send_double_needed =
            std::accumulate(send_objects.begin(), send_objects.end(), size_t(0),
                [](size_t sum, const auto &so){
                    for (auto &a : so.second)
                        sum += attribute_map_double.at(a);
                    return sum;
                });

        const size_t recv_double_needed =
            std::accumulate(receive_objects.begin(), receive_objects.end(), size_t(0),
                [](size_t sum, const auto &ro){
                    for (auto &a : ro.second)
                        sum += attribute_map_double.at(a);
                    return sum;
                });

        // Allocate backing memory if not yet done
        if (!send_buffer.buffer_double.data) {
            send_buffer.buffer_double.data = new double[send_double_needed];
            send_buffer.buffer_double.size = send_double_needed;
            std::fill_n(send_buffer.buffer_double.data, send_double_needed, 0.0);
        }
        if (!receive_buffer.buffer_double.data) {
            receive_buffer.buffer_double.data = new double[recv_double_needed];
            receive_buffer.buffer_double.size = recv_double_needed;
            std::fill_n(receive_buffer.buffer_double.data, recv_double_needed, 0.0);
        }

        double *sp_double = send_buffer.buffer_double.data;
        for (const auto& so : send_objects) {
            auto &mp = send_objects_double_data[so.first];
            for (const auto& attr : so.second) {
                auto &vec = mp[attr];
                vec.clear();
                size_t n = 0;
                if (auto it=attribute_map_double.find(attr); it!=attribute_map_double.end())
                    n = it->second;
                for (size_t i=0;i<n;i++) vec.emplace_back(sp_double++);
            }
        }

        double *rp_double = receive_buffer.buffer_double.data;
        for (const auto& ro : receive_objects) {
            auto &mp = receive_objects_double_data[ro.first];
            for (const auto& attr : ro.second) {
                auto &vec = mp[attr];
                vec.clear();
                size_t n = 0;
                if (auto it=attribute_map_double.find(attr); it!=attribute_map_double.end())
                    n = it->second;
                for (size_t i=0;i<n;i++) vec.emplace_back(rp_double++);
            }
        }
        
        std::cout << "[Init] Initializing uint8_t send/receive data buffers...\n";
        const size_t send_uint8_t_needed =
            std::accumulate(send_objects.begin(), send_objects.end(), size_t(0),
                [](size_t sum, const auto &so){
                    for (auto &a : so.second)
                        sum += attribute_map_uint8_t.at(a);
                    return sum;
                });

        const size_t recv_uint8_t_needed =
            std::accumulate(receive_objects.begin(), receive_objects.end(), size_t(0),
                [](size_t sum, const auto &ro){
                    for (auto &a : ro.second)
                        sum += attribute_map_uint8_t.at(a);
                    return sum;
                });

        if (!send_buffer.buffer_uint8_t.data) {
            send_buffer.buffer_uint8_t.data = new uint8_t[send_uint8_t_needed];
            send_buffer.buffer_uint8_t.size = send_uint8_t_needed;
            std::fill_n(send_buffer.buffer_uint8_t.data, send_uint8_t_needed, 0.0);
        }
        if (!receive_buffer.buffer_uint8_t.data) {
            receive_buffer.buffer_uint8_t.data = new uint8_t[recv_uint8_t_needed];
            receive_buffer.buffer_uint8_t.size = recv_uint8_t_needed;
            std::fill_n(receive_buffer.buffer_uint8_t.data, recv_uint8_t_needed, 0.0);
        }

        uint8_t *sp_uint8_t = send_buffer.buffer_uint8_t.data;
        for (const auto& so : send_objects) {
            auto &mp = send_objects_uint8_t_data[so.first];
            for (const auto& attr : so.second) {
                auto &vec = mp[attr];
                vec.clear();
                size_t n = 0;
                if (auto it=attribute_map_uint8_t.find(attr); it!=attribute_map_uint8_t.end())
                    n = it->second;
                for (size_t i=0;i<n;i++) vec.emplace_back(sp_uint8_t++);
            }
        }

        uint8_t *rp_uint8_t = receive_buffer.buffer_uint8_t.data;
        for (const auto& ro : receive_objects) {
            auto &mp = receive_objects_uint8_t_data[ro.first];
            for (const auto& attr : ro.second) {
                auto &vec = mp[attr];
                vec.clear();
                size_t n = 0;
                if (auto it=attribute_map_uint8_t.find(attr); it!=attribute_map_uint8_t.end())
                    n = it->second;
                for (size_t i=0;i<n;i++) vec.emplace_back(rp_uint8_t++);
            }
        }
        std::cout << "[Init] Initialization complete.\n";
    }

    void bind_send_data() override {
        if (world_time) *world_time = get_time_now() - sim_start_time;

        if (mode_ == Mode::Sender || mode_ == Mode::Both1 || mode_ == Mode::Both2 || mode_ == Mode::ImageSender) {
            const double t = get_time_now() - sim_start_time;
            double pos1[3]  = { std::sin(t), 2.0, 3.0 };
            double quat1[4] = { std::sin(t), std::cos(t), 0.0, 0.0 };
            double pos2[3]  = { 3.0, 4.0 + 0.5*std::sin(t), 5.0 };
            double quat2[4] = { 0.0, std::sin(t), std::cos(t), 0.0 };
            double joint_angular_position[1] = { 0.5 * std::sin(t) };
            uint8_t *rgb_3840_2160 = new uint8_t[3840 * 2160 * 3];
            for (size_t i = 0; i < 3840 * 2160 * 3; i++) {
                rgb_3840_2160[i] = static_cast<uint8_t>((i + static_cast<size_t>(t * 10)) % 256);
            }
            if (mode_ == Mode::Sender) {
                set_send_object("object1", "position", pos1, 3);
                set_send_object("object1", "quaternion", quat1, 4);
                set_send_object("object2", "position", pos2, 3);
                set_send_object("object2", "quaternion", quat2, 4);
                set_send_object("joint1", "joint_angular_position", joint_angular_position, 1);
            } else if (mode_ == Mode::Both1) {
                set_send_object("object3", "position", pos1, 3);
                set_send_object("object4", "quaternion", quat2, 4);
            } else if (mode_ == Mode::Both2) {
                set_send_object("object3", "quaternion", quat1, 4);
                set_send_object("object4", "position", pos2, 3);
                set_send_object("joint2", "joint_angular_position", joint_angular_position, 1);
            } else if (mode_ == Mode::ImageSender) {
                set_send_object("image1", "rgb_3840_2160",
                                rgb_3840_2160,
                                3840 * 2160 * 3);
            }
            delete[] rgb_3840_2160;
            using clock = std::chrono::steady_clock;
            static auto last = clock::now();
            auto now = clock::now();
            if (now - last >= std::chrono::milliseconds(250)) {
                last = now;
                for (auto send_object_data : send_objects_double_data) {
                    const auto& obj = send_object_data.first;
                    const auto& attrs = send_object_data.second;
                    std::cout << "[send double] " << obj;
                    for (const auto& attr : attrs) {
                        std::cout << "  " << attr.first << "=[";
                        for (size_t i = 0; i < attr.second.size(); ++i) {
                            double v = attr.second[i] ? *attr.second[i] : std::numeric_limits<double>::quiet_NaN();
                            std::cout << v << (i + 1 < attr.second.size() ? ", " : "");
                        }
                        std::cout << "]";
                    }
                    std::cout << "\n";
                }
                for (auto send_object_data : send_objects_uint8_t_data) {
                    const auto& obj = send_object_data.first;
                    const auto& attrs = send_object_data.second;
                    std::cout << "[send uint8_t] " << obj;
                    for (const auto& attr : attrs) {
                        std::cout << "  " << attr.first << "=[";
                        for (size_t i = 0; i < std::min(size_t(10), attr.second.size()); ++i) {
                            uint8_t v = attr.second[i] ? *attr.second[i] : std::numeric_limits<uint8_t>::quiet_NaN();
                            std::cout << static_cast<int>(v) << (i + 1 < 10 ? ", " : "");
                        }
                        std::cout << "]";
                    }
                    std::cout << "\n";
                }
            }
        }
    }

    void bind_api_callbacks() override {}
    void bind_api_callbacks_response() override {}

    void bind_receive_data() override {}
    void clean_up() override {
        if (send_buffer.buffer_double.data)
        {
            delete[] send_buffer.buffer_double.data;
            send_buffer.buffer_double.data = nullptr;
        }
        if (receive_buffer.buffer_double.data)
        {
            delete[] receive_buffer.buffer_double.data;
            receive_buffer.buffer_double.data = nullptr;
        }
        send_objects_double_data.clear();
        receive_objects_double_data.clear();
        if (send_buffer.buffer_uint8_t.data)
        {
            delete[] send_buffer.buffer_uint8_t.data;
            send_buffer.buffer_uint8_t.data = nullptr;
        }
        if (receive_buffer.buffer_uint8_t.data)
        {
            delete[] receive_buffer.buffer_uint8_t.data;
            receive_buffer.buffer_uint8_t.data = nullptr;
        }
        send_objects_uint8_t_data.clear();
        receive_objects_uint8_t_data.clear();
    }
    void reset() override { sim_start_time = get_time_now(); }

private:
    std::map<std::string, std::string> meta_data;
    std::map<std::string, std::set<std::string>> send_objects;
    std::map<std::string, std::set<std::string>> receive_objects;
    std::map<std::string, std::map<std::string, std::vector<double *>>> send_objects_double_data;
    std::map<std::string, std::map<std::string, std::vector<double *>>> receive_objects_double_data;
    std::map<std::string, std::map<std::string, std::vector<uint8_t *>>> send_objects_uint8_t_data;
    std::map<std::string, std::map<std::string, std::vector<uint8_t *>>> receive_objects_uint8_t_data;

    Mode mode_;
    std::thread th_;
    std::atomic<bool> stop_flag_{false};

    double sim_start_time{0.0};
};

// ------------------- App / CLI -------------------
static std::atomic<bool> g_exit{false};
static void on_signal(int){ g_exit.store(true); }

struct Args {
    std::string world="my_world", sim="sim_1";
    std::string host="127.0.0.1";
    std::string server="7000", client="7001";
    std::string mode="receiver";
    std::string transport="";
};

static Args parse_args(int argc, char** argv) {
    Args a;
    for (int i=1;i<argc;i++){
        std::string k = argv[i];
        auto take=[&](std::string& s){ if (i+1<argc) s=argv[++i]; };
        if (k=="--world") take(a.world);
        else if (k=="--sim") take(a.sim);
        else if (k=="--host") take(a.host);
        else if (k=="--server") take(a.server);
        else if (k=="--client") take(a.client);
        else if (k=="--mode") take(a.mode);
        else if (k=="--transport") take(a.transport);
    }
    return a;
}

// static void print_vec(const std::vector<double*>& v) {
//     std::cout << "[";
//     for (size_t i=0;i<v.size();++i){
//         std::cout << (v[i]? *v[i] : 0.0);
//         if (i+1<v.size()) std::cout << ", ";
//     }
//     std::cout << "]";
// }

// static void print_attr_or_na(const std::map<std::string, std::vector<double*>>& attrs,
//                              const char* name) {
//     std::cout << name << "=";
//     auto it = attrs.find(name);
//     if (it != attrs.end()) print_vec(it->second);
//     else std::cout << "<n/a>";
// }

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    Args args = parse_args(argc, argv);

    // ---- show build config ----
    LOG_INFO("Build config: "
#if USE_ZMQ
            "ZMQ=1 "
#else
            "ZMQ=0 "
#endif
#if USE_TCP
            "TCP=1 "
#else
            "TCP=0 "
#endif
#if USE_UDP
            "UDP=1"
#else
            "UDP=0"
#endif
    );

    // ---- default / validate transport ----
    if (args.transport.empty()) {
        args.transport = default_enabled_transport();
        LOG_INFO("Default transport selected: " << args.transport);
    } else if (!is_transport_enabled(args.transport)) {
        LOG_ERROR("Transport '" << args.transport << "' is not compiled in this build.");
        return 3;
    }

    auto mode = (args.mode == "sender" ? MyConnector::Mode::Sender :
                 (args.mode == "receiver" ? MyConnector::Mode::Receiver :
                  (args.mode == "both1" ? MyConnector::Mode::Both1 :
                   (args.mode == "both2" ? MyConnector::Mode::Both2 :
                    (args.mode == "imagesender" ? MyConnector::Mode::ImageSender :
                     (args.mode == "imagereceiver" ? MyConnector::Mode::ImageReceiver :
                      MyConnector::Mode::Sender))))));

    TransportType tt = TransportType::Tcp;
#if USE_ZMQ
    if (args.transport=="zmq") tt = TransportType::Zmq;
#endif
#if USE_TCP
    if (args.transport=="tcp") tt = TransportType::Tcp;
#endif
#if USE_UDP
    if (args.transport=="udp") tt = TransportType::Udp;
#endif

    std::cout << "[App] mode=" << args.mode
              << " transport=" << args.transport
              << " host=" << args.host
              << " server=" << args.server << " client=" << args.client
              << " world=" << args.world << " sim=" << args.sim << "\n";

#ifdef _WIN32
    std::cout << "[Init] Initializing Winsock...\n";
    WSADATA wsaData;
    int wsaerr = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaerr != 0) {
        throw std::runtime_error("WSAStartup failed with error: " + std::to_string(wsaerr));
    }
    std::cout << "[Init] Winsock initialized.\n";
#endif

    MyConnector my_connector(args.world, args.sim, args.host, args.server, args.client, mode, tt);
    my_connector.start();

    while (!g_exit.load()) {
        if (mode == MyConnector::Mode::ImageReceiver) {
            my_connector.log_receive_snapshot();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    my_connector.stop();
    return 0;
}
