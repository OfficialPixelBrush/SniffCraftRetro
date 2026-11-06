#include <catch2/catch_test_macros.hpp>

#ifndef _WIN32

#include <asio.hpp>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

#include "packet_builder.hpp"

// These tests start the real sniffcraft executable (headless) between a fake
// client and a fake server, send Beta 1.7.3 traffic through it and check that
// every byte arrives unchanged on the other side, and what ends up in the logs.

namespace
{
    using namespace std::chrono_literals;
    namespace fs = std::filesystem;

    unsigned short FreePort()
    {
        asio::io_context io;
        asio::ip::tcp::acceptor acceptor(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
        return acceptor.local_endpoint().port();
    }

    std::string ReadFile(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    /// @brief Runs sniffcraft in a temporary directory with a given packets conf
    class ProxyProcess
    {
    public:
        ProxyProcess(const unsigned short server_port, const std::string& packets_conf = "{}")
        {
            static std::atomic<int> counter{ 0 };
            dir = fs::temp_directory_path() / ("sniffcraft_test_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
            fs::create_directories(dir);
            local_port = FreePort();

            std::ofstream conf(dir / "conf.json");
            conf << "{\"ActiveConf\": \"test\", \"test\": {"
                << "\"ServerAddress\": \"127.0.0.1:" << server_port << "\","
                << "\"LocalPort\": " << local_port << ","
                << "\"LogToTxtFile\": true,"
                << "\"LogToBinFile\": true,"
                << "\"LogToConsole\": false,"
                << "\"LogRawBytes\": false,"
                << "\"Packets\": " << packets_conf
                << "}}";
            conf.close();

            pid = fork();
            REQUIRE(pid >= 0);
            if (pid == 0)
            {
                if (chdir(dir.c_str()) != 0)
                {
                    _exit(127);
                }
                // Keep the test output readable
                if (freopen((dir / "stdout.txt").c_str(), "w", stdout) == nullptr || freopen((dir / "stderr.txt").c_str(), "w", stderr) == nullptr)
                {
                    _exit(127);
                }
                execl(SNIFFCRAFT_EXE, SNIFFCRAFT_EXE, "--headless", (dir / "conf.json").c_str(), static_cast<char*>(nullptr));
                _exit(127);
            }

            // Wait for the proxy to listen (it prints a line once the acceptor is ready)
            for (int i = 0; i < 100; ++i)
            {
                if (ReadFile(dir / "stdout.txt").find("Starting redirection") != std::string::npos)
                {
                    return;
                }
                std::this_thread::sleep_for(50ms);
            }
            FAIL("sniffcraft did not start listening");
        }

        ~ProxyProcess()
        {
            Stop();
            std::error_code ec;
            if (!keep_files)
            {
                fs::remove_all(dir, ec);
            }
        }

        void Stop()
        {
            if (pid > 0)
            {
                kill(pid, SIGTERM);
                int status;
                waitpid(pid, &status, 0);
                pid = -1;
            }
        }

        fs::path dir;
        unsigned short local_port;
        pid_t pid = -1;
        bool keep_files = false;
    };

    std::vector<unsigned char> Concat(const std::vector<std::vector<unsigned char>>& packets)
    {
        std::vector<unsigned char> output;
        for (const auto& p : packets)
        {
            output.insert(output.end(), p.begin(), p.end());
        }
        return output;
    }

    /// @brief Send data in small random pieces, with optional pauses, so packets get
    /// split in as many places as possible
    void SendInPieces(asio::ip::tcp::socket& socket, const std::vector<unsigned char>& data, const size_t max_piece, const bool pauses, const unsigned seed)
    {
        std::mt19937 rng(seed);
        size_t offset = 0;
        while (offset < data.size())
        {
            const size_t piece = std::min<size_t>(data.size() - offset, 1 + rng() % max_piece);
            asio::write(socket, asio::buffer(data.data() + offset, piece));
            offset += piece;
            if (pauses && rng() % 8 == 0)
            {
                std::this_thread::sleep_for(2ms);
            }
        }
    }

    /// @brief Read exactly size bytes (fails the test after a timeout)
    std::vector<unsigned char> ReadExactly(asio::ip::tcp::socket& socket, const size_t size)
    {
        std::vector<unsigned char> output(size);
        std::atomic<bool> done{ false };
        std::thread watchdog([&]() {
            for (int i = 0; i < 100 && !done; ++i)
            {
                std::this_thread::sleep_for(100ms);
            }
            if (!done)
            {
                asio::error_code ec;
                socket.close(ec);
            }
        });
        asio::error_code ec;
        const size_t read = asio::read(socket, asio::buffer(output), ec);
        done = true;
        watchdog.join();
        output.resize(read);
        return output;
    }

    std::vector<unsigned char> ClientToServerTraffic()
    {
        return Concat({
            B(0x02).s16("Steve").v,
            B(0x01).i32(14).s16("Steve").i64(0).i8(0).v,
            B(0x0A).b(true).v,
            B(0x0D).f64(1.5).f64(65.62).f64(64.0).f64(-3.25).f32(90.0f).f32(0.0f).b(true).v,
            B(0x03).s16("hello there").v,
            B(0x0F).i32(-5).i8(70).i32(9).i8(1).i16(4).i8(64).i16(0).v,
            B(0x0F).i32(-5).i8(70).i32(9).i8(1).i16(-1).v,
            B(0x66).i8(0).i16(36).i8(0).i16(1).b(false).i16(276).i8(1).i16(0).v,
            B(0x00).v,
            B(0x12).i32(1).i8(1).v,
            B(0xFF).s16("Quitting").v,
        });
    }

    std::vector<unsigned char> ServerToClientTraffic()
    {
        std::vector<unsigned char> chunk = B(0x33).i32(-16).i16(0).i32(32).i8(15).i8(127).i8(15).i32(30000).bytes(30000, 0x11).v;
        return Concat({
            B(0x02).s16("-").v,
            B(0x01).i32(1234).s16("").i64(987654321).i8(0).v,
            B(0x06).i32(0).i32(64).i32(0).v,
            B(0x04).i64(6000).v,
            B(0x32).i32(-1).i32(2).b(true).v,
            chunk,
            B(0x34).i32(1).i32(-1).i16(3).i16(0x1234).i16(0x2345).i16(0x3456).i8(1).i8(2).i8(3).i8(0x10).i8(0x20).i8(0x30).v,
            B(0x18).i32(11).i8(90).i32(1).i32(2).i32(3).i8(4).i8(5).i8(0x00).i8(1).i8(0x7F).v,
            B(0x3C).f64(1.0).f64(64.0).f64(-1.0).f32(4.0f).i32(2).i8(1).i8(2).i8(3).i8(-1).i8(-2).i8(-3).v,
            B(0x68).i8(0).i16(3).i16(276).i8(1).i16(10).i16(-1).i16(4).i8(64).i16(0).v,
            B(0x00).v,
            B(0x03).s16("<Steve> hello there").v,
            B(0xFF).s16("bye").v,
        });
    }

    struct Session
    {
        std::vector<unsigned char> server_received;
        std::vector<unsigned char> client_received;
    };

    /// @brief Plays a full session through the proxy: each side sends its data
    /// while the other side reads everything it should receive
    Session RunSession(
        asio::io_context& io,
        asio::ip::tcp::acceptor& server_acceptor,
        const ProxyProcess& proxy,
        const std::vector<unsigned char>& client_data,
        const std::vector<unsigned char>& server_data,
        const size_t max_piece,
        const bool pauses
    )
    {
        asio::ip::tcp::socket client(io);
        client.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), proxy.local_port));

        asio::ip::tcp::socket server(io);
        server_acceptor.accept(server);

        Session session;
        std::thread server_thread([&]() {
            SendInPieces(server, server_data, max_piece, pauses, 1);
            session.server_received = ReadExactly(server, client_data.size());
        });
        std::thread client_thread([&]() {
            SendInPieces(client, client_data, max_piece, pauses, 2);
            session.client_received = ReadExactly(client, server_data.size());
        });
        server_thread.join();
        client_thread.join();

        asio::error_code ec;
        client.close(ec);
        server.close(ec);
        return session;
    }
}

