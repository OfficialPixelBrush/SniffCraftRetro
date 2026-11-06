#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "packet_data.h"

// NOTE: SniffCraftRetro stand-in for the NetworkStream that packets.h is written
// against. It implements the primitive types of the Minecraft Beta 1.7.3 protocol:
//
//   - all integers/floats are big-endian
//   - bool is one byte
//   - String16: int16 length in *characters*, then that many UTF-16BE code units
//   - String8:  uint16 length in *bytes*, then (modified) UTF-8 (Java's writeUTF)
//   - entity metadata: see ReadEntityMetadata
//
// The same class is used for reading (constructed over a byte buffer) and writing
// (default constructed, bytes available through Buffer()).
//
// Beyond what packets.h needs, this stream provides:
//   - NotEnoughData: thrown when the buffer ends in the middle of a packet. Beta 1.7.3
//     packets have no length prefix, so this is how a proxy knows it has to wait for
//     more bytes.
//   - Require(n): lets length-prefixed arrays be validated *before* allocating them.
//   - optional tracing: when a trace vector is supplied, every logical read (one
//     primitive, one string, one byte array, one metadata entry) records the byte
//     range it consumed. SniffCraft uses this to link fields to bytes in its GUI.
class NetworkStream {
public:
	struct Span {
		size_t begin;
		size_t end;
	};

	// Thrown when the input ends before the packet is complete
	class NotEnoughData : public std::runtime_error {
	public:
		NotEnoughData() : std::runtime_error("not enough data") {}
	};

	// Thrown when the input can not be a valid packet
	class ProtocolError : public std::runtime_error {
	public:
		explicit ProtocolError(const std::string& what) : std::runtime_error(what) {}
	};

	// Writer
	NetworkStream() = default;

	// Reader over [data, data + size). The buffer must outlive the stream.
	NetworkStream(const uint8_t* data, size_t size, std::vector<Span>* trace = nullptr)
		: readData_(data), readSize_(size), trace_(trace) {}

	// --- Reader state ---
	size_t Position() const { return readPos_; }
	size_t Remaining() const { return readSize_ - readPos_; }

	// Throws NotEnoughData if fewer than n bytes are left
	void Require(size_t n) const {
		if (n > Remaining())
			throw NotEnoughData();
	}

	// Validates a length read from the wire before allocating a container for it.
	// Throws ProtocolError if count is negative, NotEnoughData if count * itemSize bytes
	// are not available (yet). itemSize is the minimum size of one element.
	void RequireCount(int64_t count, size_t itemSize) const {
		if (count < 0)
			throw ProtocolError("negative length " + std::to_string(count));
		const uint64_t needed = static_cast<uint64_t>(count) * itemSize;
		if (needed > Remaining())
			throw NotEnoughData();
	}

	// Skips n bytes without tracing them
	void Skip(size_t n) {
		Require(n);
		readPos_ += n;
	}

	// --- Writer state ---
	const std::vector<uint8_t>& Buffer() const { return out_; }

	// --- Primitives ---
	template <typename T>
	T Read() {
		const size_t begin = readPos_;
		T value = ReadRaw<T>();
		Trace(begin);
		return value;
	}

