#ifndef CASH_UTIL_H
#define CASH_UTIL_H

#include <cash/string.h>
#include <pwd.h>

#ifndef NDEBUG
#define CASH_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define CASH_DEBUG(...) ((void)0)
#endif

char* read_all_stdin(void);
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
