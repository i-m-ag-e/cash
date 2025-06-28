#include <cash/ast.h>
#include <cash/parser/lexer.h>
#include <cash/parser/parser.h>
#include <cash/parser/token.h>
#include <stdio.h>
#include <stdlib.h>

#include "cash/error.h"

extern bool REPL_MODE;

static bool is_at_end(struct Parser* parser);
static struct Token peek(struct Parser* parser);
static enum TokenType peek_tt(struct Parser* parser);
static struct Token peek_next(struct Parser* parser);
static struct Token advance(struct Parser* parser);
static bool match(enum TokenType type, struct Parser* parser);
static struct Token consume(enum TokenType type, struct Parser* parser);

static bool skip_line_terminator(struct Parser* parser);
static bool parse_statement(struct Parser* parser, struct Stmt* stmt);

struct Parser parser_new(const char* input, bool repl_mode) {
    struct Parser parser = {
        .lexer = lexer_new(input, repl_mode),
        .input = input,
        .program = make_program(),
        .error = false,
    };
    return parser;
}

void reset_parser(const char* input, struct Parser* parser) {
    reset_lexer(input, &parser->lexer);
    parser->error = false;
    parser->input = input;
    parser->program = make_program();
}

void free_parser(struct Parser* parser) {
    free_lexer(&parser->lexer);
}

bool parse_program(struct Parser* parser) {
    parser->current_token = lexer_next_token(&parser->lexer);
    parser->next_token = lexer_next_token(&parser->lexer);
    while (peek(parser).type != TOKEN_EOF) {
        if (parser->error) {
            return false;
        }
        struct Stmt stmt;
        parse_statement(parser, &stmt);
        add_statement(&parser->program, stmt);
    }

    if (parser->error) {
        return false;
    }
    return true;
}

static bool skip_line_terminator(struct Parser* parser) {
    while (peek_tt(parser) == TOKEN_LINE_BREAK ||
           peek_tt(parser) == TOKEN_SEMICOLON) {
        advance(parser);
    }
    return !parser->error;
}

static bool parse_statement(struct Parser* parser, struct Stmt* stmt) {
    struct ShellString command_name = consume(TOKEN_WORD, parser).value.word;
    struct ArgumentList arg_list = make_arg_list();

    while (!is_at_end(parser)) {
        if (parser->error)
            return false;
        struct Token argument = consume(TOKEN_WORD, parser);
        add_argument(&arg_list, argument.value.word);

        if (peek_tt(parser) == TOKEN_SEMICOLON ||
            peek_tt(parser) == TOKEN_LINE_BREAK) {
            if (!skip_line_terminator(parser))
                return false;
            break;
        }
    }

    if (parser->error)
        return false;

    *stmt = (struct Stmt){.command = {command_name, arg_list}};
    return true;
}

static bool is_at_end(struct Parser* parser) {
    return parser->current_token.type == TOKEN_EOF;
}

static struct Token peek(struct Parser* parser) {
    return parser->current_token;
}

static enum TokenType peek_tt(struct Parser* parser) {
    return parser->current_token.type;
}

static struct Token peek_next(struct Parser* parser) {
    return parser->next_token;
}

static struct Token advance(struct Parser* parser) {
    struct Token curr = parser->current_token;
    if (curr.type == TOKEN_ERROR)
        parser->error = true;
    if (!is_at_end(parser)) {
        parser->current_token = parser->next_token;
        parser->next_token = lexer_next_token(&parser->lexer);
    }
    dump_token(&curr);
    return parser->current_token;
}

static bool match(enum TokenType type, struct Parser* parser) {
    if (peek(parser).type != type) {
        return false;
    }
    advance(parser);
    return true;
}

static struct Token consume(enum TokenType type, struct Parser* parser) {
    struct Token token = peek(parser);
    if (token.type == TOKEN_ERROR) {
        parser->error = true;
    } else if (token.type != type) {
        cash_error(EXIT_FAILURE, "Expected token `%s`, found `%s`\n",
                   token_type_to_string(type),
                   token_type_to_string(token.type));
        parser->error = true;
    }
    advance(parser);
    return token;
}
