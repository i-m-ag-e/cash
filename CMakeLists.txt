cmake_minimum_required(VERSION 3.10)
project(cash
    VERSION 0.1
    LANGUAGES C 
)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb -O0")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

add_executable(cash
    src/parser/token.c
    src/parser/lexer.c
    src/parser/parser.c
    src/vm.c
    src/string.c
    src/ast.c
    src/repl.c
    src/main.c
)

target_include_directories(cash PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_include_directories(cash PUBLIC "${CMAKE_BINARY_DIR}/include")
target_link_libraries(cash PUBLIC readline)
target_link_libraries(cash PUBLIC y)
