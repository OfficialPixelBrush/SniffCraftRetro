#include "sniffcraft/Protocol.hpp"

#include <algorithm>
#include <variant>

namespace Protocol
{
    // X(id enumerator, struct name in packets.h)
    // The struct name is what is used as the packet name everywhere in SniffCraft
#define SNIFFCRAFT_PACKET_LIST(X) \
    X(KeepAlive, KeepAlive) \
    X(Login, Login) \
    X(PreLogin, PreLogin) \
    X(ChatMessage, ChatMessage) \
    X(SetTime, SetTime) \
    X(SetEquipment, SetEquipment) \
    X(SetSpawnPosition, SetSpawnPosition) \
    X(InteractWithEntity, InteractWithEntity) \
    X(SetHealth, SetHealth) \
    X(Respawn, Respawn) \
    X(PlayerMovement, PlayerMovement) \
    X(PlayerPosition, PlayerPosition) \
    X(PlayerRotation, PlayerRotation) \
    X(PlayerPositionAndRotation, PlayerPositionAndRotation) \
    X(MineBlock, MineBlock) \
    X(PlaceBlock, PlaceBlock) \
    X(SetHotbarSlot, SetHotbarSlot) \
    X(InteractWithBlock, InteractWithBlock) \
    X(Animation, Animation) \
    X(PlayerAction, PlayerAction) \
    X(SpawnPlayer, SpawnPlayer) \
    X(SpawnItem, SpawnItem) \
    X(CollectItem, CollectItem) \
    X(SpawnObject, SpawnObject) \
    X(SpawnMob, SpawnMob) \
    X(SpawnPainting, SpawnPainting) \
    X(PlayerInput, PlayerInput) \
    X(EntityVelocity, EntityVelocity) \
    X(DespawnEntity, DespawnEntity) \
    X(EntityMovement, EntityMovement) \
    X(EntityPosition, EntityPosition) \
    X(EntityRotation, EntityRotation) \
    X(EntityPositionAndRotation, EntityPositionAndRotation) \
    X(TeleportEntity, TeleportEntity) \
    X(EntityEvent, EntityEvent) \
    X(AddPassenger, AddPassenger) \
    X(EntityMetadata, EntityMetadata) \
    X(SetChunkVisibility, SetChunkVisibility) \
    X(Chunk, ChunkData) \
    X(SetMultipleBlocks, SetMultipleBlocks) \
    X(SetBlock, SetBlock) \
    X(BlockEvent, BlockEvent) \
    X(Explosion, Explosion) \
    X(WorldEvent, WorldEvent) \
    X(GameEvent, GameEvent) \
    X(LightningBolt, LightningBolt) \
    X(OpenContainer, OpenContainer) \
    X(CloseContainer, CloseContainer) \
    X(ClickSlot, ClickSlot) \
    X(SetSlot, SetSlot) \
    X(FillContainer, FillContainer) \
    X(ContainerData, ContainerData) \
    X(ContainerTransaction, ContainerTransaction) \
    X(UpdateSign, UpdateSign) \
    X(ItemData, ItemData) \
    X(IncrementStatistic, IncrementStatistic) \
    X(Disconnect, Disconnect)

    std::shared_ptr<Packet::BasePacket> CreatePacket(const int id)
    {
        if (id < 0 || id > 255)
        {
            return nullptr;
        }
        switch (static_cast<PacketId>(id))
        {
#define X(ID, TYPE) case PacketId::ID: return std::make_shared<Packet::TYPE>();
            SNIFFCRAFT_PACKET_LIST(X)
#undef X
        }
        return nullptr;
    }

    const std::vector<PacketInfo>& AllPackets()
    {
        static const std::vector<PacketInfo> packets = []() {
            std::vector<PacketInfo> v = {
#define X(ID, TYPE) PacketInfo{ static_cast<int>(PacketId::ID), #TYPE },
                SNIFFCRAFT_PACKET_LIST(X)
#undef X
            };
            std::sort(v.begin(), v.end(), [](const PacketInfo& a, const PacketInfo& b) { return a.id < b.id; });
            return v;
        }();
        return packets;
    }

