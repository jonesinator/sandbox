add_executable (make-gpt source/gpt.cpp)
target_link_libraries (make-gpt LINK_PUBLIC json)
set_property(TARGET make-gpt PROPERTY CXX_STANDARD 23)
