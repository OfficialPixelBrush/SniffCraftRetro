#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "packet_builder.hpp"
#include "sniffcraft/Protocol.hpp"

// Packets are built byte by byte (see packet_builder.hpp) from the Beta 1.7.3
// protocol description, without using anything from protocol/ (not even
// NetworkStream), so these tests check packets.h against the actual wire format
// and not against itself.
namespace
{
    struct Case
    {
        std::string label;
        int id;
        std::vector<unsigned char> bytes;
        bool roundtrip = true;
    };

    std::vector<Case> Cases()
    {
        std::vector<Case> c;
        auto add = [&](const std::string& label, const B& b, const bool roundtrip = true) {
            c.push_back({ label, b.v[0], b.v, roundtrip });
        };

        add("KeepAlive", B(0x00));
        add("Login c2s", B(0x01).i32(14).s16("Steve").i64(0).i8(0));
        add("Login s2c", B(0x01).i32(1234).s16("").i64(-8812937129378123LL).i8(-1));
        add("PreLogin", B(0x02).s16("Steve"));
        add("PreLogin unicode", B(0x02).s16(std::vector<int>{ 'h', 0x00E9, 0x00A7, 0x4E2D, 0xD83D, 0xDE00 }));
        add("ChatMessage", B(0x03).s16("hello world"));
        add("SetTime", B(0x04).i64(6000));
        add("SetEquipment", B(0x05).i32(5).i16(0).i16(276).i16(3));
        add("SetSpawnPosition", B(0x06).i32(-12).i32(64).i32(300));
        add("InteractWithEntity", B(0x07).i32(1).i32(2).b(true));
        add("SetHealth", B(0x08).i16(20));
        add("Respawn", B(0x09).i8(-1));
        add("PlayerMovement", B(0x0A).b(true));
        add("PlayerPosition", B(0x0B).f64(1.5).f64(64.0).f64(65.62).f64(-3.25).b(true));
        add("PlayerRotation", B(0x0C).f32(90.0f).f32(-12.5f).b(false));
        add("PlayerPositionAndRotation", B(0x0D).f64(1.5).f64(65.62).f64(64.0).f64(-3.25).f32(90.0f).f32(-12.5f).b(true));
        add("MineBlock", B(0x0E).i8(2).i32(-5).i8(70).i32(9).i8(1));
        add("PlaceBlock with item", B(0x0F).i32(-5).i8(70).i32(9).i8(1).i16(4).i8(64).i16(0));
        add("PlaceBlock without item", B(0x0F).i32(-5).i8(70).i32(9).i8(1).i16(-1), false);
        add("SetHotbarSlot", B(0x10).i16(4));
        add("InteractWithBlock (use bed)", B(0x11).i32(7).i8(0).i32(10).i8(64).i32(-10));
        add("Animation", B(0x12).i32(7).i8(1));
        add("PlayerAction", B(0x13).i32(7).i8(1));
        add("SpawnPlayer", B(0x14).i32(8).s16("Alex").i32(320).i32(2048).i32(-64).i8(10).i8(-5).i16(276));
        add("SpawnItem", B(0x15).i32(9).i16(5).i8(3).i16(0).i32(1).i32(2).i32(3).i8(4).i8(5).i8(6));
        add("CollectItem", B(0x16).i32(9).i32(8));
        add("SpawnObject no owner", B(0x17).i32(10).i8(60).i32(1).i32(2).i32(3).i32(0));
        add("SpawnObject with owner", B(0x17).i32(10).i8(60).i32(1).i32(2).i32(3).i32(8).i16(100).i16(-100).i16(0));
        add("SpawnMob", B(0x18).i32(11).i8(90).i32(1).i32(2).i32(3).i8(4).i8(5)
            .i8(0x00).i8(1)              // byte, index 0
            .i8(0x20 | 1).i16(300)       // short, index 1
            .i8(0x7F));
        add("SpawnMob empty metadata", B(0x18).i32(11).i8(90).i32(1).i32(2).i32(3).i8(4).i8(5).i8(0x7F));
        add("SpawnPainting", B(0x19).i32(12).s16("Kebab").i32(1).i32(2).i32(3).i32(2));
        add("PlayerInput", B(0x1B).f32(0.5f).f32(1.0f).f32(10.0f).f32(20.0f).b(true).b(false));
        add("EntityVelocity", B(0x1C).i32(7).i16(1).i16(-2).i16(3));
        add("DespawnEntity", B(0x1D).i32(7));
        add("EntityMovement", B(0x1E).i32(7));
        add("EntityPosition", B(0x1F).i32(7).i8(1).i8(-2).i8(3));
        add("EntityRotation", B(0x20).i32(7).i8(10).i8(-10));
        add("EntityPositionAndRotation", B(0x21).i32(7).i8(1).i8(-2).i8(3).i8(10).i8(-10));
        add("TeleportEntity", B(0x22).i32(7).i32(100).i32(2000).i32(-300).i8(10).i8(-10));
        add("EntityEvent", B(0x26).i32(7).i8(2));
        add("AddPassenger", B(0x27).i32(7).i32(8));
        add("EntityMetadata all types", B(0x28).i32(7)
            .i8(0x00).i8(1)                                             // byte
            .i8(0x20 | 1).i16(300)                                      // short
            .i8(0x40 | 2).i32(123456)                                   // int
            .i8(0x60 | 3).f32(1.5f)                                     // float
            .i8(0x80 | 4).s16("name")                                   // string
            .i8(0xA0 | 5).i16(276).i8(1).i16(10)                        // slot
            .i8(0xC0 | 6).i32(1).i32(2).i32(3)                          // position
            .i8(0x7F));
        add("SetChunkVisibility", B(0x32).i32(-3).i32(4).b(true));
        add("ChunkData", B(0x33).i32(-48).i16(0).i32(64).i8(15).i8(127).i8(15).i32(20).bytes(20, 0x78));
        add("ChunkData empty", B(0x33).i32(0).i16(0).i32(0).i8(0).i8(0).i8(0).i32(0));
        add("SetMultipleBlocks", B(0x34).i32(1).i32(-1).i16(3)
            .i16(0x1234).i16(0x2345).i16(0x3456)
            .i8(1).i8(2).i8(3)
            .i8(0x10).i8(0x20).i8(0x30));
        add("SetMultipleBlocks empty", B(0x34).i32(1).i32(-1).i16(0));
        add("SetBlock", B(0x35).i32(-5).i8(70).i32(9).i8(4).i8(2));
        add("BlockEvent", B(0x36).i32(-5).i16(70).i32(9).i8(1).i8(12));
        add("Explosion", B(0x3C).f64(1.0).f64(64.0).f64(-1.0).f32(4.0f).i32(2).i8(1).i8(2).i8(3).i8(-1).i8(-2).i8(-3));
        add("Explosion no blocks", B(0x3C).f64(1.0).f64(64.0).f64(-1.0).f32(4.0f).i32(0));
        add("WorldEvent", B(0x3D).i32(1003).i32(10).i8(64).i32(-10).i32(0));
        add("GameEvent", B(0x46).i8(1));
        add("LightningBolt", B(0x47).i32(99).i8(1).i32(1).i32(2).i32(3));
        add("OpenContainer", B(0x64).i8(1).i8(0).s8("Chest").i8(27));
        add("CloseContainer", B(0x65).i8(1));
        add("ClickSlot with item", B(0x66).i8(1).i16(5).i8(0).i16(7).b(false).i16(276).i8(1).i16(0));
        add("ClickSlot without item", B(0x66).i8(1).i16(5).i8(1).i16(8).b(true).i16(-1));
        add("SetSlot with item", B(0x67).i8(0).i16(36).i16(276).i8(1).i16(10));
        add("SetSlot without item", B(0x67).i8(0).i16(36).i16(-1));
        add("FillContainer", B(0x68).i8(0).i16(3).i16(276).i8(1).i16(10).i16(-1).i16(4).i8(64).i16(0));
        add("FillContainer empty", B(0x68).i8(0).i16(0));
        add("ContainerData", B(0x69).i8(2).i16(0).i16(100));
        add("ContainerTransaction", B(0x6A).i8(0).i16(7).b(true));
        add("UpdateSign", B(0x82).i32(-5).i16(70).i32(9).s16("line 1").s16("").s16("line 3").s16("l4"));
        add("ItemData", B(0x83).i16(358).i16(0).i8(5).bytes(5, 9));
        add("ItemData empty", B(0x83).i16(358).i16(0).i8(0));
        add("IncrementStatistic", B(0xC8).i32(1000).i8(1));
        add("Disconnect", B(0xFF).s16("Server closed"));
        return c;
    }
}

