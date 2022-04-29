add_library (json source/json.cpp)
target_include_directories (json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_property(TARGET json PROPERTY CXX_STANDARD 23)
target_compile_options(json PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable (json-test tests/json_test.cpp)
target_link_libraries (json-test LINK_PUBLIC json)
set_property(TARGET json-test PROPERTY CXX_STANDARD 23)
target_compile_options(json-test PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_test(NAME array_with_null COMMAND json-test validate [null])

add_test(NAME array_with_no_terminator COMMAND json-test validate [null)
set_property(TEST array_with_no_terminator PROPERTY WILL_FAIL TRUE)

add_test(NAME not_a_json_value COMMAND json-test validate zoom)
set_property(TEST not_a_json_value PROPERTY WILL_FAIL TRUE)
