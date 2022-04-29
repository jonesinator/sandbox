#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>

#include "json/json.hpp"

using cli_arguments = std::deque<std::string>;
using test_function = std::function<void(const cli_arguments&)>;
using test_map      = std::unordered_map<std::string, test_function>;

test_map& tests() {
    static test_map tm;
    return tm;
}

struct add_test {
    add_test(const std::string& name, test_function tf) {
        tests().insert({name, tf});
    }
};

void validate(const cli_arguments& arguments) {
    std::cout << arguments.at(0) << std::endl;
    std::stringstream ss(arguments.at(0));
    json_value::parse(ss);
}
auto validate_test = add_test("validate", validate);

int main(int argument_count, const char** argument_vector) {
    // Get the command-line arguments into a nicer data structure.
    cli_arguments arguments;
    for (const auto argument : std::span<const char*>(argument_vector, argument_count)) {
        arguments.emplace_back(argument);
    }

    // Discard the first argument, the name of the executable.
    arguments.pop_front();

    // Determine the test name and look up the test in the test map.
    std::string& test_name = arguments.front();
    auto found = tests().find(test_name);
    if (found == tests().end()) {
        std::cerr << "Test \"" << test_name  << "\" not found!" << std::endl;
        return EXIT_FAILURE;
    }
    arguments.pop_front();

    // Execute the test.
    try {
        (found->second)(arguments);
    } catch(const std::exception& error) {
        std::cerr << "Test \"" << test_name  << "\" failed!" << std::endl << error.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cerr << "Test \"" << test_name  << "\" succeeded!" << std::endl;
    return EXIT_SUCCESS;
}
