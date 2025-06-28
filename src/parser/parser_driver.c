#include <cash/ast.h>
#include <cash/memory.h>
#include <cash/parser/parser_driver.h>
#include <parse.tab.h>
#include <stdlib.h>

struct Driver make_driver(bool repl_mode) {
    return (struct Driver){.repl_mode = repl_mode,
                           .continue_string = false,
                           .program = make_program(),
                           .token_queue = NULL,
                           .token_capacity = 0,
                           .token_count = 0,
                           .token_head_offset = 0};
}

void free_driver(struct Driver* driver) {
    free_program(&driver->program);
    free(driver->token_queue);
}

void driver_push_token(struct Driver* driver, YYSTYPE token) {
    ADD_LIST(driver, token_count, token_capacity, token_queue, token);
}

YYSTYPE driver_pop_token(struct Driver* driver) {
    driver->token_count--;
    return driver->token_queue[driver->token_head_offset++];
}

void driver_reset_queue(struct Driver* driver) {
    driver->token_count = 0;
    driver->token_head_offset = 0;
}
