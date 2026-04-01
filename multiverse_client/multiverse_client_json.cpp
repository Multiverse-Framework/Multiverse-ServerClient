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

#include "multiverse_client_json.h"

#include <algorithm>

std::map<std::string, size_t> attribute_map_double = {{"", 0}, {"time", 1}, {"scalar", 1}, {"position", 3},
    {"quaternion", 4}, {"linear_velocity", 3}, {"angular_velocity", 3}, {"linear_acceleration", 3},
    {"angular_acceleration", 3}, {"odometric_velocity", 6}, {"joint_linear_position", 1}, {"joint_angular_position", 1},
    {"joint_linear_velocity", 1}, {"joint_angular_velocity", 1}, {"joint_linear_acceleration", 1},
    {"joint_angular_acceleration", 1}, {"joint_force", 1}, {"joint_torque", 1}, {"cmd_joint_linear_position", 1},
    {"cmd_joint_angular_position", 1}, {"cmd_joint_linear_velocity", 1}, {"cmd_joint_angular_velocity", 1},
    {"cmd_joint_force", 1}, {"cmd_joint_torque", 1}, {"joint_position", 3}, {"joint_quaternion", 4}, {"force", 3},
    {"torque", 3}};

bool MultiverseClientJson::compute_request_and_response_meta_data()
{
    // always start from a clean OBJECT
    request_meta_data_json = Json::Value(Json::objectValue);

    // parse & validate server response
    response_meta_data_json = Json::Value(Json::nullValue);
    if (!response_meta_data_str.empty() && reader.parse(response_meta_data_str, response_meta_data_json) &&
        response_meta_data_json.isObject() && response_meta_data_json.isMember("time") &&
        response_meta_data_json["time"].isNumeric() && response_meta_data_json["time"].asDouble() >= 0)
    {
        // copy meta_data if present
        if (response_meta_data_json.isMember("meta_data"))
            request_meta_data_json["meta_data"] = response_meta_data_json["meta_data"];

        // IMPORTANT: ensure "send"/"receive" are OBJECTS
        request_meta_data_json["send"] = Json::Value(Json::objectValue);
        request_meta_data_json["receive"] = Json::Value(Json::objectValue);

        if (response_meta_data_json.isMember("send") && response_meta_data_json["send"].isObject())
        {
            for (const std::string& object_name : response_meta_data_json["send"].getMemberNames())
            {
                request_meta_data_json["send"][object_name] = Json::arrayValue;
                const Json::Value& attrs = response_meta_data_json["send"][object_name];
                if (attrs.isObject())
                {
                    for (const std::string& attribute_name : attrs.getMemberNames())
                        request_meta_data_json["send"][object_name].append(attribute_name);
                }
            }
        }

        if (response_meta_data_json.isMember("receive") && response_meta_data_json["receive"].isObject())
        {
            for (const std::string& object_name : response_meta_data_json["receive"].getMemberNames())
            {
                request_meta_data_json["receive"][object_name] = Json::arrayValue;
                const Json::Value& attrs = response_meta_data_json["receive"][object_name];
                if (attrs.isObject())
                {
                    for (const std::string& attribute_name : attrs.getMemberNames())
                        request_meta_data_json["receive"][object_name].append(attribute_name);
                }
            }
        }

        return true;
    }

    return false;
}

void MultiverseClientJson::compute_request_buffer_sizes(
    std::map<std::string, size_t>& send_buffer_size, std::map<std::string, size_t>& receive_buffer_size) const
{
    std::map<std::string, std::map<std::string, size_t>> sizes = {
        {"send", {{"double", 0}, {"uint8", 0}, {"uint16", 0}}},
        {"receive", {{"double", 0}, {"uint8", 0}, {"uint16", 0}}}};

    for (auto& dir : sizes)
    {
        const std::string& key = dir.first; // "send" / "receive"
        auto& acc = dir.second;

        const Json::Value& obj = request_meta_data_json[key];
        if (!obj.isObject())
        {
            acc["double"] = acc["uint8"] = acc["uint16"] = static_cast<size_t>(-1);
            continue;
        }

        for (const std::string& object_name : obj.getMemberNames())
        {
            const Json::Value& arr = obj[object_name];
            if (!arr.isArray())
            {
                acc["double"] = acc["uint8"] = acc["uint16"] = static_cast<size_t>(-1);
                break;
            }

            for (const Json::Value& attribute : arr)
            {
                if (!attribute.isString())
                {
                    acc["double"] = acc["uint8"] = acc["uint16"] = static_cast<size_t>(-1);
                    break;
                }
                auto it = attribute_map_double.find(attribute.asString());
                if (it != attribute_map_double.end())
                    acc["double"] += it->second;
            }
        }
    }

    send_buffer_size = sizes["send"];
    receive_buffer_size = sizes["receive"];
}

void MultiverseClientJson::compute_response_buffer_sizes(
    std::map<std::string, size_t>& send_buffer_size, std::map<std::string, size_t>& receive_buffer_size) const
{
    std::map<std::string, std::map<std::string, size_t>> sizes = {
        {"send", {{"double", 0}, {"uint8", 0}, {"uint16", 0}}},
        {"receive", {{"double", 0}, {"uint8", 0}, {"uint16", 0}}}};

    for (auto& dir : sizes)
    {
        const std::string& key = dir.first;
        auto& acc = dir.second;

        const Json::Value& obj = response_meta_data_json[key];
        if (!obj.isObject())
            continue;

        for (const std::string& object_name : obj.getMemberNames())
        {
            const Json::Value& attrs = obj[object_name];
            if (!attrs.isObject())
                continue;

            for (const std::string& attribute_name : attrs.getMemberNames())
            {
                if (attribute_map_double.find(attribute_name) != attribute_map_double.end())
                    acc["double"] += attrs[attribute_name].size(); // count elements of that attribute
            }
        }
    }

    send_buffer_size = sizes["send"];
    receive_buffer_size = sizes["receive"];
}