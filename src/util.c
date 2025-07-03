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
extern bool REPL_MODE;

const struct passwd *get_pw(void) {
    const struct passwd *pw = getpwuid(getuid());
    if (pw == NULL) {
        cash_perror(EXIT_FAILURE, "getpwuid",
                    "could not get user entry (fatal error)\n%s", "");
        exit(EXIT_FAILURE);
    }
    return pw;
}

char *make_new_prompt(const char *username) {
    char *cwd = getcwd(NULL, 0);

    const size_t size_needed = strlen(username) + strlen(cwd) +
                               (COLOR_ATTR_LEN * 4) + (COLOR_LEN * 2) + 3;
    char *buf = malloc(size_needed * sizeof(char));
    sprintf(buf, BOLD GREEN "%s" RESET ":" BOLD BLUE "%s" RESET "$ ", username,
            cwd);

    free(cwd);
    return buf;
}
