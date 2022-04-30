#include <array>
#include <bit>
#include <cstdint>
#include <vector>

// Convenience type definitions to make the code more readable.
using guid = std::array<std::uint8_t, 16>;
using lba = std::uint64_t;

// UEFI Spec 2.8 -- Section 5.3.2 Table 22.
struct gpt_partition_entry {
    guid partition_type_guid;
    guid unique_partition_guid;
    lba starting_lba;
    lba ending_lba;
    std::uint64_t attributes;
    std::array<char16_t, 36> partition_name;
} __attribute__ ((packed));
static_assert(sizeof(gpt_partition_entry) == 128);

// Describes a GPT disk to be built.
struct gpt_descriptor {
    std::size_t block_size;
    std::size_t number_of_blocks;
    guid disk_guid;
    std::vector<gpt_partition_entry> partitions;
};

// Describes the raw bytes of a GPT device.
struct gpt_data {
    std::vector<std::byte> header;
    std::vector<std::byte> footer;
};

// Constructs the raw data for a GPT device given certain device attributes and partition layout.
gpt_data make_gpt(const gpt_descriptor& descriptor);
