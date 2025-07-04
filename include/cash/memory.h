#ifndef CASH_MEMORY_H
#define CASH_MEMORY_H

#include <cash/error.h>

#define ADD_LIST(list, count, cap, items, item, item_type)                    \
    do {                                                                      \
        if ((list)->count >= (list)->cap) {                                   \
            (list)->cap = (list)->cap == 0 ? 1 : (list)->cap * 2;             \
            item_type* new_list =                                             \
                realloc((list)->items, (list)->cap * sizeof(*(list)->items)); \
            if (!new_list) {                                                  \
                CASH_ERROR(EXIT_FAILURE,                                      \
                           "Failed to allocate memory for list%s\n", "");     \
                exit(EXIT_FAILURE);                                           \
            }                                                                 \
            (list)->items = new_list;                                         \
        }                                                                     \
        (list)->items[(list)->count] = (item);                                \
        (list)->count++;                                                      \
    } while (0)

#endif  // CASH_MEMORY_H
