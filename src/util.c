#include <cash/ast.h>
#include <cash/colors.h>
#include <cash/error.h>
#include <cash/parser/parser.h>
#include <cash/string.h>
#include <cash/util.h>
#include <cash/vm.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char **environ;
extern bool repl_mode;

const struct passwd *get_pw(void) {
    const struct passwd *pw = getpwuid(getuid());
    if (pw == NULL) {
        CASH_PERROR(EXIT_FAILURE, "getpwuid",
                    "could not get user entry (fatal error)\n%s", "");
        exit(EXIT_FAILURE);
    }
    return pw;
}

char *get_cwd(void) {
    char *cwd = getcwd(NULL, 0);
    if (cwd == NULL) {
        CASH_PERROR(EXIT_FAILURE, "getcwd",
                    "could not get current working directory%s", "");
        exit(EXIT_FAILURE);
    }
    return cwd;
}

char *make_new_prompt(const char *username) {
    char *cwd = get_cwd();
    const size_t size_needed =
        strlen(username) + strlen(cwd) + COLOR_ATTR_LEN * 4 + COLOR_LEN * 2 + 3;
    char *buf = malloc(size_needed * sizeof(char));
    sprintf(buf, BOLD GREEN "%s" RESET ":" BOLD BLUE "%s" RESET "$ ", username,
            cwd);

    free(cwd);
    return buf;
}

// I have realised now that this is literally the most redundant function in the
// repo I shall ponder on my ignorance and remove it later
char *strndup_null_terminated(const char *source, int len) {
    char *buf = malloc((len + 1) * sizeof(char));
    strncpy(buf, source, len);
    buf[len] = '\0';
    return buf;
}

// checks for positive integers only
int is_number(const char *str) {
    // using strtol
    if (str == NULL || *str == '\0') {
        return -1;
    }
    char *endptr;
    long val = strtol(str, &endptr, 10);

    if (*endptr != '\0' || val < 0 || val > INT_MAX) {
        return -1;
    }

    return (int)val;
}

struct String number_to_string(int number) {
    const int length = snprintf(NULL, 0, "%d", number);
    char *buf = malloc((length + 1) * sizeof(char));
    if (!buf) {
        CASH_ERROR(EXIT_FAILURE, "Memory allocation failed%s\n", "");
        exit(EXIT_FAILURE);
    }
    snprintf(buf, length + 1, "%d", number);
    return (struct String){.string = buf, .length = length};
}

char *read_all_stdin(void) {
    size_t size = 0;
    size_t capacity = 1024;
    char *buffer = malloc(capacity);
    if (!buffer) {
        CASH_ERROR(EXIT_FAILURE,
                   "could not allocate memory for stdin buffer\n%s", "");
        exit(EXIT_FAILURE);
    }

    size_t n;
    while ((n = fread(buffer + size, 1, capacity - size - 1, stdin)) > 0) {
        size += n;
        if (size == capacity - 1) {
            capacity *= 2;
            char *new_buffer = realloc(buffer, capacity);
            if (!new_buffer) {
                CASH_ERROR(EXIT_FAILURE,
                           "could not allocate memory for stdin buffer\n%s",
                           "");
                exit(EXIT_FAILURE);
            }
            buffer = new_buffer;
        }
    }

    buffer[size] = '\0';
    return buffer;
}

char *read_file(const char *path) {
    FILE *file = fopen(path, "r");

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

    char *buffer = malloc((size_t)size + 1);
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

void run_string(const char *text, int argc, char **argv) {
    struct Vm vm = make_vm(argc, argv);
    struct Parser parser = parser_new(text, false);
    parse_program(&parser);
    const struct Program prog = parser.program;

#ifndef NDEBUG
    print_program(&prog, 0);
#endif

    run_program(&vm, &prog);

    free_vm(&vm);
    free_program(&prog);
    free_parser(&parser);
}

void run_file(const char *path, int argc, char **argv) {
    char *contents = read_file(path);
    run_string(contents, argc, argv);
    free(contents);
}
