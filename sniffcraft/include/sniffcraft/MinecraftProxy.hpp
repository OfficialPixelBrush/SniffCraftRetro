#pragma once

#include "sniffcraft/BaseProxy.hpp"

#include <memory>

class Logger;

/// @brief Proxy for Minecraft Beta 1.7.3 connections. Every packet is parsed
/// (with the format defined in protocol/packets.h) and logged, then the original
/// bytes are forwarded unchanged.
class MinecraftProxy : public BaseProxy
{
public:
    MinecraftProxy(asio::io_context& io_context);
    virtual ~MinecraftProxy();

    virtual std::optional<std::string> Start(const std::string& server_address, const unsigned short server_port) override;

    std::shared_ptr<Logger> GetLogger() const;

protected:
    virtual size_t ProcessData(const std::vector<unsigned char>::const_iterator& data, const size_t length, const Endpoint source) override;

private:
    /// @brief Beta 1.7.3 packets have no length prefix, so once a packet can not be
    /// understood there is no way to know where the next one starts. When this happens,
    /// all the remaining data of this direction is forwarded without being parsed.
    /// @param source Direction of the stream that failed
    /// @param reason What went wrong
    /// @param data First unparsed byte
    /// @param length Number of unparsed bytes available
    void EnterRawForwarding(const Endpoint source, const std::string& reason, const std::vector<unsigned char>::const_iterator& data, const size_t length);

private:
    std::shared_ptr<Logger> logger;

    /// Set to true once a stream can not be parsed anymore, one per direction
    bool client_raw_forwarding;
    bool server_raw_forwarding;
};
