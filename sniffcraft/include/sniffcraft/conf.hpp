#pragma once

#include "sniffcraft/Json/Json.hpp"

#include <ctime>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>

class Conf
{
public:
    static const std::string active_conf_key;
    static const std::string server_address_key;
    static const std::string local_port_key;
    static const std::string text_file_log_key;
    static const std::string binary_file_log_key;
    static const std::string console_log_key;
    static const std::string raw_bytes_log_key;
    static const std::string network_recap_to_console_key;
    static const std::string packets_key;
    static const std::string ignored_clientbound_key;
    static const std::string ignored_serverbound_key;
    static const std::string detailed_clientbound_key;
    static const std::string detailed_serverbound_key;

    static bool headless;
    static std::string conf_path;
    static std::optional<std::string> active_conf;
    static std::shared_mutex conf_mutex;

public:
    static Json::Value LoadConf();
    static void SaveConf(const Json::Value& conf);
    static std::time_t GetModifiedTimestamp();
    static std::set<std::string> GetConfList();
    static void DeleteConf(const std::string& name);

private:
    static Json::Value LoadConfFile();
};
