// Utility that can create a file representing a block device with a GPT partition scheme.

#include <array>
#include <bit>
#include <cctype>
#include <cerrno>
#include <codecvt>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <locale>
#include <span>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

static_assert(std::endian::native == std::endian::little, "Must run on a little-endian machine!");

using guid = std::array<std::uint8_t, 16>;
using lba = std::uint64_t;

//
// Data Structures as defined by the UEFI Spec 2.8 in Section 5
//

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

//
// Utilities
//

// Slow but succinct method of computing the crc32 of an arbitrary amount of data.
constexpr std::uint32_t crc32(std::span<const std::byte> data) {
    std::uint32_t crc = 0xffffffff;

    for (std::byte byte : data) {
        for (std::size_t bit = 0; bit < 8; bit++, byte >>= 1) {
            std::uint32_t b = (static_cast<std::uint32_t>(byte) ^ crc) & 1;
            crc >>= 1;
            crc = b ? crc ^ 0xEDB88320 : crc;
        }
    }

    return ~crc;
}
 
static_assert(
    crc32(
        std::array<std::byte, 9>{
            std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
            std::byte{'4'}, std::byte{'5'}, std::byte{'6'},
            std::byte{'7'}, std::byte{'8'}, std::byte{'9'}
        }
    ) == 0xcbf43926
);

//
// Main Algorithm (Construct GPT Data)
//

struct gpt_descriptor {
    std::size_t block_size;
    std::size_t number_of_blocks;
    guid disk_guid;
    std::vector<gpt_partition_entry> partitions;
};

struct gpt_data {
    std::vector<std::byte> header;
    std::vector<std::byte> footer;
};

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

//
// System Driver (Linux)
//

struct simple_file {
    std::FILE* file_handle;

    simple_file(const std::string& path, const std::string& mode) {
        file_handle = std::fopen(path.c_str(), mode.c_str());
        if (!file_handle) {
            throw std::system_error(errno, std::generic_category(), "error opening file");
        }
    }

    ~simple_file() {
        if (std::fclose(file_handle) == EOF) {
            std::perror("error closing file");
        }
    }
};

void write_gpt(const std::string& path, const gpt_descriptor& descriptor, const gpt_data& data) {
    simple_file stream(path.c_str(), "wb");
    std::span header_span{data.header.data(), data.header.size()};
    std::span footer_span{data.footer.data(), data.footer.size()};
    std::uint64_t disk_bytes = descriptor.block_size * descriptor.number_of_blocks;
    int file_descriptor = ::fileno(stream.file_handle);

    if (file_descriptor == -1) {
        throw std::system_error(errno, std::generic_category(), "error getting file descriptor");
    }

    if (::ftruncate(file_descriptor, disk_bytes) == -1) {
        throw std::system_error(errno, std::generic_category(), "error truncating file");
    }

    if (std::fseek(stream.file_handle, 0, SEEK_SET) != 0) {
        throw std::system_error(errno, std::generic_category(), "error seeking to beginning of file");
    }

    if (std::fwrite(std::as_bytes(header_span).data(), header_span.size_bytes(), 1, stream.file_handle) != 1) {
        throw std::system_error(errno, std::generic_category(), "error writing header");
    }

    if (std::fseek(stream.file_handle, disk_bytes - footer_span.size_bytes(), SEEK_SET) != 0) {
        throw std::system_error(errno, std::generic_category(), "error seeking to footer start");
    }

    if (std::fwrite(std::as_bytes(footer_span).data(), footer_span.size_bytes(), 1, stream.file_handle) != 1) {
        throw std::system_error(errno, std::generic_category(), "error writing footer");
    }
}

//
// I/O Driver
//

std::istream& operator>>(std::istream& stream, guid& dest) {
    int pos = 0;

    auto read_chunk = [&](unsigned count, bool last = false){
        for (int end = pos + count; pos < end; ++pos) {
            std::array<char, 2> input;
            input.at(0) = stream.get();
            input.at(1) = stream.get();
            dest.at(pos) = static_cast<std::uint8_t>(std::stoul(std::string{input.data(), input.size()}, nullptr, 16));
        }

        if (!last) {
            if (char separator = stream.get(); separator != '-') {
                throw std::invalid_argument("Not a dash separator!");
            }
        }
    };

    stream >> std::ws;
    read_chunk(4);
    read_chunk(2);
    read_chunk(2);
    read_chunk(2);
    read_chunk(6, true);

    return stream;
}

std::istream& operator>>(std::istream& stream, gpt_partition_entry& dest) {
    stream >> dest.partition_type_guid;
    stream >> dest.unique_partition_guid;

    // Work around some packed struct weirdness.
    std::uint64_t temp;
    stream >> temp; dest.starting_lba = temp;
    stream >> temp; dest.ending_lba = temp;
    stream >> temp; dest.attributes = temp;
    stream >> std::ws;

    // Read the rest of the line as the utf-8 encoded string representing the partition name. Reading to the end of the
    // line allows everything other than newlines to appear in the name. Newlines in the name are probably technically
    // allowed since it's arbitrary utf-16, but we don't have to allow everything.
    std::string partition_name_u8;
    std::getline(stream, partition_name_u8);

    auto convert = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{};
    std::u16string partition_name_u16 = convert.from_bytes(partition_name_u8);

    if (partition_name_u16.size() > dest.partition_name.size()) {
        throw std::runtime_error("Partition name too long!");
    }

    for (auto iter = dest.partition_name.begin(); char16_t c : partition_name_u16) {
        *(iter++) = c;
    }

    return stream;
}

std::istream& operator>>(std::istream& stream, gpt_descriptor& dest) {
    std::size_t num_partitions;

    stream >> dest.block_size >> dest.number_of_blocks >> dest.disk_guid >> num_partitions;

    for (std::size_t i = 0; i < num_partitions; ++i) {
        gpt_partition_entry entry {
            .partition_type_guid{0},
            .unique_partition_guid{0},
            .starting_lba{0},
            .ending_lba{0},
            .attributes{0},
            .partition_name{0}
        };
        stream >> entry;
        dest.partitions.push_back(entry);
    }

    return stream;
}

int main() {
    try {
        std::string destination;
        gpt_descriptor descriptor;
        std::cin >> destination >> descriptor;
        auto data = make_gpt(descriptor);
        write_gpt(destination, descriptor, data);
    } catch (const std::exception& error) {
        std::cerr << "FATAL ERROR: " << error.what() << std::endl;
        return EXIT_FAILURE;
    }
}
