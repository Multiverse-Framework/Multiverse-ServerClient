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
#include <limits>
#include <json/json.h>

#ifndef KB_ERROR
#define KB_ERROR(x) std::cerr << "[ERROR] " << x << std::endl
#endif
#ifndef KB_INFO
#define KB_INFO(x) std::cout << "[INFO] " << x << std::endl
#endif

#include "multiverse_client_json.h"

// ---- attribute sizes ----
static const std::map<std::string, size_t> attribute_map_double = {
    {"", 0},
    {"time", 1},
    {"position", 3},
    {"quaternion", 4},
    {"cmd_joint_rvalue", 1},
    {"joint_position", 3},
    {"joint_quaternion", 4},
    {"force", 3},
    {"torque", 3},
};

class MultiverseKnowRobConnector : public MultiverseClientJson {
public:
    enum class Mode { Sender, Receiver };

    MultiverseKnowRobConnector(const std::string& world,
                               const std::string& sim,
                               const std::string& host_,
                               const std::string& server_port_,
                               const std::string& client_port_,
                               Mode mode,
                               TransportType transport)
    : mode_(mode)
    {
        // local metadata/schemas (client-side defaults)
        meta_data["world_name"]      = world;
        meta_data["simulation_name"] = sim;
        meta_data["length_unit"]     = "m";
        meta_data["angle_unit"]      = "rad";
        meta_data["mass_unit"]       = "kg";
        meta_data["time_unit"]       = "s";
        meta_data["handedness"]      = "rhs";

        // base transport endpoints
        host        = host_;
        server_port = server_port_;
        client_port = client_port_;

        if (mode_ == Mode::Sender) {
            send_objects = {
                {"object1", {"position", "quaternion"}},
                {"object2", {"position", "quaternion"}},
                {"knob",    {"cmd_joint_rvalue"}}
            };
            receive_objects.clear();
        } else {
            receive_objects = {
                {"object1", {"position", "quaternion"}},
                {"object2", {"position", "quaternion"}},
                {"knob",    {"cmd_joint_rvalue"}}
            };
        }

        set_transport(transport);
        connect();

        if (world_time) *world_time = 0.0;
        reset(); 

        std::cout << "[Init] mode=" << (mode_==Mode::Sender ? "Sender" : "Receiver")
                  << " transport=" << (transport == TransportType::Tcp ? "tcp" : "zmq")
                  << " host=" << host << " server=" << server_port
                  << " data=" << client_port << "\n";
    }

    ~MultiverseKnowRobConnector() { stop(); }

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

    void set_object_pose(const std::string &obj,
                         const double pos[3],
                         const double quat[4])
    {
        auto it = send_objects_data.find(obj);
        if (it == send_objects_data.end())
        {
            std::cerr << "[set_object_pose] object not found in send_objects_data: " << obj << "\n";
            return;
        }

        auto &amap = it->second;
        auto ip = amap.find("position");
        auto iq = amap.find("quaternion");

        // Write position
        if (ip != amap.end() && ip->second.size() == 3)
        {
            for (size_t i = 0; i < 3; ++i)
                *(ip->second[i]) = pos[i];
        }
        else
        {
            std::cerr << "[set_object_pose] '" << obj
                      << "' is missing attribute 'position' (size 3).\n";
        }

        // Write quaternion
        if (iq != amap.end() && iq->second.size() == 4)
        {
            for (size_t i = 0; i < 4; ++i)
                *(iq->second[i]) = quat[i];
        }
        else
        {
            std::cerr << "[set_object_pose] '" << obj
                      << "' is missing attribute 'quaternion' (size 4).\n";
        }

        // ---- rate-limited debug print (every ~250 ms) ----
        using clock = std::chrono::steady_clock;
        static auto last = clock::now();
        auto now = clock::now();
        if (now - last >= std::chrono::milliseconds(250))
        {
            last = now;
            std::cout << "[pose->send] " << obj
                      << "  pos=[" << pos[0] << ", " << pos[1] << ", " << pos[2] << "]"
                      << "  quat=[" << quat[0] << ", " << quat[1] << ", " << quat[2] << ", " << quat[3] << "]\n";
        }
    }

