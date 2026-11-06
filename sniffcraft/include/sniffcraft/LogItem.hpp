#pragma once

#include "sniffcraft/enums.hpp"
#include "sniffcraft/Protocol.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

struct LogItem
{
    /// Parsed packet. nullptr for notes (messages from SniffCraft itself)
    std::shared_ptr<Packet::BasePacket> packet;
    std::chrono::time_point<std::chrono::system_clock> date;
    Endpoint origin;
    /// Size of the packet on the wire, 0 for packets that are not really part of the network data
    size_t bandwidth_bytes;
    /// The exact bytes of the packet as they were on the wire (id included)
    std::vector<unsigned char> raw;
    /// Message, only set for notes
    std::string note;

    /// Packet id, or -1 for notes
    int PacketId() const
    {
        return packet == nullptr ? -1 : static_cast<int>(packet->id);
    }
};
