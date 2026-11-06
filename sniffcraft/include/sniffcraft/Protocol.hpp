#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/network_stream.h"
#include "protocol/packets.h"

#include "sniffcraft/Json/Json.hpp"

/// @brief Everything SniffCraft needs to know about the Minecraft Beta 1.7.3 protocol.
/// The wire format itself lives in protocol/packets.h, this is the glue around it:
/// a packet factory, a streaming parser, packet names and a JSON representation.
///
/// Unlike modern versions, Beta 1.7.3 packets have no length prefix, no compression,
/// no encryption and no connection states: a packet is one id byte followed by its
/// fields, and the only way to know where it ends is to parse it. This is why a
/// stream can not be resynchronized once a packet could not be understood.
namespace Protocol
{
    /// @brief Protocol version sent by Beta 1.7.3 clients in the Login packet
    constexpr int PROTOCOL_VERSION = 14;

    enum class ParseStatus
    {
        /// A full packet was parsed
        Ok,
        /// The buffer ends in the middle of a packet, retry with more data
        NeedMoreData,
        /// The first byte is not a Beta 1.7.3 packet id
        UnknownPacket,
        /// The id is known but the content is invalid
        Malformed,
    };

    struct ParseResult
    {
        ParseStatus status = ParseStatus::NeedMoreData;
        /// Number of bytes (id included) of the packet, only valid if status == Ok
        size_t consumed = 0;
        /// Parsed packet, only valid if status == Ok
        std::shared_ptr<Packet::BasePacket> packet;
        /// First byte of the buffer (the packet id), if the buffer was not empty
        int id = -1;
        /// Description of the problem if status is UnknownPacket or Malformed
        std::string error;
    };

    /// @brief Try to parse one packet at the start of a buffer
    /// @param data Start of the buffer
    /// @param size Number of available bytes
    /// @param trace If not null, will be filled with the byte range consumed by each
    /// field read (offsets relative to data, the id byte is not part of any range)
    ParseResult ParsePacket(const unsigned char* data, size_t size, std::vector<NetworkStream::Span>* trace = nullptr);

    /// @brief Name of a packet, as used in the config files and the logs
    /// @return The name, or an empty view if id is not a known packet id
    std::string_view PacketName(const int id);

    /// @brief Reverse of PacketName
    /// @return The id, or -1 if the name is unknown
    int PacketIdFromName(const std::string_view& name);

    struct PacketInfo
    {
        int id;
        std::string_view name;
    };

    /// @brief All known packets, ordered by id
    const std::vector<PacketInfo>& AllPackets();

    /// @brief Create an empty packet for a given id
    /// @return nullptr if id is unknown
    std::shared_ptr<Packet::BasePacket> CreatePacket(const int id);

    /// @brief Convert a parsed packet to json
    /// @param packet Packet to convert
    /// @param clientbound true if the packet was sent by the server, some packets
    /// have a different meaning depending on the direction (e.g. Login)
    /// @param spans If not null, byte ranges as filled by ParsePacket. Every value
    /// is then wrapped in {"start_offset", "end_offset", "content"} where offsets
    /// are the number of bytes remaining in the packet before and after the value
    /// (this is the format the GUI uses to link values and bytes)
    /// @param total_size Size in bytes of the whole packet, only used with spans
    /// @param spans_used If not null, will be set to the number of spans used, which
    /// should always be spans->size() when spans is given
    Json::Value PacketToJson(
        const Packet::BasePacket& packet,
        const bool clientbound,
        const std::vector<NetworkStream::Span>* spans = nullptr,
        const size_t total_size = 0,
        size_t* spans_used = nullptr
    );
}
