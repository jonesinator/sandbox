#include <codecvt>
#include <locale>
#include <stdexcept>

#include "json/json.hpp"

static void read_constant(const std::string& expected, std::istream& stream) {
    for (const auto expected_c : expected) {
        if (const char8_t actual_c = stream.get(); actual_c != expected_c) {
            throw std::runtime_error("unable to read constant");
        }
    }
}

static char16_t read_escaped_unicode_hex(std::istream& stream) {
    std::string hex_str;

    for (int j = 0; j < 4; ++j) {
        if (char8_t c = stream.get(); std::isxdigit(c)) {
            hex_str += c;
        } else {
            throw std::runtime_error("got non-hex digit or eof in escaped unicode character");
        }
    }

    return static_cast<char16_t>(std::stoul(hex_str, nullptr, 16));
}

static void read_string(std::string& str, std::istream& stream) {
    while (true) {
        if (char8_t c = stream.get(); stream.eof() || std::iscntrl(c)) {
            throw std::runtime_error("got eof or control character while reading string");
        } else if (c == '"') {
            break;
        } else if (c == '\\') {
            switch(stream.get()) {
            case '"':  str += '"';  break;
            case '\\': str += '\\'; break;
            case '/':  str += '/';  break;
            case 'b':  str += '\b'; break;
            case 'f':  str += '\f'; break;
            case 'n':  str += '\n'; break;
            case 'r':  str += '\r'; break;
            case 't':  str += '\t'; break;
            case 'u': {
                std::u16string u16char;
                u16char.push_back(read_escaped_unicode_hex(stream));
                if (u16char[0] >= 0xd800 && u16char[0] <= 0xdbff) {
                    read_constant("\\u", stream);
                    u16char.push_back(read_escaped_unicode_hex(stream));
                }

                str += std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes(u16char);
                break;
            }
            default: throw std::runtime_error("illegal escape character or eof");
            }
        } else {
            str += c;
        }
    }
}

static void read_array(json_value::json_array& arr, std::istream& stream) {
    // Check for empty.
    stream >> std::ws;
    if (stream.peek() == ']') {
        return;
    }

    while (true) {
        // Read the value.
        stream >> std::ws;
        arr.push_back(json_value::parse(stream));
        stream >> std::ws;

        // Determine if there are more values to read.
        if (char8_t next = stream.get(); next == ',') {
            continue;
        } else if (next == ']') {
            break;
        } else {
            throw std::runtime_error("error reading array");
        }
    }
}

static void read_object(json_value::json_object& obj, std::istream& stream) {
    // Check for empty.
    stream >> std::ws;
    if (stream.peek() == '}') {
        return;
    }

    while (true) {
        // Read the key.
        stream >> std::ws;
        read_constant("\"", stream);
        std::string key;
        read_string(key, stream);

        // Read the separator.
        stream >> std::ws;
        read_constant(":", stream);

        // Read the value.
        stream >> std::ws;
        obj.insert({key, json_value::parse(stream)});
        stream >> std::ws;

        // Determine if there are more values to read.
        if (char8_t next = stream.get(); next == ',') {
            continue;
        } else if (next == '}') {
            break;
        } else {
            throw std::runtime_error("error reading object");
        }
    }
}

json_value::json_value_ptr json_value::parse(std::istream& stream) {
    json_value::json_value_ptr v = std::make_shared<json_value>();

    stream >> std::ws;
    if (char8_t first = stream.peek(); first == '{') {
        stream.ignore();
        read_object(v->value.emplace<json_value::json_object>(), stream);
    } else if (first == '[') {
        stream.ignore();
        read_array(v->value.emplace<json_value::json_array>(), stream);
    } else if (first == '"') {
        stream.ignore();
        read_string(v->value.emplace<std::string>(), stream);
    } else if (first == 't') {
        stream.ignore();
        read_constant("rue", stream);
        v->value = true;
    } else if (first == 'f') {
        stream.ignore();
        read_constant("alse", stream);
        v->value = false;
    } else if (first == 'n') {
        stream.ignore();
        read_constant("ull", stream);
        v->value = std::monostate{};
    } else if (first == '-' || std::isdigit(first)) {
        stream >> v->value.emplace<double>();
    } else {
        throw std::runtime_error("illegal first character");
    }

    return v;
}
