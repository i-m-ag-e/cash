#ifndef CASH_PARSER_PARSER_DRIVER_H_
#define CASH_PARSER_PARSER_DRIVER_H_

#include <cash/ast.h>
#include <parse.tab.h>
//
#include <lexer.yy.h>

struct Driver {
    bool repl_mode;
    bool continue_string;

    YYSTYPE* token_queue;
    int token_capacity;
    int token_count;
    int token_head_offset;

    struct Program program;

    const char* file_name;
};

struct Driver make_driver(bool repl_mode);
void free_driver(struct Driver* driver);

void driver_push_token(struct Driver* driver, YYSTYPE token);
YYSTYPE driver_pop_token(struct Driver* driver);
void driver_reset_queue(struct Driver* driver);

YYSTYPE next_token(struct Driver* driver);
YYSTYPE lex_repl_mode(struct Driver* driver);

#endif  // CASH_PARSER_PARSER_DRIVER_H_
