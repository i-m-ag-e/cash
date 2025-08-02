#ifndef CASH_UTIL_H
#define CASH_UTIL_H

#include <cash/string.h>
#include <pwd.h>

#ifndef NDEBUG

static const char* debug_files[] = {"repl.c", "parser.c", "lexer.c", "vm.c"};
static const int debug_files_len = sizeof(debug_files) / sizeof(debug_files[0]);

#define CASH_DEBUG(...)                                            \
    do {                                                           \
        for (int _____i = 0; _____i < debug_files_len; ++_____i) { \
            if (debug_files[_____i][0] == '*' ||                   \
                strcmp(__FILE_NAME__, debug_files[_____i]) == 0) { \
                fprintf(stderr, __VA_ARGS__);                      \
                break;                                             \
            }                                                      \
        }                                                          \
    } while (0)

#define CASH_DEBUG_EXPR(expr)                                     \
    do {                                                          \
        for (int ____i = 0; ____i < debug_files_len; ++____i) {   \
            if (debug_files[____i][0] == '*' ||                   \
                strcmp(__FILE_NAME__, debug_files[____i]) == 0) { \
                (expr);                                           \
                break;                                            \
            }                                                     \
        }                                                         \
    } while (0)
#else
#define CASH_DEBUG(...) ((void)0)
#define CASH_DEBUG_EXPR(expr) \
    do {                      \
    } while (0)
#endif

struct String read_all_fd(int fd);
char* read_file(const char* path);

void run_string(const char* text, int argc, char** argv);
void run_file(const char* path, int argc, char** argv);

const struct passwd* get_pw(void);
char* make_new_prompt(const char* username);

char* get_cwd(void);

char* strndup_null_terminated(const char* source, int len);
int is_number(const char* str);
struct String number_to_string(int number);

#endif  // CASH_UTIL_H
