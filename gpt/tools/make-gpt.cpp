#include <cctype>
#include <cerrno>
#include <codecvt>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <locale>
#include <span>
#include <string>
#include <system_error>
#include <unistd.h>

#include "gpt/gpt.hpp"
#include "json/json.hpp"

// Wrapper around a std::FILE that ensures the file is closed when the wrapper is destroyed.
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

// Writes the GPT data to the file. The data must be the data generated via the given descriptor.
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

guid parse_guid(const std::string& str) {
    if (str.length() != 36) {
        throw std::invalid_argument("not a UUID!");
    }

    guid dest{};
    int pos = 0;
    auto str_iter = str.begin();

    auto check_iterator = [&str, &str_iter] {
        if (str_iter == str.end()) {
            throw std::invalid_argument("Error parsing GUID!");
        }
    };

    auto read_chunk = [&](unsigned count) {
        for (int end = pos + count; pos < end; ++pos) {
            std::array<char, 2> input;
            check_iterator();
            input.at(0) = *(str_iter++);
            check_iterator();
            input.at(1) = *(str_iter++);
            dest.at(pos) = static_cast<std::uint8_t>(std::stoul(std::string{input.data(), input.size()}, nullptr, 16));
        }

        if (str_iter != str.end()) {
            if (*(str_iter++) != '-') {
                throw std::invalid_argument("Not a dash separator!");
            }
        }
    };

    read_chunk(4);
    read_chunk(2);
    read_chunk(2);
    read_chunk(2);
    read_chunk(6);

    return dest;
}

gpt_descriptor parse_gpt_descriptor(const json_value::json_value_ptr& v) {
    auto obj = std::get<json_value::json_object>(**v);

    gpt_descriptor descriptor;
    descriptor.block_size = static_cast<std::uint64_t>(std::get<double>(**obj.at("block_size")));
    descriptor.number_of_blocks = static_cast<std::uint64_t>(std::get<double>(**obj.at("number_of_blocks")));
    descriptor.disk_guid = parse_guid(std::get<std::string>(**obj.at("disk_guid")));
    for (std::shared_ptr<json_value> p_val : std::get<json_value::json_array>(**obj.at("partitions"))) {
        auto p_obj = std::get<json_value::json_object>(**p_val);

        descriptor.partitions.push_back(
            gpt_partition_entry {
                .partition_type_guid = parse_guid(std::get<std::string>(**p_obj.at("partition_type_guid"))),
                .unique_partition_guid = parse_guid(std::get<std::string>(**p_obj.at("unique_partition_guid"))),
                .starting_lba = static_cast<std::uint64_t>(std::get<double>(**p_obj.at("starting_lba"))),
                .ending_lba = static_cast<std::uint64_t>(std::get<double>(**p_obj.at("ending_lba"))),
                .attributes = static_cast<std::uint64_t>(std::get<double>(**p_obj.at("attributes"))),
                .partition_name{}
            }
        );

        auto convert = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{};
        std::u16string name16 = convert.from_bytes(std::get<std::string>(**p_obj.at("partition_name")));
        if (name16.size() > 36) {
            throw std::invalid_argument("partition name too long!");
        }
        std::copy(name16.begin(), name16.end(), descriptor.partitions.back().partition_name.data());
    }

    return descriptor;
}

int main() {
    try {
        json_value::json_value_ptr config_json = json_value::parse(std::cin);
        gpt_descriptor descriptor = parse_gpt_descriptor(config_json);
        gpt_data data = make_gpt(descriptor);
        write_gpt("gpt.bin", descriptor, data);
    } catch (const std::exception& error) {
        std::cerr << "FATAL ERROR: " << error.what() << std::endl;
        return EXIT_FAILURE;
    }
}
