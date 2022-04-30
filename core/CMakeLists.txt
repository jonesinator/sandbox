add_library (core tests/crc32.cpp)
target_include_directories (core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_property(TARGET core PROPERTY CXX_STANDARD 23)
target_compile_options(core PRIVATE -Wall -Wextra -Wpedantic -Werror)
