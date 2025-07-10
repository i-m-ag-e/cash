#ifndef CASH_PARSER_PARSER_H
#define CASH_PARSER_PARSER_H

#include <cash/ast.h>
#include <cash/parser/lexer.h>
#include <cash/parser/token.h>

struct Parser {
    struct Lexer* lexer;
    struct Token current_token;
    struct Token next_token;

    const char* input;
    struct Program program;
    bool error;
    bool is_subparser;
};

struct Parser parser_new(const char* input, bool repl_mode);
void reset_parser(const char* input, struct Parser* parser);
void free_parser(const struct Parser* parser);

bool parse_program(struct Parser* parser);

#endif  // CASH_PARSER_PARSER_H
