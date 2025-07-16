#ifndef NDEBUG

#include <cash/colors.h>
#include <cash/parser/token.h>
#include <stdio.h>

#define CASE_TT(tt) \
    case tt:        \
        return #tt;

#define TO_STRING_TT(tt, repr) \
    case tt:                   \
        return repr

const char* token_type_to_string(enum TokenType type) {
    switch (type) {
        TO_STRING_TT(TOKEN_WORD, "<string>");
        TO_STRING_TT(TOKEN_NUMBER, "<number>");
        TO_STRING_TT(TOKEN_LINE_BREAK, "\\n");
        TO_STRING_TT(TOKEN_SEMICOLON, ";");

        TO_STRING_TT(TOKEN_AMP, "&");
        TO_STRING_TT(TOKEN_AND, "&&");
        TO_STRING_TT(TOKEN_OR, "||");
        TO_STRING_TT(TOKEN_NOT, "!");

        TO_STRING_TT(TOKEN_LPAREN, "(");
        TO_STRING_TT(TOKEN_RPAREN, ")");
        TO_STRING_TT(TOKEN_PIPE, "|");
        TO_STRING_TT(TOKEN_REDIRECT, ">");
        TO_STRING_TT(TOKEN_ERROR, "<ERROR>");
        TO_STRING_TT(TOKEN_EOF, "<EOF>");
    }
    return "";
}

const char* dump_token_type(enum TokenType type) {
    switch (type) {
        CASE_TT(TOKEN_WORD);
        CASE_TT(TOKEN_NUMBER);

        CASE_TT(TOKEN_LINE_BREAK);
        CASE_TT(TOKEN_SEMICOLON);
        CASE_TT(TOKEN_LPAREN);
        CASE_TT(TOKEN_RPAREN);

        CASE_TT(TOKEN_AMP);

        CASE_TT(TOKEN_AND);
        CASE_TT(TOKEN_OR);
        CASE_TT(TOKEN_NOT);

        CASE_TT(TOKEN_PIPE);
        CASE_TT(TOKEN_REDIRECT);

        CASE_TT(TOKEN_ERROR);
        CASE_TT(TOKEN_EOF);
    }
    return "";
}

void dump_token(const struct Token* token) {
    printf(YELLOW "<" BOLD CYAN "%s" YELLOW "; " GREEN "%d" RESET "-" GREEN
                  "%d" YELLOW ":" BLUE "%d" RESET "-" BLUE "%d" YELLOW
                  "; " MAGENTA "\"%.*s\"" YELLOW ">\n" RESET,
           dump_token_type(token->type), token->first_line, token->last_line,
           token->first_column, token->last_column,
           token->type == TOKEN_LINE_BREAK ? 2 : token->lexeme_length,
           token->type == TOKEN_LINE_BREAK ? "\\n" : token->lexeme);
}

#endif
