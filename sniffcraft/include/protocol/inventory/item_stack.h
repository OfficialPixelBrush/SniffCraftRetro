#pragma once

#include "../base_types.h"

// NOTE: SniffCraftRetro stand-in.
// Item ids are int16 on the wire; -1 means "no item" (the count and damage fields
// are then absent from the stream for most packets).
namespace Items {
enum class Id : int16_t {
	INVALID = -1,
};
} // namespace Items

using ItemId = Items::Id;

struct ItemStack {
	ItemId id = Items::Id::INVALID;
	ItemAmount count = 0;
	ItemDamage data = 0;
};
