#include <cstddef>
#include <cstdint>
#include <span>

// Slow but simple method of computing the crc32 of an arbitrary amount of contiguous data.
constexpr std::uint32_t crc32(std::span<const std::byte> data) {
    std::uint32_t crc = 0xffffffff;

    for (std::byte byte : data) {
        for (std::size_t bit = 0; bit < 8; bit++, byte >>= 1) {
            std::uint32_t b = (static_cast<std::uint32_t>(byte) ^ crc) & 1;
            crc >>= 1;
            crc = b ? crc ^ 0xedb88320 : crc;
        }
    }

    return ~crc;
}
