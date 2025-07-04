#ifndef CASH_ERROR_H
#define CASH_ERROR_H

#include <cash/colors.h>
#include <errno.h>

#define CASH_ERROR(status, fmt, ...)                                        \
    do {                                                                    \
        fprintf(stderr,                                                     \
                RED "cash:  Error: " fmt RESET __VA_OPT__(, ) __VA_ARGS__); \
        if (!repl_mode)                                                     \
            exit(status);                                                   \
    } while (0)

#define CASH_PERROR(status, how, fmt, ...)                                   \
    do {                                                                     \
        fprintf(stderr, RED "cash:  Error: " fmt RESET RED "%s: %s\n" RESET, \
                __VA_ARGS__ __VA_OPT__(, ) how, strerror(errno));            \
        if (!repl_mode)                                                      \
            exit(status);                                                    \
    } while (0)

#define CASH_WARNING(fmt, ...)                                                 \
    do {                                                                       \
        fprintf(stderr, YELLOW "cash: " fmt RESET __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

#endif
