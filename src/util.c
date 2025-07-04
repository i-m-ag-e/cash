#include <cash/colors.h>
#include <cash/error.h>
#include <cash/util.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char *const *environ;
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
                    "could not get current working directory");
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

char *strndup_null_terminated(const char *source, int len) {
    char *buf = malloc((len + 1) * sizeof(char));
    strncpy(buf, source, len);
    buf[len] = '\0';
    return buf;
}
