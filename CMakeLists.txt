cmake_minimum_required(VERSION 3.10)
project(cash
    VERSION 0.1
    LANGUAGES C 
)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb -O0")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

FLEX_TARGET(
    Lexer
    "${CMAKE_SOURCE_DIR}/src/parser/lexer.l"
    "${CMAKE_BINARY_DIR}/src/lexer.yy.c"
    DEFINES_FILE "${CMAKE_BINARY_DIR}/include/lexer.yy.h" 
)

BISON_TARGET(
    Parser
    "${CMAKE_SOURCE_DIR}/src/parser/parse.y"
    "${CMAKE_BINARY_DIR}/src/parse.tab.c"
    COMPILE_FLAGS "--header=${CMAKE_BINARY_DIR}/include/parse.tab.h -Wcounterexamples"
)

add_executable(cash
    # ${FLEX_Lexer_OUTPUTS}
    # ${BISON_Parser_OUTPUTS}
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
