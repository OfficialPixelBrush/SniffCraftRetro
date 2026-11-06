#include <iomanip>
#include <iostream>
#include <sstream>

#include "sniffcraft/Logger.hpp"
#include "sniffcraft/MinecraftProxy.hpp"
#include "sniffcraft/Protocol.hpp"

MinecraftProxy::MinecraftProxy(asio::io_context& io_context) : BaseProxy(io_context)
{
    client_raw_forwarding = false;
    server_raw_forwarding = false;
}

MinecraftProxy::~MinecraftProxy()
{
    if (logger != nullptr)
    {
        logger->Stop();
    }
}

std::optional<std::string> MinecraftProxy::Start(const std::string& server_address, const unsigned short server_port)
{
    logger = std::make_shared<Logger>();

    return BaseProxy::Start(server_address, server_port);
}

std::shared_ptr<Logger> MinecraftProxy::GetLogger() const
{
    return logger;
}

size_t MinecraftProxy::ProcessData(const std::vector<unsigned char>::const_iterator& data, const size_t length, const Endpoint source)
{
    Connection& dst_connection = source == Endpoint::Server ? client_connection : server_connection;
    bool& raw_forwarding = source == Endpoint::Server ? server_raw_forwarding : client_raw_forwarding;

    // We can't parse this stream anymore, just transfer the bytes
    if (raw_forwarding)
    {
        dst_connection.WriteData(&(*data), length);
        return length;
    }

    const Protocol::ParseResult result = Protocol::ParsePacket(&(*data), length);

    switch (result.status)
    {
    case Protocol::ParseStatus::NeedMoreData:
        // Nothing is transferred until the packet is complete
        return 0;
    case Protocol::ParseStatus::UnknownPacket:
    case Protocol::ParseStatus::Malformed:
        EnterRawForwarding(source, result.error, data, length);
        dst_connection.WriteData(&(*data), length);
        return length;
    case Protocol::ParseStatus::Ok:
        break;
    }

    // The client says which version it is using in its Login packet
    if (source == Endpoint::Client && result.packet->id == PacketId::Login)
    {
        const int client_version = static_cast<const Packet::Login&>(*result.packet).entityIdProtocolVersion;
        if (client_version != Protocol::PROTOCOL_VERSION)
        {
            logger->LogNote(
                "WARNING, the client protocol version (" + std::to_string(client_version) +
                ") is not the one of Beta 1.7.3 (" + std::to_string(Protocol::PROTOCOL_VERSION) +
                "). Logged packet details may be wrong",
                Endpoint::Client
            );
        }
    }

    logger->Log(result.packet, source, result.consumed, std::vector<unsigned char>(data, data + result.consumed));

    // Transfer the data as they came
    dst_connection.WriteData(&(*data), result.consumed);

    return result.consumed;
}

void MinecraftProxy::EnterRawForwarding(const Endpoint source, const std::string& reason, const std::vector<unsigned char>::const_iterator& data, const size_t length)
{
    (source == Endpoint::Server ? server_raw_forwarding : client_raw_forwarding) = true;

    std::stringstream message;
    message << "Can't parse the data (" << reason << "). The rest of the "
        << (source == Endpoint::Server ? "server --> client" : "client --> server")
        << " data will be transferred but not logged anymore, as Beta 1.7.3 packets have no length"
        << " and the stream can't be resynchronized. Next bytes:";
    for (size_t i = 0; i < std::min<size_t>(length, 16); ++i)
    {
        message << " 0x" << std::setw(2) << std::setfill('0') << std::uppercase << std::hex << static_cast<int>(*(data + i));
    }
    std::cerr << message.str() << std::endl;
    logger->LogNote(message.str(), source);
}