TEST_CASE("Every packet id is covered by at least one test case")
{
    std::set<int> covered;
    for (const auto& c : Cases())
    {
        covered.insert(c.id);
    }
    for (const auto& info : Protocol::AllPackets())
    {
        INFO(info.name);
        CHECK(covered.count(info.id) == 1);
    }
    CHECK(Protocol::AllPackets().size() == 57);
}

TEST_CASE("Names and ids are consistent")
{
    for (const auto& info : Protocol::AllPackets())
    {
        CHECK(Protocol::PacketName(info.id) == info.name);
        CHECK(Protocol::PacketIdFromName(info.name) == info.id);
        CHECK(Protocol::CreatePacket(info.id) != nullptr);
        CHECK(static_cast<int>(Protocol::CreatePacket(info.id)->id) == info.id);
    }
    CHECK(Protocol::PacketName(0x1A).empty());
    CHECK(Protocol::PacketIdFromName("NotAPacket") == -1);
    CHECK(Protocol::PacketName(0x01) == "Login");
    CHECK(Protocol::PacketName(0x33) == "ChunkData");
}

TEST_CASE("Full packets are parsed and consume exactly their bytes")
{
    for (const auto& c : Cases())
    {
        INFO(c.label);
        // Add trailing bytes that must not be consumed
        std::vector<unsigned char> data = c.bytes;
        data.push_back(0x00);
        data.push_back(0xFF);

        const Protocol::ParseResult r = Protocol::ParsePacket(data.data(), data.size());
        REQUIRE(r.status == Protocol::ParseStatus::Ok);
        CHECK(r.consumed == c.bytes.size());
        CHECK(r.id == c.id);
        REQUIRE(r.packet != nullptr);
        CHECK(static_cast<int>(r.packet->id) == c.id);
    }
}

