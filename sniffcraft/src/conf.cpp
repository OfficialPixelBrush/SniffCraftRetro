#include "sniffcraft/conf.hpp"

#include <filesystem>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef WIN32
#include <unistd.h>
#else
#define stat _stat
#endif

const std::string Conf::active_conf_key = "ActiveConf";
const std::string Conf::server_address_key = "ServerAddress";
const std::string Conf::local_port_key = "LocalPort";
const std::string Conf::text_file_log_key = "LogToTxtFile";
const std::string Conf::binary_file_log_key = "LogToBinFile";
const std::string Conf::console_log_key = "LogToConsole";
const std::string Conf::raw_bytes_log_key = "LogRawBytes";
const std::string Conf::network_recap_to_console_key = "NetworkRecapToConsole";
const std::string Conf::packets_key = "Packets";
const std::string Conf::ignored_clientbound_key = "ignored_clientbound";
const std::string Conf::ignored_serverbound_key = "ignored_serverbound";
const std::string Conf::detailed_clientbound_key = "detailed_clientbound";
const std::string Conf::detailed_serverbound_key = "detailed_serverbound";

#ifdef WITH_GUI
bool Conf::headless = false;
#else
bool Conf::headless = true;
#endif

std::string Conf::conf_path = "";

std::optional<std::string> Conf::active_conf = std::nullopt;

std::shared_mutex Conf::conf_mutex;

Json::Value Conf::LoadConf()
{
    const Json::Value main_json = LoadConfFile();

    Json::Value json = main_json[active_conf.value()];

    // Set default values if missing
    if (!json.contains(server_address_key))
        json[server_address_key] = "127.0.0.1:25565";
    if (!json.contains(local_port_key))
        json[local_port_key] = 25555;
    if (!json.contains(text_file_log_key))
        json[text_file_log_key] = true;
    if (!json.contains(binary_file_log_key))
        json[binary_file_log_key] = false;
    if (!json.contains(console_log_key))
        json[console_log_key] = true;
    if (!json.contains(raw_bytes_log_key))
        json[raw_bytes_log_key] = false;
    if (!json.contains(network_recap_to_console_key))
        json[network_recap_to_console_key] = false;
    Json::Value packet_lists = {
        { ignored_clientbound_key, Json::Array() },
        { ignored_serverbound_key, Json::Array() },
        { detailed_clientbound_key, Json::Array() },
        { detailed_serverbound_key, Json::Array() },
    };
    if (!json.contains(packets_key))
        json[packets_key] = packet_lists;

    return json;
}

void Conf::SaveConf(const Json::Value& conf)
{
    Json::Value main_json = LoadConfFile();

    main_json[active_conf_key] = active_conf.value();
    main_json[active_conf.value()] = conf;

    std::ofstream file = std::ofstream(conf_path, std::ios::out);
    if (!file.is_open())
    {
        throw std::runtime_error("Error trying to open conf file at: " + conf_path);
    }

    file << main_json.Dump(4);
    file.close();
}

std::time_t Conf::GetModifiedTimestamp()
{
    struct stat result;
    if (stat(conf_path.c_str(), &result) == 0)
    {
        return result.st_mtime;
    }
    return -1;
}

std::set<std::string> Conf::GetConfList()
{
    Json::Value json = LoadConfFile();

    std::set<std::string> output;
    for (const auto& [k, v] : json.get_object())
    {
        if (k != active_conf_key)
        {
            output.insert(k);
        }
    }
    
    return output;
}

void Conf::DeleteConf(const std::string& name)
{
    Json::Value json = LoadConfFile();

    json.get_object().erase(name);

    std::ofstream file = std::ofstream(conf_path, std::ios::out);
    if (!file.is_open())
    {
        throw std::runtime_error("Error trying to open conf file at: " + conf_path);
    }

    file << json.Dump(4);
    file.close();
}

Json::Value Conf::LoadConfFile()
{
    if (conf_path.empty())
    {
        conf_path = "conf.json";
    }

    // Create file if it doesn't exist
    if (!std::filesystem::exists(conf_path))
    {
        std::ofstream outfile(conf_path, std::ios::out);
        outfile << Json::Value().Dump(4);
    }

    std::ifstream file = std::ifstream(conf_path, std::ios::in);
    if (!file.is_open())
    {
        throw std::runtime_error("Error trying to open conf file at: " + conf_path);
    }
    Json::Value json;
    file >> json;
    file.close();

    if (!json.is_object())
    {
        json = Json::Object();
    }

    // Convert old conf format
    if (!json.contains(active_conf_key))
    {
        json = {
            { active_conf_key, active_conf.value_or("default") },
            { active_conf.value_or("default"), json }
        };
    }

    if (!json[active_conf_key].is_string())
    {
        json[active_conf_key] = "default";
    }

    if (!active_conf.has_value())
    {
        active_conf = json[active_conf_key].get_string();
    }

    if (!json.contains(active_conf.value()))
    {
        json[active_conf.value()] = Json::Value();
    }

    return json;
}