    void set_knob(double v) { knob_.store(v); }

    void log_receive_snapshot(std::chrono::milliseconds every = std::chrono::milliseconds(250)) const
    {
        using clock = std::chrono::steady_clock;
        static thread_local auto last = clock::time_point{};
        auto now = clock::now();
        if (last != clock::time_point{} && (now - last) < every)
            return;
        last = now;

        double t = (world_time ? *world_time : -1.0);
        std::cout << "[recv] t=" << t << " s\n";

        for (const auto &[obj, attrs] : receive_objects_data)
        {
            std::cout << "  [" << (obj.empty() ? "(default)" : obj) << "]";
            if (attrs.empty())
            {
                std::cout << " <no-attrs>\n";
                continue;
            }
            std::cout << "\n";
            for (const auto &[name, vec] : attrs)
            {
                std::cout << "    " << name << " = [";
                for (size_t i = 0; i < vec.size(); ++i)
                {
                    double v = vec[i] ? *vec[i] : std::numeric_limits<double>::quiet_NaN();
                    std::cout << v << (i + 1 < vec.size() ? ", " : "");
                }
                std::cout << "]\n";
            }
        }
    }

    const std::map<std::string, std::map<std::string, std::vector<double*>>>&
    receive_map() const { log_receive_snapshot(); return receive_objects_data; }

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
    }

    void bind_response_meta_data() override {
        std::cout << "[Meta] " << response_meta_data_str << "\n";

        auto accept = [](const Json::Value& root,
                         std::map<std::string, std::set<std::string>>& out) {
            out.clear();
            if (!root.isObject()) return;
            for (const auto& obj : root.getMemberNames()) {
                std::set<std::string> attrs;
                const Json::Value& entry = root[obj];
                if (entry.isObject()) {
                    // attributes are member names
                    for (const auto& attr : entry.getMemberNames()) attrs.insert(attr);
                } else if (entry.isArray()) {
                    // or server may echo arrays of attribute names
                    for (const auto& v : entry) if (v.isString()) attrs.insert(v.asString());
                }
                if (!attrs.empty()) out[obj] = std::move(attrs);
            }
        };
        accept(response_meta_data_json["send"],    send_objects);
        accept(response_meta_data_json["receive"], receive_objects);
    }

    void init_send_and_receive_data() override {
        double *sp = send_buffer.buffer_double.data;
        for (const auto& so : send_objects) {
            auto &mp = send_objects_data[so.first];
            for (const auto& attr : so.second) {
                auto &vec = mp[attr];
                vec.clear();
                size_t n = 0;
                if (auto it=attribute_map_double.find(attr); it!=attribute_map_double.end())
                    n = it->second;
                for (size_t i=0;i<n;i++) vec.emplace_back(sp++);
            }
        }

        if (auto it=send_objects_data.find("knob"); it!=send_objects_data.end()) {
            auto jt = it->second.find("cmd_joint_rvalue");
            if (jt!=it->second.end() && !jt->second.empty()) knob_ptr_ = jt->second[0];
        }

        double *rp = receive_buffer.buffer_double.data;
        for (const auto& ro : receive_objects) {
            auto &mp = receive_objects_data[ro.first];
            for (const auto& attr : ro.second) {
                auto &vec = mp[attr];
                vec.clear();
                size_t n = 0;
                if (auto it=attribute_map_double.find(attr); it!=attribute_map_double.end())
                    n = it->second;
                for (size_t i=0;i<n;i++) vec.emplace_back(rp++);
            }
        }
    }

    void bind_send_data() override {
        if (world_time) *world_time = get_time_now() - sim_start_time;
        if (knob_ptr_) *knob_ptr_ = knob_.load();

        if (mode_ == Mode::Sender) {
            const double t = get_time_now() - sim_start_time;
            double pos1[3]  = { std::sin(t), 2.0, 3.0 };
            double quat1[4] = { 1.0, 0.0, 0.0, 0.0 };
            double pos2[3]  = { 3.0, 4.0 + 0.5*std::sin(t), 5.0 };
            double quat2[4] = { 0.0, 0.0, 0.0, 1.0 };
            set_object_pose("object1", pos1, quat1);
            set_object_pose("object2", pos2, quat2);
        }
    }

    void bind_api_callbacks() override {}
    void bind_api_callbacks_response() override {}

    void bind_receive_data() override {}
    void clean_up() override {
        send_objects_data.clear();
        receive_objects_data.clear();
    }
    void reset() override { sim_start_time = get_time_now(); }