TEST_CASE("Every proper prefix of a packet asks for more data")
{
    for (const auto& c : Cases())
    {
        for (size_t len = 0; len < c.bytes.size(); ++len)
        {
            INFO(c.label << " truncated to " << len << " bytes");
            const Protocol::ParseResult r = Protocol::ParsePacket(c.bytes.data(), len);
            CHECK(r.status == Protocol::ParseStatus::NeedMoreData);
        }
    }
}

TEST_CASE("Packets survive a Serialize round trip")
{
    for (const auto& c : Cases())
    {
        if (!c.roundtrip)
        {
            continue;
        }
        INFO(c.label);
        const Protocol::ParseResult r = Protocol::ParsePacket(c.bytes.data(), c.bytes.size());
        REQUIRE(r.status == Protocol::ParseStatus::Ok);
        NetworkStream out;
        r.packet->Serialize(out);
        CHECK(out.Buffer() == c.bytes);
    }
}

TEST_CASE("Traced byte ranges are contiguous and fully used by the json")
{
    for (const auto& c : Cases())
    {
        for (const bool clientbound : { false, true })
        {
            INFO(c.label << (clientbound ? " (clientbound)" : " (serverbound)"));
            std::vector<NetworkStream::Span> trace;
            const Protocol::ParseResult r = Protocol::ParsePacket(c.bytes.data(), c.bytes.size(), &trace);
            REQUIRE(r.status == Protocol::ParseStatus::Ok);

            // Ranges tile the packet, starting right after the id byte
            size_t pos = 1;
            for (const auto& span : trace)
            {
                CHECK(span.begin == pos);
                CHECK(span.end >= span.begin);
                pos = span.end;
            }
            CHECK(pos == c.bytes.size());

            size_t used = 0;
            const Json::Value detailed = Protocol::PacketToJson(*r.packet, clientbound, &trace, c.bytes.size(), &used);
            CHECK(used == trace.size());
            CHECK(detailed.is_object());

            // Without spans, nothing is wrapped
            const Json::Value plain = Protocol::PacketToJson(*r.packet, clientbound);
            CHECK(plain.is_object());
            CHECK(plain.Dump().find("start_offset") == std::string::npos);
        }
    }
}