TEST_CASE("Traffic goes through the proxy unchanged, however it is split")
{
    const std::vector<unsigned char> client_data = ClientToServerTraffic();
    const std::vector<unsigned char> server_data = ServerToClientTraffic();

    struct Mode { size_t max_piece; bool pauses; };
    for (const Mode mode : { Mode{ 1, true }, Mode{ 7, true }, Mode{ 300, false }, Mode{ 100000, false } })
    {
        DYNAMIC_SECTION("pieces up to " << mode.max_piece << " bytes" << (mode.pauses ? " with pauses" : ""))
        {
            asio::io_context io;
            asio::ip::tcp::acceptor server_acceptor(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
            ProxyProcess proxy(server_acceptor.local_endpoint().port());

            const Session session = RunSession(io, server_acceptor, proxy, client_data, server_data, mode.max_piece, mode.pauses);
            CHECK(session.server_received == client_data);
            CHECK(session.client_received == server_data);
        }
    }
}

TEST_CASE("Packets are logged with their direction, name and configured details")
{
    asio::io_context io;
    asio::ip::tcp::acceptor server_acceptor(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    ProxyProcess proxy(
        server_acceptor.local_endpoint().port(),
        // Ignore keep alives and chunk data, by name and by id, details for Login
        R"({
            "ignored_clientbound": ["ChunkData", 0],
            "ignored_serverbound": [0],
            "detailed_clientbound": ["Login"],
            "detailed_serverbound": ["Login", "Explosion"]
        })"
    );

    const std::vector<unsigned char> client_data = ClientToServerTraffic();
    const std::vector<unsigned char> server_data = ServerToClientTraffic();
    const Session session = RunSession(io, server_acceptor, proxy, client_data, server_data, 50, false);
    REQUIRE(session.server_received == client_data);
    REQUIRE(session.client_received == server_data);

    // The proxy closes its logger when the connection is cleaned up (checked every second)
    std::this_thread::sleep_for(2500ms);
    proxy.Stop();

    const fs::path dir = proxy.dir;
    std::string text;
    fs::path binary;
    for (const auto& entry : fs::directory_iterator(dir))
    {
        const std::string name = entry.path().filename().string();
        if (name.size() > 11 && name.substr(name.size() - 11) == "_sclogs.txt")
        {
            text = ReadFile(entry.path());
        }
        else if (entry.path().extension() == ".scbin")
        {
            binary = entry.path();
        }
    }
    REQUIRE(!text.empty());

    INFO(text);
    CHECK(text.find("[C --> S] PreLogin") != std::string::npos);
    CHECK(text.find("[C --> S] Login") != std::string::npos);
    CHECK(text.find("[S --> C] Login") != std::string::npos);
    CHECK(text.find("[C --> S] PlayerPositionAndRotation") != std::string::npos);
    CHECK(text.find("[S --> C] SetTime") != std::string::npos);
    CHECK(text.find("[S --> C] SetMultipleBlocks") != std::string::npos);
    CHECK(text.find("[S --> C] SpawnMob") != std::string::npos);
    CHECK(text.find("[S --> C] Explosion") != std::string::npos);
    CHECK(text.find("[S --> C] FillContainer") != std::string::npos);
    CHECK(text.find("[S --> C] ChatMessage") != std::string::npos);
    CHECK(text.find("[S --> C] Disconnect") != std::string::npos);
    CHECK(text.find("[C --> S] PlaceBlock") != std::string::npos);
    CHECK(text.find("[C --> S] ClickSlot") != std::string::npos);

    // Ignored packets are not logged (they still count in the network recap at the end)
    const std::string log_lines = text.substr(0, text.find("Sorted by count:"));
    CHECK(log_lines.find("ChunkData") == std::string::npos);
    CHECK(log_lines.find("KeepAlive") == std::string::npos);
    CHECK(text.substr(text.find("Sorted by count:")).find("ChunkData") != std::string::npos);

    // Details only where asked
    CHECK(text.find("\"protocolVersion\": 14") != std::string::npos);
    CHECK(text.find("\"username\": \"Steve\"") != std::string::npos);
    CHECK(text.find("\"entityId\": 1234") != std::string::npos);
    CHECK(text.find("\"hello there\"") == std::string::npos);

    // Network recap at the end of the file
    CHECK(text.find("Sorted by count:") != std::string::npos);
    CHECK(text.find("Sorted by bandwidth:") != std::string::npos);

    // Binary capture: header + all packets, even the ones that were ignored in the text log
    REQUIRE(!binary.empty());
    const std::string bin = ReadFile(binary);
    REQUIRE(bin.size() > 16);
    CHECK(bin.substr(0, 4) == "SCRT");
    CHECK(static_cast<unsigned char>(bin[7]) == 14);
    CHECK(bin.size() > client_data.size() + server_data.size());
}

