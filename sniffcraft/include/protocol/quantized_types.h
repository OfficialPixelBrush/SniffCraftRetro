#pragma once

#include <cstdint>

// NOTE: SniffCraftRetro stand-in, inferred from how packets.h uses these types.
// These hold raw wire values (absolute positions are fixed point, 1/32 block).
// Types that end up in anonymous unions must stay trivially default constructible.

struct Int32_2 {
	int32_t x, z;
};

struct Int32_3 {
	int32_t x, y, z;
};

struct Int16_3 {
	int16_t x, y, z;
};

struct Int8_2 {
	int8_t x, y;
};

struct Int8_3 {
	int8_t x, y, z;
};