TEST_CASE("Json content")
{
    SECTION("Login depends on the direction")
    {
        const std::vector<unsigned char> bytes = B(0x01).i32(14).s16("Steve").i64(42).i8(-1).v;
        const auto r = Protocol::ParsePacket(bytes.data(), bytes.size());
        REQUIRE(r.status == Protocol::ParseStatus::Ok);

        const Json::Value serverbound = Protocol::PacketToJson(*r.packet, false);
        CHECK(serverbound.contains("protocolVersion"));
        CHECK(!serverbound.contains("entityId"));
        CHECK(serverbound["protocolVersion"].get<int>() == 14);
        CHECK(serverbound["username"].get_string() == "Steve");
        CHECK(serverbound["worldSeed"].get<long long int>() == 42);
        CHECK(serverbound["dimension"].get<int>() == -1);

        const Json::Value clientbound = Protocol::PacketToJson(*r.packet, true);
        CHECK(clientbound.contains("entityId"));
        CHECK(!clientbound.contains("protocolVersion"));
    }

    SECTION("Strings are converted to UTF-8")
    {
        const std::vector<unsigned char> bytes = B(0x03).s16(std::vector<int>{ 'h', 0x00E9, 0x4E2D, 0xD83D, 0xDE00 }).v;
        const auto r = Protocol::ParsePacket(bytes.data(), bytes.size());
        REQUIRE(r.status == Protocol::ParseStatus::Ok);
        CHECK(Protocol::PacketToJson(*r.packet, false)["message"].get_string() == "h\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80");
    }

    SECTION("Lone surrogates do not produce invalid UTF-8")
    {
        const std::vector<unsigned char> bytes = B(0x03).s16(std::vector<int>{ 'a', 0xD83D, 'b' }).v;
        const auto r = Protocol::ParsePacket(bytes.data(), bytes.size());
        REQUIRE(r.status == Protocol::ParseStatus::Ok);
        CHECK(Protocol::PacketToJson(*r.packet, false)["message"].get_string() == "a\xEF\xBF\xBD" "b");
    }

    SECTION("Player position y and eye height are flipped for clientbound packets")
    {
        // x, second double, third double, z
        const std::vector<unsigned char> bytes = B(0x0D).f64(1.0).f64(65.62).f64(64.0).f64(3.0).f32(0).f32(0).b(true).v;
        const auto r = Protocol::ParsePacket(bytes.data(), bytes.size());
        REQUIRE(r.status == Protocol::ParseStatus::Ok);

        const Json::Value clientbound = Protocol::PacketToJson(*r.packet, true);
        CHECK(clientbound["position"]["y"].get<double>() == 64.0);
        CHECK(clientbound["cameraY"].get<double>() == 65.62);

        const Json::Value serverbound = Protocol::PacketToJson(*r.packet, false);
        CHECK(serverbound["position"]["y"].get<double>() == 65.62);
        CHECK(serverbound["cameraY"].get<double>() == 64.0);
    }

    SECTION("Explosion records are 3 bytes each")
    {
        const std::vector<unsigned char> bytes = B(0x3C).f64(1).f64(2).f64(3).f32(4).i32(2).i8(1).i8(2).i8(3).i8(-1).i8(-2).i8(-3).v;
        const auto r = Protocol::ParsePacket(bytes.data(), bytes.size());
        REQUIRE(r.status == Protocol::ParseStatus::Ok);
        CHECK(r.consumed == bytes.size());
        const Json::Value j = Protocol::PacketToJson(*r.packet, true);
        REQUIRE(j["destroyedBlocks"].size() == 2);
        CHECK(j["destroyedBlocks"][0]["x"].get<int>() == 1);
        CHECK(j["destroyedBlocks"][1]["z"].get<int>() == -3);
    }

    SECTION("Detailed json wraps values with their remaining bytes")
    {
        // id(1) + int(4) + int(4) + bool(1) = 10 bytes
        const std::vector<unsigned char> bytes = B(0x07).i32(1).i32(2).b(true).v;
        std::vector<NetworkStream::Span> trace;
        const auto r = Protocol::ParsePacket(bytes.data(), bytes.size(), &trace);
        REQUIRE(r.status == Protocol::ParseStatus::Ok);
        const Json::Value j = Protocol::PacketToJson(*r.packet, false, &trace, bytes.size());
        CHECK(j["sourceEntityId"]["start_offset"].get<int>() == 9);
        CHECK(j["sourceEntityId"]["end_offset"].get<int>() == 5);
        CHECK(j["targetEntityId"]["start_offset"].get<int>() == 5);
        CHECK(j["targetEntityId"]["end_offset"].get<int>() == 1);
        CHECK(j["attack"]["start_offset"].get<int>() == 1);
        CHECK(j["attack"]["end_offset"].get<int>() == 0);
        CHECK(j["attack"]["content"].get<bool>() == true);
    }
}

