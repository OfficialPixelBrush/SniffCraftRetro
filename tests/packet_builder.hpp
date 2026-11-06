#pragma once

#include <cstring>
#include <string>
#include <vector>

// Builds Beta 1.7.3 packets byte by byte, independently from anything in protocol/
namespace
{
    struct B
    {
        std::vector<unsigned char> v;

        explicit B(const int id) { v.push_back(static_cast<unsigned char>(id)); }

        B& i8(const int x)
        {
            v.push_back(static_cast<unsigned char>(x & 0xFF));
            return *this;
        }
        B& i16(const int x)
        {
            i8(x >> 8);
            return i8(x);
        }
        B& i32(const long long x)
        {
            i16(static_cast<int>(x >> 16));
            return i16(static_cast<int>(x));
        }
        B& i64(const long long x)
        {
            i32(x >> 32);
            return i32(x);
        }
        B& f32(const float x)
        {
            unsigned int bits;
            std::memcpy(&bits, &x, 4);
            return i32(bits);
        }
        B& f64(const double x)
        {
            unsigned long long bits;
            std::memcpy(&bits, &x, 8);
            return i64(static_cast<long long>(bits));
        }
        B& b(const bool x) { return i8(x ? 1 : 0); }
        // Only ASCII + a few BMP characters, as UTF-16BE code units
        B& s16(const std::vector<int>& units)
        {
            i16(static_cast<int>(units.size()));
            for (int u : units)
            {
                i16(u);
            }
            return *this;
        }
        B& s16(const std::string& ascii)
        {
            i16(static_cast<int>(ascii.size()));
            for (char c : ascii)
            {
                i16(c);
            }
            return *this;
        }
        // Java writeUTF: unsigned short length in bytes + bytes
        B& s8(const std::string& ascii)
        {
            i16(static_cast<int>(ascii.size()));
            for (char c : ascii)
            {
                i8(c);
            }
            return *this;
        }
        B& bytes(const size_t n, const int start = 0)
        {
            for (size_t i = 0; i < n; ++i)
            {
                i8(start + static_cast<int>(i));
            }
            return *this;
        }
    };
}
