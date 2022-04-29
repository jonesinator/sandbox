#include <istream>
#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

struct json_value {
    using json_value_ptr = std::shared_ptr<json_value>;
    using json_array = std::vector<json_value_ptr>;
    using json_object = std::unordered_map<std::string, json_value_ptr>;
    using json_data = std::variant<std::monostate, std::string, double, bool, json_array, json_object>;

    static json_value_ptr parse(std::istream& stream);

    constexpr json_data& operator*() noexcept {
	return value;
    }

    constexpr const json_data& operator*() const noexcept {
	return value;
    }

    json_data value;
};
