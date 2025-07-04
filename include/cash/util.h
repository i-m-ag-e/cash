#ifndef CASH_UTIL_H
#define CASH_UTIL_H

#include <pwd.h>
#include <stdlib.h>
#include <string.h>

const struct passwd* get_pw(void);
char* make_new_prompt(const char* username);

char* get_cwd(void);

char* strndup_null_terminated(const char* source, int len);

#endif  // CASH_UTIL_H