    std::string_view PacketName(const int id)
    {
        for (const PacketInfo& p : AllPackets())
        {
            if (p.id == id)
            {
                return p.name;
            }
        }
        return std::string_view();
    }

    int PacketIdFromName(const std::string_view& name)
    {
        for (const PacketInfo& p : AllPackets())
        {
            if (p.name == name)
            {
                return p.id;
            }
        }
        return -1;
    }

    ParseResult ParsePacket(const unsigned char* data, size_t size, std::vector<NetworkStream::Span>* trace)
    {
        ParseResult result;
        if (trace != nullptr)
        {
            trace->clear();
        }
        if (size == 0)
        {
            result.status = ParseStatus::NeedMoreData;
            return result;
        }

        result.id = data[0];
        std::shared_ptr<Packet::BasePacket> packet = CreatePacket(result.id);
        if (packet == nullptr)
        {
            result.status = ParseStatus::UnknownPacket;
            result.error = "unknown packet id " + std::to_string(result.id);
            return result;
        }

        NetworkStream stream(data, size, trace);
        try
        {
            stream.Skip(1);
            packet->Deserialize(stream);
        }
        catch (const NetworkStream::NotEnoughData&)
        {
            if (trace != nullptr)
            {
                trace->clear();
            }
            result.status = ParseStatus::NeedMoreData;
            return result;
        }
        catch (const std::exception& e)
        {
            if (trace != nullptr)
            {
                trace->clear();
            }
            result.status = ParseStatus::Malformed;
            result.error = std::string("invalid ") + std::string(PacketName(result.id)) + " packet: " + e.what();
            return result;
        }

        result.status = ParseStatus::Ok;
        result.consumed = stream.Position();
        result.packet = std::move(packet);
        return result;
    }

    namespace
    {
        /// @brief Helper to build the json of a packet. Every value must be passed
        /// through operator() in the same order as it was read from the stream, so
        /// the byte ranges recorded while parsing can be attached to the right value.
        class Dumper
        {
        public:
            Dumper(const std::vector<NetworkStream::Span>* spans_, const size_t total_size_) :
                spans(spans_),
                total_size(total_size_)
            {

            }

            size_t Used() const
            {
                return index;
            }

            /// @brief Wrap v with the next recorded byte range (if any)
            Json::Value operator()(Json::Value v)
            {
                if (spans == nullptr || index >= spans->size())
                {
                    ++index;
                    return v;
                }
                const NetworkStream::Span& span = (*spans)[index++];
                Json::Object wrapped;
                wrapped["start_offset"] = static_cast<unsigned long long int>(total_size - span.begin);
                wrapped["end_offset"] = static_cast<unsigned long long int>(total_size - span.end);
                wrapped["content"] = std::move(v);
                return wrapped;
            }

            template<typename X, typename Y, typename Z>
            Json::Value Xyz(const X x, const Y y, const Z z)
            {
                Json::Object o;
                o["x"] = (*this)(Json::Value(x));
                o["y"] = (*this)(Json::Value(y));
                o["z"] = (*this)(Json::Value(z));
                return o;
            }

            /// @brief Item with the optional count and damage fields. They are
            /// only read from the stream when with_details is true
            Json::Value Item(const ItemStack& item, const bool with_details)
            {
                Json::Object o;
                o["id"] = (*this)(Json::Value(item.id));
                if (with_details)
                {
                    o["count"] = (*this)(Json::Value(item.count));
                    o["damage"] = (*this)(Json::Value(item.data));
                }
                return o;
            }

