#pragma once

#include <cstdint>

// NOTE: SniffCraftRetro stand-in. Dimension is a single signed byte on the wire.
enum class Dimension : int8_t {
	Nether = -1,
	Overworld = 0,
	Sky = 1,
};
