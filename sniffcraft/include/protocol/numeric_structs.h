#pragma once

#include <cstdint>

// NOTE: SniffCraftRetro stand-in, inferred from how packets.h uses these types.
//
// Several of these are placed in anonymous unions inside packets.h (e.g. Float2
// next to `struct { float yaw, pitch; }`), which requires them to be trivially
// default constructible. Keep them as plain aggregates without default member
// initializers.

struct Vec3 {
	double x, y, z;
};

struct Float2 {
	float x, y;
};

template <typename T>
struct TriNumber {
	T x, y, z;
};

// "Slim" integer triple: x and z are always int32 on the wire, y has its own width.
// (Block positions in Beta 1.7.3 are int32 x, int8 y, int32 z for most packets and
// int32 x, int16 y, int32 z for a few others.)
template <typename T>
struct SlimInt3 {
	int32_t x;
	T y;
	int32_t z;
};
