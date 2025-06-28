#ifndef CASH_PARSER_LEXER_H
#define CASH_PARSER_LEXER_H

#include <cash/ast.h>
#include <cash/parser/token.h>

struct Lexer {
    bool repl_mode;
    bool error;
    const char* input;
    int token_start;
    int position;

    int first_line;
    int first_column;
    int last_line;
    int last_column;

    struct Token* token_queue;
    int token_queue_head;
    int token_queue_size;
    int token_queue_capacity;

    bool continue_string;
    struct ShellString current_string;
};

struct Lexer lexer_new(const char* input, bool repl_mode);
struct Token lexer_next_token(struct Lexer* lexer);
void lexer_lex_full(struct Lexer* lexer);
void reset_lexer(const char* input, struct Lexer* lexer);
void free_lexer(struct Lexer* lexer);

#endif  // CASH_PARSER_LEXER_H
