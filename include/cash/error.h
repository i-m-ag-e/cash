#ifndef CASH_ERROR_H
#define CASH_ERROR_H

#include <cash/colors.h>
#include <errno.h>

#define cash_error(status, fmt, ...)                                        \
    do {                                                                    \
        fprintf(stderr,                                                     \
                RED "cash:  Error: " fmt RESET __VA_OPT__(, ) __VA_ARGS__); \
        if (!REPL_MODE)                                                     \
            exit(status);                                                   \
    } while (0)

#define cash_perror(status, how, fmt, ...)                                   \
    do {                                                                     \
        fprintf(stderr, RED "cash:  Error: " fmt RESET RED "%s: %s\n" RESET, \
                __VA_ARGS__ __VA_OPT__(, ) how, strerror(errno));            \
        if (!REPL_MODE)                                                      \
            exit(status);                                                    \
    } while (0)

#define cash_warning(fmt, ...)                                                 \
    do {                                                                       \
        fprintf(stderr, YELLOW "cash: " fmt RESET __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#endif