private:
    // local state
    std::map<std::string, std::string> meta_data;
    std::map<std::string, std::set<std::string>> send_objects;
    std::map<std::string, std::set<std::string>> receive_objects;
    std::map<std::string, std::map<std::string, std::vector<double *>>> send_objects_data;
    std::map<std::string, std::map<std::string, std::vector<double *>>> receive_objects_data;

    Mode mode_;
    std::thread th_;
    std::atomic<bool> stop_flag_{false};

    std::atomic<double> knob_{0.0};
    double* knob_ptr_ = nullptr;

    double sim_start_time{0.0};
};

// ------------------- App / CLI -------------------
static std::atomic<bool> g_exit{false};
static void on_signal(int){ g_exit.store(true); }

struct Args {
    std::string world="my_world", sim="sim01", host="127.0.0.1";
    std::string server="7000", data="7001";
    std::string mode="receiver";      // "sender" or "receiver"
    std::string transport="";         // "tcp" or "zmq" (default decided below)
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
        else if (k=="--client" || k=="--data") take(a.data);
        else if (k=="--mode") take(a.mode);
        else if (k=="--transport") take(a.transport);
    }
    return a;
}

static void print_vec(const std::vector<double*>& v) {
    std::cout << "[";
    for (size_t i=0;i<v.size();++i){
        std::cout << (v[i]? *v[i] : 0.0);
        if (i+1<v.size()) std::cout << ", ";
    }
    std::cout << "]";
}

static void print_attr_or_na(const std::map<std::string, std::vector<double*>>& attrs,
                             const char* name) {
    std::cout << name << "=";
    auto it = attrs.find(name);
    if (it != attrs.end()) {
        print_vec(it->second);
    } else {
        std::cout << "<n/a>";
    }
}

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    Args args = parse_args(argc, argv);

    // decide default transport
#ifdef USE_ZMQ
    if (args.transport.empty()) args.transport = "zmq";
#else
    if (args.transport.empty()) args.transport = "tcp";
#endif

    auto mode = (args.mode=="sender") ? MultiverseKnowRobConnector::Mode::Sender
                                      : MultiverseKnowRobConnector::Mode::Receiver;

    TransportType tt = TransportType::Tcp;
    if (args.transport=="zmq") tt = TransportType::Zmq;
    else if (args.transport=="tcp") tt = TransportType::Tcp;
    else {
        KB_ERROR("Unknown --transport " << args.transport << " (use 'tcp' or 'zmq')");
        return 2;
    }

    std::cout << "[App] mode=" << args.mode
              << " transport=" << args.transport
              << " server=" << args.server << " data=" << args.data
              << " host=" << args.host << "\n";

    MultiverseKnowRobConnector cli(args.world, args.sim, args.host, args.server, args.data, mode, tt);
    cli.start();

    while (!g_exit.load()) {
        if (mode == MultiverseKnowRobConnector::Mode::Receiver) {
            const auto& rod = cli.receive_map();
            auto it1 = rod.find("object1");
            auto it2 = rod.find("object2");

            std::cout << "---\n";
            if (it1 != rod.end()) {
                print_attr_or_na(it1->second, "position");   std::cout << "  ";
                print_attr_or_na(it1->second, "quaternion"); std::cout << "\n";
            }
            if (it2 != rod.end()) {
                print_attr_or_na(it2->second, "position");   std::cout << "  ";
                print_attr_or_na(it2->second, "quaternion"); std::cout << "\n";
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    cli.stop();
    return 0;
}