            /// @brief Entity metadata, one range per entry + one for the terminator
            Json::Value Metadata(const std::vector<PacketData::EntityMetadata::DataEntry>& metadata)
            {
                Json::Array output;
                for (const auto& entry : metadata)
                {
                    Json::Object o;
                    o["index"] = Json::Value(entry.index);
                    switch (entry.type)
                    {
                    case PacketData::EntityMetadata::Type::Byte:
                        o["type"] = "byte";
                        break;
                    case PacketData::EntityMetadata::Type::Short:
                        o["type"] = "short";
                        break;
                    case PacketData::EntityMetadata::Type::Int:
                        o["type"] = "int";
                        break;
                    case PacketData::EntityMetadata::Type::Float:
                        o["type"] = "float";
                        break;
                    case PacketData::EntityMetadata::Type::String:
                        o["type"] = "string";
                        break;
                    case PacketData::EntityMetadata::Type::Slot:
                        o["type"] = "slot";
                        break;
                    case PacketData::EntityMetadata::Type::Position:
                        o["type"] = "position";
                        break;
                    }
                    std::visit([&o](const auto& v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, ItemStack>)
                        {
                            Json::Object item;
                            item["id"] = Json::Value(v.id);
                            item["count"] = Json::Value(v.count);
                            item["damage"] = Json::Value(v.data);
                            o["value"] = std::move(item);
                        }
                        else if constexpr (std::is_same_v<T, Int32_3>)
                        {
                            Json::Object pos;
                            pos["x"] = Json::Value(v.x);
                            pos["y"] = Json::Value(v.y);
                            pos["z"] = Json::Value(v.z);
                            o["value"] = std::move(pos);
                        }
                        else
                        {
                            o["value"] = Json::Value(v);
                        }
                    }, entry.value);
                    output.push_back((*this)(Json::Value(std::move(o))));
                }
                // Terminator
                (*this)(Json::Value());
                return output;
            }

        private:
            const std::vector<NetworkStream::Span>* spans;
            size_t total_size;
            size_t index = 0;
        };

