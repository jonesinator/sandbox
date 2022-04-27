#include <istream>
#include <map>
#include <memory>
#include <variant>
#include <vector>

struct json_value {
    using json_value_ptr = std::shared_ptr<json_value>;
    using json_array = std::vector<json_value_ptr>;
    using json_object = std::map<std::string, json_value_ptr>;
    using json_data = std::variant<std::monostate, std::string, double, bool, json_array, json_object>;

    static json_value_ptr parse(std::istream& stream);

    json_data value;
};
