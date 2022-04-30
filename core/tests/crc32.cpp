#include "core/crc32.hpp"
 
static_assert(
    crc32(
        std::array<std::byte, 9>{
            std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
            std::byte{'4'}, std::byte{'5'}, std::byte{'6'},
            std::byte{'7'}, std::byte{'8'}, std::byte{'9'}
        }
    ) == 0xcbf43926
);
