#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "inventory/item_stack.h"
#include "quantized_types.h"

// NOTE: SniffCraftRetro stand-in. Enumerator values follow the Beta 1.7.3 wire
// format; the underlying type of each enum is its width on the wire. SniffCraft
// only ever reads/prints these as numbers, so a project-specific replacement may
// add or rename enumerators freely as long as the underlying types match.
namespace PacketData {

// byte
enum class MineStatus : uint8_t {
	Started = 0,
	Digging = 1, // not sent by vanilla
	Finished = 2,
	BlockBroken = 3,
	DropItem = 4,
};

// byte: -Y, +Y, -Z, +Z, -X, +X
enum class FaceDirection : int8_t {
	NegativeY = 0,
	PositiveY = 1,
	NegativeZ = 2,
	PositiveZ = 3,
	NegativeX = 4,
	PositiveX = 5,
};

// byte
enum class BlockInteraction : uint8_t {
	InBed = 0,
};

// byte
enum class Animation : uint8_t {
	None = 0,
	SwingArm = 1,
	Damage = 2,
	LeaveBed = 3,
	Crouch = 104,
	Uncrouch = 105,
};

// byte
enum class PlayerAction : uint8_t {
	Crouch = 1,
	Uncrouch = 2,
	LeaveBed = 3,
};

// byte
enum class ObjectType : uint8_t {
	Boat = 1,
	Minecart = 10,
	StorageMinecart = 11,
	PoweredMinecart = 12,
	ActivatedTnt = 50,
	Arrow = 60,
	Snowball = 61,
	Egg = 62,
	FallingSand = 70,
	FallingGravel = 71,
	FishingFloat = 90,
};

// byte
enum class MobType : uint8_t {
	Creeper = 50,
	Skeleton = 51,
	Spider = 52,
	Giant = 53,
	Zombie = 54,
	Slime = 55,
	Ghast = 56,
	ZombiePigman = 57,
	Pig = 90,
	Sheep = 91,
	Cow = 92,
	Chicken = 93,
	Squid = 94,
	Wolf = 95,
};

// int32
enum class PaintingDirection : int32_t {
	NegativeZ = 0,
	NegativeX = 1,
	PositiveZ = 2,
	PositiveX = 3,
};

// byte
enum class EntityEvent : uint8_t {
	Hurt = 2,
	Dead = 3,
	WolfSmoke = 6,
	WolfHearts = 7,
	WolfShake = 8,
};

// byte
enum class NoteInstrument : uint8_t {
	Harp = 0,
	DoubleBass = 1,
	SnareDrum = 2,
	Clicks = 3,
	BassDrum = 4,
};

// byte, 0..24
enum class NotePitch : uint8_t {};

// byte
enum class PistonState : uint8_t {
	Pushing = 0,
	Pulling = 1,
};

// byte
enum class PistonDirection : uint8_t {
	Down = 0,
	Up = 1,
	South = 2,
	West = 3,
	North = 4,
	East = 5,
};

// int32
enum class WorldEvent : int32_t {
	Click2 = 1000,
	Click1 = 1001,
	BowFire = 1002,
	DoorToggle = 1003,
	Extinguish = 1004,
	RecordPlay = 1005,
	Smoke = 2000,
	BlockBreak = 2001,
};

// byte
enum class GameEvent : uint8_t {
	InvalidBed = 0,
	BeginRain = 1,
	EndRain = 2,
};

// byte
enum class WindowType : uint8_t {
	Chest = 0,
	Workbench = 1,
	Furnace = 2,
	Dispenser = 3,
};

// int16 ("property" of a window)
enum class ContainerDataType : int16_t {
	FurnaceProgress = 0,
	FurnaceFire = 1,
};

// Entity metadata ("watchable objects"). Each entry starts with one byte:
// (type << 5) | index, and the list is terminated by 0x7F.
struct EntityMetadata {
	enum class Type : uint8_t {
		Byte = 0,
		Short = 1,
		Int = 2,
		Float = 3,
		String = 4,
		Slot = 5,     // int16 id, int8 count, int16 damage (always all three)
		Position = 6, // int32 x, y, z
	};

	struct DataEntry {
		uint8_t index = 0;
		Type type = Type::Byte;
		std::variant<int8_t, int16_t, int32_t, float, std::string, ItemStack, Int32_3> value;
	};
};

} // namespace PacketData
