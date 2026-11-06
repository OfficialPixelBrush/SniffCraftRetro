#pragma once

#include <cstdint>

// NOTE: SniffCraftRetro stand-in, inferred from how packets.h uses these aliases.
// The wire width of each alias is what matters for the Beta 1.7.3 format:
//   EntityId      int32     (always 4 bytes)
//   WindowId      byte
//   TransactionId int16     (the "action number" of a window click)
//   NetworkSlotId int16
//   ItemAmount    byte      (stack size)
//   ItemDamage    int16     (damage / metadata)
//   MapId         int16
//   TickTime      int64     (world time in ticks)
//   BlockType     byte
using EntityId = int32_t;
using WindowId = uint8_t;
using TransactionId = int16_t;
using NetworkSlotId = int16_t;
using ItemAmount = int8_t;
using ItemDamage = int16_t;
using MapId = int16_t;
using TickTime = int64_t;

// Block ids in Beta 1.7.3 fit in a byte. Any value is representable.
enum class BlockType : uint8_t {};
