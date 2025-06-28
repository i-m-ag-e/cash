#include <cash/ast.h>
#include <cash/error.h>
#include <cash/parser/lexer.h>
#include <cash/parser/token.h>
// #include <cash/parser/parser_driver.h>
#include <cash/repl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cash/parser/parser.h"
#include "cash/vm.h"

// #include "parse.tab.h"
// #include "lexer.yy.h"

bool REPL_MODE = false;

static char* read_file(const char* path);
static void run_file(const char* path);

// int cash_lex(YYSTYPE* yylval, YYLTYPE* yylloc, yyscan_t yyscanner,
//              struct Driver* driver);
// int yyparse(yyscan_t yyscanner, struct Driver* program);
// int yylex_destroy(yyscan_t scanner);

int main(int argc, char* argv[]) {
    if (argc == 1) {
        REPL_MODE = true;
        run_repl();
    } else {
        run_file(argv[1]);
    }
    // yyscan_t scanner;
    // yylex_init(&scanner);

    // struct Driver driver;
    // yyparse(&scanner, &driver);
    // print_program(&driver.program);
    // free_program(&driver.program);

    // yylex_destroy(&scanner);
}

// int main(int argc, char* argv[]);

static char* read_file(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) {
        cash_error(EXIT_FAILURE, "could not read file " BOLD WHITE "%s", path);
    }
    int seek_stat = fseek(file, 0L, SEEK_END);
    if (seek_stat != 0) {
        cash_perror(seek_stat, "fseek",
                    "could not seek end of file " BOLD WHITE "%s", path);
    }

    long size = ftell(file);
    if (size == -1L) {
        cash_perror(size, "ftell",
                    "could not determine length of file " BOLD WHITE "%s",
                    path);
    }
    rewind(file);

    char* buffer = malloc((size_t)size + 1);
    size_t read_size = fread((void*)buffer, sizeof(char), (size_t)size, file);

    if (read_size != (size_t)size) {
        if (feof(file))
            cash_error(EXIT_FAILURE,
                       "unexpected end of file while reading " BOLD WHITE "%s",
                       path);
        if (ferror(file))
            cash_perror(EXIT_FAILURE, "fread",
                        "error while reading file " BOLD WHITE "%s", path);
        cash_error(EXIT_FAILURE,
                   "unknown error while reading file " BOLD WHITE "%s" RED
                   "(return value from fread() != file_size (%zu != %zu))",
                   path, read_size, size);
    }
    buffer[read_size] = '\0';
    return buffer;
}

static void run_file(const char* path) {
    char* contents = read_file(path);

    struct Parser parser = parser_new(contents, false);
    parse_program(&parser);
    struct Program prog = parser.program;

    print_program(&prog);

    run_program(&prog);

    free_program(&prog);
    free_parser(&parser);
    free(contents);
}
