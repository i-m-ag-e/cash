#include <cash/ast.h>
#include <cash/error.h>
#include <cash/repl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cash/parser/parser.h"
#include "cash/vm.h"

bool repl_mode = false;

static char* read_file(const char* path);
static void run_file(const char* path);

int main(const int argc, char* argv[]) {
    if (argc == 1) {
        repl_mode = true;
        struct Repl repl = make_repl();
        run_repl(&repl);
        free_repl(&repl);
    } else {
        run_file(argv[1]);
    }
}

static char* read_file(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) {
        CASH_ERROR(EXIT_FAILURE, "could not read file " BOLD WHITE "%s", path);
    }
    const int seek_stat = fseek(file, 0L, SEEK_END);
    if (seek_stat != 0) {
        CASH_PERROR(seek_stat, "fseek",
                    "could not seek end of file " BOLD WHITE "%s", path);
    }

    const long size = ftell(file);
    if (size == -1L) {
        CASH_PERROR(size, "ftell",
                    "could not determine length of file " BOLD WHITE "%s",
                    path);
    }
    rewind(file);

    char* buffer = malloc((size_t)size + 1);
    const size_t read_size = fread(buffer, sizeof(char), (size_t)size, file);

    if (read_size != (size_t)size) {
        if (feof(file))
            CASH_ERROR(EXIT_FAILURE,
                       "unexpected end of file while reading " BOLD WHITE "%s",
                       path);
        if (ferror(file))
            CASH_PERROR(EXIT_FAILURE, "fread",
                        "error while reading file " BOLD WHITE "%s", path);
        CASH_ERROR(EXIT_FAILURE,
                   "unknown error while reading file " BOLD WHITE "%s" RED
                   "(return value from fread() != file_size (%zu != %zu))",
                   path, read_size, size);
    }
    buffer[read_size] = '\0';
    return buffer;
}

static void run_file(const char* path) {
    char* contents = read_file(path);

    struct Vm vm = make_vm();
    struct Parser parser = parser_new(contents, false);
    parse_program(&parser);
    const struct Program prog = parser.program;

    print_program(&prog, 0);

    run_program(&vm, &prog);

    free_vm(&vm);
    free_program(&prog);
    free_parser(&parser);
    free(contents);
}
