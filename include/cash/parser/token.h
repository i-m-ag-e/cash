#ifndef CASH_PARSER_TOKEN_H
#define CASH_PARSER_TOKEN_H

#include <cash/ast.h>

enum TokenType {
    TOKEN_WORD,
    TOKEN_NUMBER,

    TOKEN_LPAREN,
    TOKEN_RPAREN,

    TOKEN_LINE_BREAK,
    TOKEN_SEMICOLON,

    TOKEN_AMP,

    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,

    TOKEN_PIPE,
    TOKEN_REDIRECT,

    TOKEN_ERROR,
    TOKEN_EOF,
};

struct Token {
    enum TokenType type;
    int first_line;
    int first_column;
    int last_line;
    int last_column;

    int lexeme_length;
    const char* lexeme;

    union {
        long number;
        struct ShellString word;
        struct {
            enum RedirectionType type;
            int left;
            int right;
        } redirection;
    } value;
};

const char* token_type_to_string(enum TokenType type);
const char* dump_token_type(enum TokenType type);

#ifndef NDEBUG
void dump_token(const struct Token* token);
#endif

#endif  // CASH_PARSER_TOKEN_H
