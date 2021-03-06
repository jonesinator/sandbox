// Utility that can create a file representing a block device with a GPT partition scheme.

#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>

#include "core/crc32.hpp"
#include "gpt/gpt.hpp"

// The data structures are defined as little-endian, and who uses big-endian these days?
static_assert(std::endian::native == std::endian::little, "Must run on a little-endian machine!");

// UEFI Spec 2.8 -- Section 5.2.3 Table 20
struct gpt_protective_mbr_partition_record {
    std::uint8_t boot_indicator;
    std::array<std::uint8_t, 3> starting_chs;
    std::uint8_t os_type;
    std::array<std::uint8_t, 3> ending_chs;
    std::uint32_t starting_lba;
    std::uint32_t size_in_lba;
} __attribute__ ((packed));
static_assert(sizeof(gpt_protective_mbr_partition_record) == 16);

// UEFI Spec 2.8 -- Section 5.2.3 Table 19.
struct gpt_protective_mbr {
    std::array<std::uint8_t, 440> boot_code;
    std::uint32_t unique_mbr_disk_signature;
    std::uint16_t unknown;
    std::array<gpt_protective_mbr_partition_record, 4> partition_record;
    std::array<std::uint8_t, 2> signature;
} __attribute__ ((packed));
static_assert(sizeof(gpt_protective_mbr) == 512);

// UEFI Spec 2.8 -- Section 5.3.2 Table 21.
struct gpt_header {
    std::array<std::uint8_t, 8> signature;
    std::array<std::uint8_t, 4> revision;
    std::uint32_t header_size;
    std::uint32_t header_crc32;
    std::uint32_t reserved;
    lba my_lba;
    lba alternate_lba;
    lba first_usable_lba;
    lba last_usable_lba;
    guid disk_guid;
    lba partition_entry_lba;
    std::uint32_t number_of_partition_entries;
    std::uint32_t size_of_partition_entry;
    std::uint32_t partition_entry_checksum;
} __attribute__ ((packed));
static_assert(sizeof(gpt_header) == 92);