	template <typename T>
	void Write(T value) {
		if constexpr (std::is_enum_v<T>) {
			Write(static_cast<std::underlying_type_t<T>>(value));
		} else if constexpr (std::is_same_v<T, bool>) {
			out_.push_back(value ? 1 : 0);
		} else if constexpr (std::is_floating_point_v<T>) {
			using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
			static_assert(sizeof(T) == 4 || sizeof(T) == 8, "unsupported floating point type");
			U bits;
			std::memcpy(&bits, &value, sizeof(T));
			Write(bits);
		} else {
			static_assert(std::is_integral_v<T>, "unsupported type");
			using U = std::make_unsigned_t<T>;
			const U bits = static_cast<U>(value);
			for (size_t i = sizeof(T); i-- > 0;)
				out_.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFF));
		}
	}

	void WriteBytes(const uint8_t* data, size_t size) {
		if (size > 0)
			out_.insert(out_.end(), data, data + size);
	}

	void ReadBytes(uint8_t* dst, size_t size) {
		const size_t begin = readPos_;
		Require(size);
		if (size > 0)
			std::memcpy(dst, readData_ + readPos_, size);
		readPos_ += size;
		Trace(begin);
	}

	// --- Strings ---
	std::string ReadString16() {
		const size_t begin = readPos_;
		const int16_t length = ReadRaw<int16_t>();
		if (length < 0)
			throw ProtocolError("negative string length");
		Require(static_cast<size_t>(length) * 2);
		std::vector<char16_t> units(static_cast<size_t>(length));
		for (auto& u : units)
			u = static_cast<char16_t>(ReadRaw<uint16_t>());
		std::string result = Utf16ToUtf8(units);
		Trace(begin);
		return result;
	}

	void WriteString16(const std::string& utf8) {
		const std::vector<char16_t> units = Utf8ToUtf16(utf8);
		if (units.size() > 32767)
			throw ProtocolError("string too long");
		Write(static_cast<int16_t>(units.size()));
		for (char16_t u : units)
			Write(static_cast<uint16_t>(u));
	}

	std::string ReadString8() {
		const size_t begin = readPos_;
		const uint16_t length = ReadRaw<uint16_t>();
		Require(length);
		std::string raw(reinterpret_cast<const char*>(readData_ + readPos_), length);
		readPos_ += length;
		std::string result = ModifiedUtf8ToUtf8(raw);
		Trace(begin);
		return result;
	}

	void WriteString8(const std::string& utf8) {
		const std::string raw = Utf8ToModifiedUtf8(utf8);
		if (raw.size() > 65535)
			throw ProtocolError("string too long");
		Write(static_cast<uint16_t>(raw.size()));
		WriteBytes(reinterpret_cast<const uint8_t*>(raw.data()), raw.size());
	}

	// --- Entity metadata ---
	// Entries: byte header ((type << 5) | index), then the value; ended by a 0x7F byte.
	// Each entry is one traced span, and so is the terminator.
	void ReadEntityMetadata(std::vector<PacketData::EntityMetadata::DataEntry>& entries) {
		using Entry = PacketData::EntityMetadata::DataEntry;
		using Type = PacketData::EntityMetadata::Type;
		entries.clear();
		while (true) {
			const size_t begin = readPos_;
			const uint8_t header = ReadRaw<uint8_t>();
			if (header == 0x7F) {
				Trace(begin);
				return;
			}
			Entry entry;
			entry.index = header & 0x1F;
			const uint8_t type = header >> 5;
			switch (type) {
			case 0:
				entry.type = Type::Byte;
				entry.value = ReadRaw<int8_t>();
				break;
			case 1:
				entry.type = Type::Short;
				entry.value = ReadRaw<int16_t>();
				break;
			case 2:
				entry.type = Type::Int;
				entry.value = ReadRaw<int32_t>();
				break;
			case 3:
				entry.type = Type::Float;
				entry.value = ReadRaw<float>();
				break;
			case 4:
				entry.type = Type::String;
				entry.value = ReadString16Raw();
				break;
			case 5: {
				entry.type = Type::Slot;
				ItemStack item;
				item.id = ReadRaw<ItemId>();
				item.count = ReadRaw<ItemAmount>();
				item.data = ReadRaw<ItemDamage>();
				entry.value = item;
				break;
			}
			case 6: {
				entry.type = Type::Position;
				Int32_3 pos;
				pos.x = ReadRaw<int32_t>();
				pos.y = ReadRaw<int32_t>();
				pos.z = ReadRaw<int32_t>();
				entry.value = pos;
				break;
			}
			default:
				throw ProtocolError("unknown entity metadata type " + std::to_string(type));
			}
			entries.push_back(std::move(entry));
			Trace(begin);
		}
	}

	void WriteEntityMetadata(const std::vector<PacketData::EntityMetadata::DataEntry>& entries) {
		using Type = PacketData::EntityMetadata::Type;
		for (const auto& entry : entries) {
			Write(static_cast<uint8_t>((static_cast<uint8_t>(entry.type) << 5) | (entry.index & 0x1F)));
			switch (entry.type) {
			case Type::Byte:
				Write(std::get<int8_t>(entry.value));
				break;
			case Type::Short:
				Write(std::get<int16_t>(entry.value));
				break;
			case Type::Int:
				Write(std::get<int32_t>(entry.value));
				break;
			case Type::Float:
				Write(std::get<float>(entry.value));
				break;
			case Type::String:
				WriteString16(std::get<std::string>(entry.value));
				break;
			case Type::Slot: {
				const ItemStack& item = std::get<ItemStack>(entry.value);
				Write(item.id);
				Write(item.count);
				Write(item.data);
				break;
			}
			case Type::Position: {
				const Int32_3& pos = std::get<Int32_3>(entry.value);
				Write(pos.x);
				Write(pos.y);
				Write(pos.z);
				break;
			}
			}
		}
		Write(static_cast<uint8_t>(0x7F));
	}