TEST_CASE("Invalid data is reported, never mistaken for a short read")
{
    SECTION("Unknown packet ids")
    {
        for (const int id : { 0x1A, 0x23, 0x6B, 0x80, 0xFE })
        {
            const unsigned char data[] = { static_cast<unsigned char>(id), 0, 0, 0, 0 };
            const auto r = Protocol::ParsePacket(data, sizeof(data));
            INFO(id);
            CHECK(r.status == Protocol::ParseStatus::UnknownPacket);
            CHECK(r.id == id);
        }
    }

    SECTION("Negative string length")
    {
        const std::vector<unsigned char> bytes = B(0x03).i16(-1).bytes(8).v;
        CHECK(Protocol::ParsePacket(bytes.data(), bytes.size()).status == Protocol::ParseStatus::Malformed);
    }

    SECTION("Unknown entity metadata type")
    {
        // type 7 does not exist
        const std::vector<unsigned char> bytes = B(0x28).i32(1).i8(0xE0).bytes(8).v;
        CHECK(Protocol::ParsePacket(bytes.data(), bytes.size()).status == Protocol::ParseStatus::Malformed);
    }

    SECTION("Negative array lengths")
    {
        CHECK(Protocol::ParsePacket(B(0x33).i32(0).i16(0).i32(0).i8(1).i8(1).i8(1).i32(-1).bytes(8).v.data(), 20).status != Protocol::ParseStatus::Ok);
        const std::vector<unsigned char> multi = B(0x34).i32(0).i32(0).i16(-5).bytes(16).v;
        CHECK(Protocol::ParsePacket(multi.data(), multi.size()).status == Protocol::ParseStatus::Malformed);
        const std::vector<unsigned char> fill = B(0x68).i8(0).i16(-5).bytes(16).v;
        CHECK(Protocol::ParsePacket(fill.data(), fill.size()).status == Protocol::ParseStatus::Malformed);
        const std::vector<unsigned char> explosion = B(0x3C).f64(1).f64(2).f64(3).f32(4).i32(-1).bytes(16).v;
        CHECK(Protocol::ParsePacket(explosion.data(), explosion.size()).status == Protocol::ParseStatus::Malformed);
    }

    SECTION("Huge lengths wait for data instead of allocating it")
    {
        const std::vector<unsigned char> chunk = B(0x33).i32(0).i16(0).i32(0).i8(15).i8(127).i8(15).i32(0x7FFFFFFF).bytes(100).v;
        CHECK(Protocol::ParsePacket(chunk.data(), chunk.size()).status == Protocol::ParseStatus::NeedMoreData);
        const std::vector<unsigned char> explosion = B(0x3C).f64(1).f64(2).f64(3).f32(4).i32(0x7FFFFFFF).bytes(16).v;
        CHECK(Protocol::ParsePacket(explosion.data(), explosion.size()).status == Protocol::ParseStatus::NeedMoreData);
    }
}

TEST_CASE("A stream of packets can be split one by one")
{
    std::vector<unsigned char> stream;
    size_t count = 0;
    for (const auto& c : Cases())
    {
        stream.insert(stream.end(), c.bytes.begin(), c.bytes.end());
        ++count;
    }

    size_t offset = 0;
    size_t parsed = 0;
    const auto cases = Cases();
    while (offset < stream.size())
    {
        const auto r = Protocol::ParsePacket(stream.data() + offset, stream.size() - offset);
        REQUIRE(r.status == Protocol::ParseStatus::Ok);
        CHECK(r.consumed == cases[parsed].bytes.size());
        offset += r.consumed;
        ++parsed;
    }
    CHECK(parsed == count);
    CHECK(offset == stream.size());
}