TEST_CASE("Data that can not be parsed is still forwarded unchanged")
{
    asio::io_context io;
    asio::ip::tcp::acceptor server_acceptor(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    ProxyProcess proxy(server_acceptor.local_endpoint().port());

    // 0xFE (server list ping) is not part of the packets of the game,
    // everything after it can not be understood
    std::mt19937 rng(42);
    std::vector<unsigned char> noise(5000);
    for (auto& b : noise)
    {
        b = static_cast<unsigned char>(rng());
    }
    const std::vector<unsigned char> client_data = Concat({
        B(0x02).s16("Steve").v,
        B(0x01).i32(14).s16("Steve").i64(0).i8(0).v,
        { 0xFE, 0x01 },
        noise,
        B(0x00).v,
    });
    const std::vector<unsigned char> server_data = ServerToClientTraffic();

    const Session session = RunSession(io, server_acceptor, proxy, client_data, server_data, 64, false);
    CHECK(session.server_received == client_data);
    // The other direction is not affected
    CHECK(session.client_received == server_data);

    std::this_thread::sleep_for(2500ms);
    proxy.Stop();

    std::string text;
    for (const auto& entry : fs::directory_iterator(proxy.dir))
    {
        const std::string name = entry.path().filename().string();
        if (name.size() > 11 && name.substr(name.size() - 11) == "_sclogs.txt")
        {
            text = ReadFile(entry.path());
        }
    }
    INFO(text);
    CHECK(text.find("[C --> S] PreLogin") != std::string::npos);
    CHECK(text.find("[C --> S] Login") != std::string::npos);
    CHECK(text.find("unknown packet id 254") != std::string::npos);
    CHECK(text.find("will be transferred but not logged anymore") != std::string::npos);
    CHECK(text.find("0xFE 0x01") != std::string::npos);
    CHECK(text.find("[S --> C] Disconnect") != std::string::npos);
}

TEST_CASE("A client that is not using protocol version 14 is reported")
{
    asio::io_context io;
    asio::ip::tcp::acceptor server_acceptor(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    ProxyProcess proxy(server_acceptor.local_endpoint().port());

    const std::vector<unsigned char> client_data = Concat({
        B(0x02).s16("Steve").v,
        B(0x01).i32(17).s16("Steve").i64(0).i8(0).v,
    });
    const std::vector<unsigned char> server_data = B(0xFF).s16("Outdated client!").v;

    const Session session = RunSession(io, server_acceptor, proxy, client_data, server_data, 1000, false);
    CHECK(session.server_received == client_data);
    CHECK(session.client_received == server_data);

    std::this_thread::sleep_for(2500ms);
    proxy.Stop();

    std::string text;
    for (const auto& entry : fs::directory_iterator(proxy.dir))
    {
        const std::string name = entry.path().filename().string();
        if (name.size() > 11 && name.substr(name.size() - 11) == "_sclogs.txt")
        {
            text = ReadFile(entry.path());
        }
    }
    INFO(text);
    CHECK(text.find("the client protocol version (17) is not the one of Beta 1.7.3 (14)") != std::string::npos);
}

#else

TEST_CASE("Proxy tests are not available on Windows")
{
    SUCCEED();
}

#endif
