#ifndef CASH_UTIL_H_
#define CASH_UTIL_H_

#include <pwd.h>

const struct passwd* get_pw(void);
char* make_new_prompt(const char* username);

#endif  // CASH_UTIL_H_