private:
	template <typename T>
	T ReadRaw() {
		if constexpr (std::is_enum_v<T>) {
			return static_cast<T>(ReadRaw<std::underlying_type_t<T>>());
		} else if constexpr (std::is_same_v<T, bool>) {
			return ReadRaw<uint8_t>() != 0;
		} else if constexpr (std::is_floating_point_v<T>) {
			static_assert(sizeof(T) == 4 || sizeof(T) == 8, "unsupported floating point type");
			using U = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
			const U bits = ReadRaw<U>();
			T value;
			std::memcpy(&value, &bits, sizeof(T));
			return value;
		} else {
			static_assert(std::is_integral_v<T>, "unsupported type");
			using U = std::make_unsigned_t<T>;
			Require(sizeof(T));
			U bits = 0;
			for (size_t i = 0; i < sizeof(T); ++i)
				bits = static_cast<U>((bits << 8) | readData_[readPos_ + i]);
			readPos_ += sizeof(T);
			return static_cast<T>(bits);
		}
	}

	// Same as ReadString16 but without recording a span (used inside metadata entries)
	std::string ReadString16Raw() {
		const int16_t length = ReadRaw<int16_t>();
		if (length < 0)
			throw ProtocolError("negative string length");
		Require(static_cast<size_t>(length) * 2);
		std::vector<char16_t> units(static_cast<size_t>(length));
		for (auto& u : units)
			u = static_cast<char16_t>(ReadRaw<uint16_t>());
		return Utf16ToUtf8(units);
	}

	void Trace(size_t begin) {
		if (trace_ != nullptr)
			trace_->push_back(Span{ begin, readPos_ });
	}

	static void AppendUtf8(std::string& out, uint32_t cp) {
		if (cp < 0x80) {
			out.push_back(static_cast<char>(cp));
		} else if (cp < 0x800) {
			out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else if (cp < 0x10000) {
			out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else {
			out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	}

	// Lone surrogates are replaced by U+FFFD so the result is always valid UTF-8
	static std::string Utf16ToUtf8(const std::vector<char16_t>& units) {
		std::string out;
		out.reserve(units.size());
		for (size_t i = 0; i < units.size(); ++i) {
			const uint32_t u = units[i];
			if (u >= 0xD800 && u <= 0xDBFF) {
				if (i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
					const uint32_t cp = 0x10000 + ((u - 0xD800) << 10) + (units[i + 1] - 0xDC00);
					AppendUtf8(out, cp);
					++i;
				} else {
					AppendUtf8(out, 0xFFFD);
				}
			} else if (u >= 0xDC00 && u <= 0xDFFF) {
				AppendUtf8(out, 0xFFFD);
			} else {
				AppendUtf8(out, u);
			}
		}
		return out;
	}

	// Invalid sequences are replaced by U+FFFD
	static std::vector<uint32_t> Utf8ToCodepoints(const std::string& s) {
		std::vector<uint32_t> cps;
		cps.reserve(s.size());
		size_t i = 0;
		while (i < s.size()) {
			const uint8_t c = static_cast<uint8_t>(s[i]);
			size_t len = 0;
			uint32_t cp = 0;
			if (c < 0x80) {
				len = 1;
				cp = c;
			} else if ((c & 0xE0) == 0xC0) {
				len = 2;
				cp = c & 0x1F;
			} else if ((c & 0xF0) == 0xE0) {
				len = 3;
				cp = c & 0x0F;
			} else if ((c & 0xF8) == 0xF0) {
				len = 4;
				cp = c & 0x07;
			} else {
				cps.push_back(0xFFFD);
				++i;
				continue;
			}
			if (i + len > s.size()) {
				cps.push_back(0xFFFD);
				break;
			}
			bool valid = true;
			for (size_t k = 1; k < len; ++k) {
				const uint8_t cc = static_cast<uint8_t>(s[i + k]);
				if ((cc & 0xC0) != 0x80) {
					valid = false;
					break;
				}
				cp = (cp << 6) | (cc & 0x3F);
			}
			if (!valid || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
				cps.push_back(0xFFFD);
				++i;
				continue;
			}
			cps.push_back(cp);
			i += len;
		}
		return cps;
	}

	static std::vector<char16_t> Utf8ToUtf16(const std::string& s) {
		std::vector<char16_t> out;
		for (uint32_t cp : Utf8ToCodepoints(s)) {
			if (cp >= 0x10000) {
				cp -= 0x10000;
				out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
				out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
			} else {
				out.push_back(static_cast<char16_t>(cp));
			}
		}
		return out;
	}

	// Java "modified UTF-8": U+0000 is C0 80, supplementary characters are two
	// 3-byte surrogates. Convert to regular UTF-8 for display.
	static std::string ModifiedUtf8ToUtf8(const std::string& s) {
		std::vector<char16_t> units;
		size_t i = 0;
		while (i < s.size()) {
			const uint8_t c = static_cast<uint8_t>(s[i]);
			if (c < 0x80) {
				units.push_back(c);
				++i;
			} else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
				units.push_back(static_cast<char16_t>(((c & 0x1F) << 6) | (static_cast<uint8_t>(s[i + 1]) & 0x3F)));
				i += 2;
			} else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
				units.push_back(static_cast<char16_t>(((c & 0x0F) << 12) |
					((static_cast<uint8_t>(s[i + 1]) & 0x3F) << 6) |
					(static_cast<uint8_t>(s[i + 2]) & 0x3F)));
				i += 3;
			} else {
				units.push_back(0xFFFD);
				++i;
			}
		}
		return Utf16ToUtf8(units);
	}

	static std::string Utf8ToModifiedUtf8(const std::string& s) {
		std::string out;
		for (char16_t u : Utf8ToUtf16(s)) {
			if (u != 0 && u < 0x80) {
				out.push_back(static_cast<char>(u));
			} else if (u < 0x800) {
				out.push_back(static_cast<char>(0xC0 | (u >> 6)));
				out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
			} else {
				out.push_back(static_cast<char>(0xE0 | (u >> 12)));
				out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
			}
		}
		return out;
	}

	const uint8_t* readData_ = nullptr;
	size_t readSize_ = 0;
	size_t readPos_ = 0;
	std::vector<Span>* trace_ = nullptr;
	std::vector<uint8_t> out_;
};
