#include <codecvt>
#include <locale>
#include <stdexcept>

#include "json/json.hpp"

static void read_constant(const std::string& expected, std::istream& stream) {
    for (const auto expected_c : expected) {
        if (const char8_t actual_c = stream.get(); actual_c != expected_c) {
            throw std::runtime_error("parse error! good luck!");
        }
    }
}

static void read_string(std::string& str, std::istream& stream) {
    while (true) {
        if (char8_t c = stream.get(); stream.eof() || std::iscntrl(c)) {
            throw std::runtime_error("parse error! good luck!");
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
                std::string hex_codepoint;
                for (int i = 0; i < 4; ++i) {
                    if (char8_t c = stream.get(); std::isxdigit(c)) {
                        hex_codepoint += c;
                    }

                    throw std::runtime_error("parse error! good luck!");
                }

                unsigned long codepoint = std::stoul(hex_codepoint, nullptr, 16);
                str += std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t>{}.to_bytes(codepoint);
                break;
            }
            default: throw std::runtime_error("parse error! good luck!");
            }
        } else {
            str += c;
        }
    }
}

static void read_array(json_value::json_array& arr, std::istream& stream) {
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
            throw std::runtime_error("parse error! good luck!");
        }
    }
}

static void read_object(json_value::json_object& obj, std::istream& stream) {
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
            throw std::runtime_error("parse error! good luck!");
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
    } else if ((first = '-') || (first >= '0' && first <= '9')) {
        stream >> v->value.emplace<double>();
    } else {
        throw std::runtime_error("parse error! good luck!");
    }

    return v;
}