gpt_data make_gpt(const gpt_descriptor& descriptor) {

    //
    // Validate the request.
    //

    if (descriptor.block_size == 0 || descriptor.block_size % 512 != 0) {
        throw std::invalid_argument("Block size must be a non-zero multiple of 512!");
    } else if (descriptor.partitions.empty()) {
        throw std::invalid_argument("Must provide at least one partition!");
    } else if (descriptor.partitions.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Too many partitions! (Like, waaaaaaay too many)");
    }

    // Determine the number of blocks (ceiling) needed to represent all of the GPT metadata.
    std::span partition_span{descriptor.partitions.data(), descriptor.partitions.size()};
    std::size_t partition_entry_blocks = 
        (partition_span.size_bytes() + (descriptor.block_size - 1)) / descriptor.block_size;
    std::size_t gpt_blocks = 3 + (partition_entry_blocks * 2);
    if (descriptor.number_of_blocks < gpt_blocks + 1) {
        throw std::invalid_argument("Number of blocks too small!");
    }

    // Ensure the partition configuration is valid.
    std::uint64_t first_usable_lba = 2 + partition_entry_blocks;
    std::uint64_t last_usable_lba = descriptor.number_of_blocks - partition_entry_blocks - 2;
    for (auto i = descriptor.partitions.begin(); i != descriptor.partitions.end(); ++i) {
        if (i->starting_lba < first_usable_lba) {
            throw std::invalid_argument("Starting LBA less than first usable LBA!");
        } else if (i->ending_lba > last_usable_lba) {
            throw std::invalid_argument("Ending LBA greater than last usable LBA!");
        } else if (i->starting_lba > i->ending_lba) {
            throw std::invalid_argument("Starting LBA is greater than ending LBA!");
        }

        for (auto j = i + 1; j != descriptor.partitions.end(); ++j) {
            if ((i->starting_lba >= j->starting_lba && i->starting_lba <= j->ending_lba) ||
                    (i->ending_lba   >= j->starting_lba && i->ending_lba   <= j->ending_lba)) {
                throw std::invalid_argument("Overlapping partitions!");
            }
        }
    }

    //
    // Construct the needed data structures.
    //

    std::uint32_t partition_entry_checksum = crc32(std::as_bytes(partition_span));

    gpt_protective_mbr mbr_header {
        .boot_code{},
        .unique_mbr_disk_signature = 0,
        .unknown = 0,
        .partition_record = {
            gpt_protective_mbr_partition_record {    
                .boot_indicator = 0,
                .starting_chs{0x00, 0x02, 0x00},
                .os_type = 0xee,
                .ending_chs{0xff, 0xff, 0xff}, // TODO Correct value.
                .starting_lba = 1UL,
                .size_in_lba = descriptor.number_of_blocks > std::numeric_limits<std::uint32_t>::max()
                               ? 0xfffffff
                               : static_cast<std::uint32_t>(descriptor.number_of_blocks - 1),
            },
            gpt_protective_mbr_partition_record {    
                .boot_indicator = 0,
                .starting_chs{},
                .os_type = 0,
                .ending_chs{},
                .starting_lba = 0UL,
                .size_in_lba = 0UL,
            },
            gpt_protective_mbr_partition_record {    
                .boot_indicator = 0,
                .starting_chs{},
                .os_type = 0,
                .ending_chs{},
                .starting_lba = 0UL,
                .size_in_lba = 0UL,
            },
            gpt_protective_mbr_partition_record {    
                .boot_indicator = 0,
                .starting_chs{},
                .os_type = 0,
                .ending_chs{},
                .starting_lba = 0UL,
                .size_in_lba = 0UL,
            }
        },
        .signature = { 0x55, 0xaa }
    };

    gpt_header first_gpt_header {
        .signature = { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'},
        .revision = { 0, 0, 1, 0 },
        .header_size = sizeof(gpt_header),
        .header_crc32 = 0,
        .reserved = 0,
        .my_lba = 1,
        .alternate_lba = descriptor.number_of_blocks - 1,
        .first_usable_lba = first_usable_lba,
        .last_usable_lba = last_usable_lba,
        .disk_guid = descriptor.disk_guid,
        .partition_entry_lba = 2,
        .number_of_partition_entries = static_cast<std::uint32_t>(descriptor.partitions.size()),
        .size_of_partition_entry = sizeof(gpt_partition_entry),
        .partition_entry_checksum = partition_entry_checksum
    };
    first_gpt_header.header_crc32 = crc32(std::as_bytes(std::span{&first_gpt_header, 1}));

    gpt_header second_gpt_header {
        .signature = { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'},
        .revision = { 0, 0, 1, 0 },
        .header_size = sizeof(gpt_header),
        .header_crc32 = 0,
        .reserved = 0,
        .my_lba = descriptor.number_of_blocks - 1,
        .alternate_lba = 1,
        .first_usable_lba = 2 + partition_entry_blocks,
        .last_usable_lba = descriptor.number_of_blocks - partition_entry_blocks - 2,
        .disk_guid = descriptor.disk_guid,
        .partition_entry_lba = descriptor.number_of_blocks - 1 - partition_entry_blocks,
        .number_of_partition_entries = static_cast<std::uint32_t>(descriptor.partitions.size()),
        .size_of_partition_entry = sizeof(gpt_partition_entry),
        .partition_entry_checksum = partition_entry_checksum
    };
    second_gpt_header.header_crc32 = crc32(std::as_bytes(std::span{&second_gpt_header, 1}));

    //
    // Construct buffers to hold the raw GPT data.
    //

    gpt_data data {
        .header = std::vector<std::byte>((2 + partition_entry_blocks) * descriptor.block_size),
        .footer = std::vector<std::byte>((1 + partition_entry_blocks) * descriptor.block_size)
    };

    std::memcpy(data.header.data() + (0 * descriptor.block_size), &mbr_header, sizeof(mbr_header));
    std::memcpy(data.header.data() + (1 * descriptor.block_size), &first_gpt_header, sizeof(gpt_header));
    std::memcpy(data.header.data() + (2 * descriptor.block_size), partition_span.data(), partition_span.size_bytes());

    std::memcpy(data.footer.data() + (0 * descriptor.block_size), partition_span.data(), partition_span.size_bytes());
    std::memcpy(data.footer.data() + (1 * descriptor.block_size), &second_gpt_header, sizeof(gpt_header));

    return data;
}
