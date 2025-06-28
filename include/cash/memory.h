#ifndef CASH_MEMORY_H
#define CASH_MEMORY_H

#define ADD_LIST(list, count, cap, items, item)                               \
    do {                                                                      \
        if ((list)->count >= (list)->cap) {                                   \
            (list)->cap = (list)->cap == 0 ? 1 : (list)->cap * 2;             \
            (list)->items =                                                   \
                realloc((list)->items, (list)->cap * sizeof(*(list)->items)); \
        }                                                                     \
        (list)->items[(list)->count] = (item);                                \
        (list)->count++;                                                      \
    } while (0)

#endif  // CASH_MEMORY_H
