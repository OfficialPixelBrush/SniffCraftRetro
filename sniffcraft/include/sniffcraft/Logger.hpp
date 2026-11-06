#pragma once

#include "sniffcraft/enums.hpp"
#include "sniffcraft/Json/Json.hpp"
#include "sniffcraft/LogItem.hpp"
#include "sniffcraft/NetworkRecapItem.hpp"
#include "sniffcraft/Protocol.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string_view>
#include <thread>

class Logger
{
public:
    Logger();
#ifdef WITH_GUI
    Logger(const std::filesystem::path& path);
#endif
    ~Logger();
    /// @brief Log a packet
    /// @param packet Parsed packet
    /// @param origin Where the packet comes from
    /// @param bandwidth_bytes Size on the wire (0 if not part of the network data)
    /// @param raw The exact bytes of the packet on the wire
    void Log(const std::shared_ptr<Packet::BasePacket>& packet, const Endpoint origin, const size_t bandwidth_bytes, std::vector<unsigned char> raw);
    /// @brief Log a message from SniffCraft itself (e.g. a stream that could not be parsed)
    void LogNote(const std::string& note, const Endpoint origin);
    const std::string& GetBaseFilename() const;
    void LoadConfig();
    void Stop();
#ifdef WITH_GUI
    /// @brief Render this Logger packets
    /// @return A pair <packet id, origin> to add to ignored, if first element is -1, nothing to add
    std::pair<int, Endpoint> Render();
    /// @brief Will recreate packets_history_filtered_indices based on currently ignored packets and search string
    void UpdateFilteredPackets();
#endif

private:
    void LogConsume();
    void LoadPacketsFromJson(const Json::Value& value);
    std::string_view OriginToString(const Endpoint origin) const;
    /// @brief Get the displayable name of a packet
    std::string GetPacketName(const LogItem& item) const;
    Endpoint SimpleOrigin(const Endpoint origin) const;
    std::string GenerateNetworkRecap(const int max_entry = -1, const int max_name_size = -1) const;

private:
    std::chrono::time_point<std::chrono::system_clock> start_time;

    std::thread log_thread;
    std::mutex log_mutex;
    std::condition_variable log_condition;
    std::queue<LogItem> logging_queue;

    std::string base_filename;
    std::ofstream log_file;
    std::ofstream binary_file;
    std::atomic<bool> is_running;
    bool log_to_file;
    bool log_to_binary_file;
    bool log_to_console;
    bool log_raw_bytes;
    bool log_network_recap_console;
#ifdef WITH_GUI
    bool in_gui;
#endif

    std::time_t last_time_checked_conf_file;
    std::time_t last_time_conf_file_loaded;
    std::time_t last_time_network_recap_printed;

    /// Ignored/detailed packet ids, for Endpoint::Client (serverbound) and Endpoint::Server (clientbound)
    std::map<Endpoint, std::set<int> > ignored_packets;
    std::mutex ignored_packets_mutex;
    std::map<Endpoint, std::set<int> > detailed_packets;

    std::map<std::string, NetworkRecapItem> clientbound_network_recap_data;
    std::map<std::string, NetworkRecapItem> serverbound_network_recap_data;
    mutable std::mutex network_recap_mutex;
    NetworkRecapItem clientbound_total_network_recap;
    NetworkRecapItem serverbound_total_network_recap;

#ifdef WITH_GUI
    std::vector<LogItem> packets_history;
    std::vector<size_t> packets_history_filtered_indices;
    std::mutex packets_history_mutex;
    long long int selected_index = -1;
    std::vector<unsigned char> selected_bytes;
    Json::Value selected_json;
    bool count_per_s_clientbound = false;
    bool count_per_s_serverbound = false;
    bool bandwidth_per_s_clientbound = false;
    bool bandwidth_per_s_serverbound = false;
    std::mutex search_mutex;
    bool search_ignored_packets = false;
    std::string search_str;
    bool autoscroll = true;
#endif
};