        /// @brief Convenience to chain o["a"] = d(...) in reading order
        Json::Value Dump(const Packet::BasePacket& base, const bool clientbound, Dumper& d)
        {
            Json::Object o;

            switch (base.id)
            {
            case PacketId::KeepAlive:
                break;
            case PacketId::Login:
            {
                const auto& p = static_cast<const Packet::Login&>(base);
                o[clientbound ? "entityId" : "protocolVersion"] = d(Json::Value(p.entityIdProtocolVersion));
                o["username"] = d(Json::Value(p.username));
                o["worldSeed"] = d(Json::Value(p.worldSeed));
                o["dimension"] = d(Json::Value(p.dimension));
                break;
            }
            case PacketId::PreLogin:
            {
                const auto& p = static_cast<const Packet::PreLogin&>(base);
                o[clientbound ? "serverId" : "username"] = d(Json::Value(p.usernameServerId));
                break;
            }
            case PacketId::ChatMessage:
            {
                const auto& p = static_cast<const Packet::ChatMessage&>(base);
                o["message"] = d(Json::Value(p.message));
                break;
            }
            case PacketId::SetTime:
            {
                const auto& p = static_cast<const Packet::SetTime&>(base);
                o["time"] = d(Json::Value(p.time));
                break;
            }
            case PacketId::SetEquipment:
            {
                const auto& p = static_cast<const Packet::SetEquipment&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["inventorySlot"] = d(Json::Value(p.inventorySlot));
                o["itemId"] = d(Json::Value(p.itemId));
                o["itemMetadata"] = d(Json::Value(p.itemMetadata));
                break;
            }
            case PacketId::SetSpawnPosition:
            {
                const auto& p = static_cast<const Packet::SetSpawnPosition&>(base);
                o["position"] = d.Xyz(p.position.x, p.position.y, p.position.z);
                break;
            }
            case PacketId::InteractWithEntity:
            {
                const auto& p = static_cast<const Packet::InteractWithEntity&>(base);
                o["sourceEntityId"] = d(Json::Value(p.sourceEntityId));
                o["targetEntityId"] = d(Json::Value(p.targetEntityId));
                o["attack"] = d(Json::Value(p.attack));
                break;
            }
            case PacketId::SetHealth:
            {
                const auto& p = static_cast<const Packet::SetHealth&>(base);
                o["health"] = d(Json::Value(p.health));
                break;
            }
            case PacketId::Respawn:
            {
                const auto& p = static_cast<const Packet::Respawn&>(base);
                o["dimension"] = d(Json::Value(p.dimension));
                break;
            }
            case PacketId::PlayerMovement:
            {
                const auto& p = static_cast<const Packet::PlayerMovement&>(base);
                o["onGround"] = d(Json::Value(p.onGround));
                break;
            }
            case PacketId::PlayerPosition:
            case PacketId::PlayerPositionAndRotation:
            {
                // Wire order is x, y, cameraY, z. Sent by the client, the second double
                // is the feet position and the third one the eye height. Sent by the
                // server, they are flipped (see the comment in packets.h)
                double x, second, third, z;
                float yaw = 0.0f, pitch = 0.0f;
                bool on_ground;
                const bool with_rotation = base.id == PacketId::PlayerPositionAndRotation;
                if (with_rotation)
                {
                    const auto& p = static_cast<const Packet::PlayerPositionAndRotation&>(base);
                    x = p.position.x; second = p.position.y; third = p.cameraY; z = p.position.z;
                    yaw = p.yaw; pitch = p.pitch; on_ground = p.onGround;
                }
                else
                {
                    const auto& p = static_cast<const Packet::PlayerPosition&>(base);
                    x = p.position.x; second = p.position.y; third = p.cameraY; z = p.position.z;
                    on_ground = p.onGround;
                }
                const Json::Value jx = d(Json::Value(x));
                const Json::Value jsecond = d(Json::Value(second));
                const Json::Value jthird = d(Json::Value(third));
                const Json::Value jz = d(Json::Value(z));
                Json::Object position;
                position["x"] = jx;
                position["y"] = clientbound ? jthird : jsecond;
                position["z"] = jz;
                o["position"] = std::move(position);
                o["cameraY"] = clientbound ? jsecond : jthird;
                if (with_rotation)
                {
                    o["yaw"] = d(Json::Value(yaw));
                    o["pitch"] = d(Json::Value(pitch));
                }
                o["onGround"] = d(Json::Value(on_ground));
                break;
            }
            case PacketId::PlayerRotation:
            {
                const auto& p = static_cast<const Packet::PlayerRotation&>(base);
                o["yaw"] = d(Json::Value(p.yaw));
                o["pitch"] = d(Json::Value(p.pitch));
                o["onGround"] = d(Json::Value(p.onGround));
                break;
            }
            case PacketId::MineBlock:
            {
                const auto& p = static_cast<const Packet::MineBlock&>(base);
                o["status"] = d(Json::Value(p.status));
                o["position"] = d.Xyz(p.position.x, p.position.y, p.position.z);
                o["face"] = d(Json::Value(p.face));
                break;
            }
            case PacketId::PlaceBlock:
            {
                const auto& p = static_cast<const Packet::PlaceBlock&>(base);
                o["position"] = d.Xyz(p.position.x, p.position.y, p.position.z);
                o["face"] = d(Json::Value(p.face));
                o["item"] = d.Item(p.item, static_cast<int16_t>(p.item.id) >= 0);
                break;
            }
            case PacketId::SetHotbarSlot:
            {
                const auto& p = static_cast<const Packet::SetHotbarSlot&>(base);
                o["slot"] = d(Json::Value(p.slot));
                break;
            }
            case PacketId::InteractWithBlock:
            {
                const auto& p = static_cast<const Packet::InteractWithBlock&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["interactionId"] = d(Json::Value(p.interactionId));
                o["position"] = d.Xyz(p.position.x, p.position.y, p.position.z);
                break;
            }
            case PacketId::Animation:
            {
                const auto& p = static_cast<const Packet::Animation&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["animation"] = d(Json::Value(p.animation));
                break;
            }
            case PacketId::PlayerAction:
            {
                const auto& p = static_cast<const Packet::PlayerAction&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["action"] = d(Json::Value(p.action));
                break;
            }
            case PacketId::SpawnPlayer:
            {
                const auto& p = static_cast<const Packet::SpawnPlayer&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["username"] = d(Json::Value(p.username));
                o["qPosition"] = d.Xyz(p.qPosition.x, p.qPosition.y, p.qPosition.z);
                o["qYaw"] = d(Json::Value(p.qYaw));
                o["qPitch"] = d(Json::Value(p.qPitch));
                o["heldItemId"] = d(Json::Value(p.heldItemId));
                break;
            }
            case PacketId::SpawnItem:
            {
                const auto& p = static_cast<const Packet::SpawnItem&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["item"] = d.Item(p.item, true);
                o["qPosition"] = d.Xyz(p.qPosition.x, p.qPosition.y, p.qPosition.z);
                o["qVelocity"] = d.Xyz(p.qVelocity.x, p.qVelocity.y, p.qVelocity.z);
                break;
            }
            case PacketId::CollectItem:
            {
                const auto& p = static_cast<const Packet::CollectItem&>(base);
                o["itemEntityId"] = d(Json::Value(p.itemEntityId));
                o["collectorEntityId"] = d(Json::Value(p.collectorEntityId));
                break;
            }
            case PacketId::SpawnObject:
            {
                const auto& p = static_cast<const Packet::SpawnObject&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["objectType"] = d(Json::Value(p.objectType));
                o["qPosition"] = d.Xyz(p.qPosition.x, p.qPosition.y, p.qPosition.z);
                o["ownerEntityId"] = d(Json::Value(p.ownerEntityId));
                if (p.ownerEntityId)
                {
                    o["qVelocity"] = d.Xyz(p.qVelocity.x, p.qVelocity.y, p.qVelocity.z);
                }
                break;
            }
            case PacketId::SpawnMob:
            {
                const auto& p = static_cast<const Packet::SpawnMob&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["mobType"] = d(Json::Value(p.mobType));
                o["qPosition"] = d.Xyz(p.qPosition.x, p.qPosition.y, p.qPosition.z);
                o["qYaw"] = d(Json::Value(p.qYaw));
                o["qPitch"] = d(Json::Value(p.qPitch));
                o["metadata"] = d.Metadata(p.metadata);
                break;
            }
            case PacketId::SpawnPainting:
            {
                const auto& p = static_cast<const Packet::SpawnPainting&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["title"] = d(Json::Value(p.title));
                o["qPosition"] = d.Xyz(p.qPosition.x, p.qPosition.y, p.qPosition.z);
                o["direction"] = d(Json::Value(p.direction));
                break;
            }
            case PacketId::PlayerInput:
            {
                const auto& p = static_cast<const Packet::PlayerInput&>(base);
                o["strafeDirection"] = d(Json::Value(p.strafeDirection));
                o["forwardDirection"] = d(Json::Value(p.forwardDirection));
                o["pitch"] = d(Json::Value(p.pitch));
                o["yaw"] = d(Json::Value(p.yaw));
                o["jumping"] = d(Json::Value(p.jumping));
                o["sneaking"] = d(Json::Value(p.sneaking));
                break;
            }
            case PacketId::EntityVelocity:
            {
                const auto& p = static_cast<const Packet::EntityVelocity&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["velocity"] = d.Xyz(p.velocity.x, p.velocity.y, p.velocity.z);
                break;
            }
            case PacketId::DespawnEntity:
            {
                const auto& p = static_cast<const Packet::DespawnEntity&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                break;
            }
            case PacketId::EntityMovement:
            {
                const auto& p = static_cast<const Packet::EntityMovement&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                break;
            }
            case PacketId::EntityPosition:
            {
                const auto& p = static_cast<const Packet::EntityPosition&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["qrPosition"] = d.Xyz(p.qrPosition.x, p.qrPosition.y, p.qrPosition.z);
                break;
            }
            case PacketId::EntityRotation:
            {
                const auto& p = static_cast<const Packet::EntityRotation&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["qYaw"] = d(Json::Value(p.qYaw));
                o["qPitch"] = d(Json::Value(p.qPitch));
                break;
            }
            case PacketId::EntityPositionAndRotation:
            {
                const auto& p = static_cast<const Packet::EntityPositionAndRotation&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["qrPosition"] = d.Xyz(p.qrPosition.x, p.qrPosition.y, p.qrPosition.z);
                o["qYaw"] = d(Json::Value(p.qYaw));
                o["qPitch"] = d(Json::Value(p.qPitch));
                break;
            }
            case PacketId::TeleportEntity:
            {
                const auto& p = static_cast<const Packet::TeleportEntity&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["qPosition"] = d.Xyz(p.qPosition.x, p.qPosition.y, p.qPosition.z);
                o["qYaw"] = d(Json::Value(p.qYaw));
                o["qPitch"] = d(Json::Value(p.qPitch));
                break;
            }
            case PacketId::EntityEvent:
            {
                const auto& p = static_cast<const Packet::EntityEvent&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["action"] = d(Json::Value(p.action));
                break;
            }
            case PacketId::AddPassenger:
            {
                const auto& p = static_cast<const Packet::AddPassenger&>(base);
                o["passengerEntityId"] = d(Json::Value(p.passengerEntityId));
                o["vehicleEntityId"] = d(Json::Value(p.vehicleEntityId));
                break;
            }
            case PacketId::EntityMetadata:
            {
                const auto& p = static_cast<const Packet::EntityMetadata&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["metadata"] = d.Metadata(p.metadata);
                break;
            }
            case PacketId::SetChunkVisibility:
            {
                const auto& p = static_cast<const Packet::SetChunkVisibility&>(base);
                Json::Object pos;
                pos["x"] = d(Json::Value(p.pos.x));
                pos["z"] = d(Json::Value(p.pos.z));
                o["pos"] = std::move(pos);
                o["visible"] = d(Json::Value(p.visible));
                break;
            }
            case PacketId::Chunk:
            {
                const auto& p = static_cast<const Packet::ChunkData&>(base);
                o["pos"] = d.Xyz(p.pos.x, p.pos.y, p.pos.z);
                // Sizes are sent as size - 1 on the wire, this is the raw value
                o["size"] = d.Xyz(p.size.x, p.size.y, p.size.z);
                o["compressedSize"] = d(Json::Value(static_cast<int32_t>(p.compressedData.size())));
                // Dumping all the bytes would make the log unreadable
                Json::Object data;
                data["size"] = static_cast<unsigned long long int>(p.compressedData.size());
                o["compressedData"] = d(Json::Value(std::move(data)));
                break;
            }
            case PacketId::SetMultipleBlocks:
            {
                const auto& p = static_cast<const Packet::SetMultipleBlocks&>(base);
                Json::Object chunk;
                chunk["x"] = d(Json::Value(p.chunkPosition.x));
                chunk["z"] = d(Json::Value(p.chunkPosition.z));
                o["chunkPosition"] = std::move(chunk);
                o["numberOfBlocks"] = d(Json::Value(p.numberOfBlocks));
                // Three arrays of numberOfBlocks values on the wire, one after the other
                const size_t n = static_cast<size_t>(std::max<int16_t>(p.numberOfBlocks, 0));
                std::vector<Json::Value> coordinates, types, metadata;
                for (size_t i = 0; i < n; ++i)
                {
                    coordinates.push_back(d(Json::Value(p.blockCoordinates[i])));
                }
                for (size_t i = 0; i < n; ++i)
                {
                    types.push_back(d(Json::Value(p.blockTypes[i])));
                }
                for (size_t i = 0; i < n; ++i)
                {
                    metadata.push_back(d(Json::Value(p.blockMetadata[i])));
                }
                Json::Array blocks;
                for (size_t i = 0; i < n; ++i)
                {
                    Json::Object block;
                    block["coordinate"] = std::move(coordinates[i]);
                    block["type"] = std::move(types[i]);
                    block["metadata"] = std::move(metadata[i]);
                    blocks.push_back(std::move(block));
                }
                o["blocks"] = std::move(blocks);
                break;
            }
            case PacketId::SetBlock:
            {
                const auto& p = static_cast<const Packet::SetBlock&>(base);
                o["position"] = d.Xyz(p.position.x, p.position.y, p.position.z);
                o["type"] = d(Json::Value(p.block.type));
                o["data"] = d(Json::Value(p.block.data));
                break;
            }
            case PacketId::BlockEvent:
            {
                const auto& p = static_cast<const Packet::BlockEvent&>(base);
                o["position"] = d.Xyz(p.position.x, p.position.y, p.position.z);
                o["instrumentState"] = d(Json::Value(p.instrumentState));
                o["pitchDirection"] = d(Json::Value(p.pitchDirection));
                break;
            }
            case PacketId::Explosion:
            {
                const auto& p = static_cast<const Packet::Explosion&>(base);
                o["position"] = d.Xyz(p.position.x, p.position.y, p.position.z);
                o["radius"] = d(Json::Value(p.radius));
                o["numberOfDestroyedBlocks"] = d(Json::Value(p.numberOfDestroyedBlocks));
                // Records of 3 signed bytes (x, y, z offsets from the center)
                Json::Array records;
                for (size_t i = 0; i + 2 < p.destroyedBlocks.size(); i += 3)
                {
                    Json::Object record;
                    record["x"] = Json::Value(p.destroyedBlocks[i]);
                    record["y"] = Json::Value(p.destroyedBlocks[i + 1]);
                    record["z"] = Json::Value(p.destroyedBlocks[i + 2]);
                    records.push_back(std::move(record));
                }
                o["destroyedBlocks"] = d(Json::Value(std::move(records)));
                break;
            }
            case PacketId::WorldEvent:
            {
                const auto& p = static_cast<const Packet::WorldEvent&>(base);
                o["eventType"] = d(Json::Value(p.eventType));
                o["position"] = d.Xyz(p.position.x, p.position.y, p.position.z);
                o["data"] = d(Json::Value(p.data));
                break;
            }
            case PacketId::GameEvent:
            {
                const auto& p = static_cast<const Packet::GameEvent&>(base);
                o["eventId"] = d(Json::Value(p.eventId));
                break;
            }
            case PacketId::LightningBolt:
            {
                const auto& p = static_cast<const Packet::LightningBolt&>(base);
                o["entityId"] = d(Json::Value(p.entityId));
                o["entityType"] = d(Json::Value(p.entityType));
                o["position"] = d.Xyz(p.position.x, p.position.y, p.position.z);
                break;
            }
            case PacketId::OpenContainer:
            {
                const auto& p = static_cast<const Packet::OpenContainer&>(base);
                o["windowId"] = d(Json::Value(p.windowId));
                o["windowType"] = d(Json::Value(p.windowType));
                o["title"] = d(Json::Value(p.title));
                o["slotCount"] = d(Json::Value(p.slotCount));
                break;
            }
            case PacketId::CloseContainer:
            {
                const auto& p = static_cast<const Packet::CloseContainer&>(base);
                o["windowId"] = d(Json::Value(p.windowId));
                break;
            }
            case PacketId::ClickSlot:
            {
                const auto& p = static_cast<const Packet::ClickSlot&>(base);
                o["windowId"] = d(Json::Value(p.windowId));
                o["slotId"] = d(Json::Value(p.slotId));
                o["rightClick"] = d(Json::Value(p.rightClick));
                o["transactionId"] = d(Json::Value(p.transactionId));
                o["shift"] = d(Json::Value(p.shift));
                o["item"] = d.Item(p.item, p.item.id != Items::Id::INVALID);
                break;
            }
            case PacketId::SetSlot:
            {
                const auto& p = static_cast<const Packet::SetSlot&>(base);
                o["windowId"] = d(Json::Value(p.windowId));
                o["slotId"] = d(Json::Value(p.slotId));
                o["item"] = d.Item(p.item, p.item.id != Items::Id::INVALID);
                break;
            }
            case PacketId::FillContainer:
            {
                const auto& p = static_cast<const Packet::FillContainer&>(base);
                o["windowId"] = d(Json::Value(p.windowId));
                o["slotCount"] = d(Json::Value(static_cast<int16_t>(p.items.size())));
                Json::Array items;
                for (const ItemStack& item : p.items)
                {
                    items.push_back(d.Item(item, item.id != Items::Id::INVALID));
                }
                o["items"] = std::move(items);
                break;
            }
            case PacketId::ContainerData:
            {
                const auto& p = static_cast<const Packet::ContainerData&>(base);
                o["windowId"] = d(Json::Value(p.windowId));
                o["type"] = d(Json::Value(p.containerData.type));
                o["value"] = d(Json::Value(p.containerData.value));
                break;
            }
            case PacketId::ContainerTransaction:
            {
                const auto& p = static_cast<const Packet::ContainerTransaction&>(base);
                o["windowId"] = d(Json::Value(p.windowId));
                o["transactionId"] = d(Json::Value(p.transactionId));
                o["accepted"] = d(Json::Value(p.accepted));
                break;
            }
            case PacketId::UpdateSign:
            {
                const auto& p = static_cast<const Packet::UpdateSign&>(base);
                o["position"] = d.Xyz(p.position.x, p.position.y, p.position.z);
                Json::Array lines;
                for (size_t i = 0; i < 4; ++i)
                {
                    lines.push_back(d(Json::Value(p.lines[i])));
                }
                o["lines"] = std::move(lines);
                break;
            }
            case PacketId::ItemData:
            {
                const auto& p = static_cast<const Packet::ItemData&>(base);
                o["itemId"] = d(Json::Value(p.itemId));
                o["mapId"] = d(Json::Value(p.mapId));
                o["size"] = d(Json::Value(static_cast<uint8_t>(p.data.size())));
                Json::Array data;
                for (const uint8_t b : p.data)
                {
                    data.push_back(Json::Value(b));
                }
                o["data"] = d(Json::Value(std::move(data)));
                break;
            }
            case PacketId::IncrementStatistic:
            {
                const auto& p = static_cast<const Packet::IncrementStatistic&>(base);
                o["statisticId"] = d(Json::Value(p.statisticId));
                o["amount"] = d(Json::Value(p.amount));
                break;
            }
            case PacketId::Disconnect:
            {
                const auto& p = static_cast<const Packet::Disconnect&>(base);
                o["reason"] = d(Json::Value(p.reason));
                break;
            }
            }

            return o;
        }
    }

    Json::Value PacketToJson(
        const Packet::BasePacket& packet,
        const bool clientbound,
        const std::vector<NetworkStream::Span>* spans,
        const size_t total_size,
        size_t* spans_used
    )
    {
        Dumper dumper(spans, total_size);
        Json::Value output = Dump(packet, clientbound, dumper);
        if (spans_used != nullptr)
        {
            *spans_used = dumper.Used();
        }
        return output;
    }
}
