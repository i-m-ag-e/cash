#include <cash/parser/lexer.h>
#include <cash/parser/parser.h>
#include <cash/parser/token.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "cash/error.h"
#include "cash/memory.h"

#define ALLOC_CHECKED(ptr, size)                                          \
    do {                                                                  \
        ptr = malloc(size);                                               \
        if (!ptr) {                                                       \
            CASH_ERROR(EXIT_FAILURE, "Memory allocation failed%s\n", ""); \
            parser->error = true;                                         \
            return false;                                                 \
        }                                                                 \
    } while (0)

#define CHECK(expr)       \
    do {                  \
        if (!(expr)) {    \
            return false; \
        }                 \
    } while (0)

extern bool repl_mode;

static struct Parser make_subparser(const struct Parser* parser);

static bool is_at_end(const struct Parser* parser);
static struct Token peek(const struct Parser* parser);
static enum TokenType peek_tt(const struct Parser* parser);
// static struct Token peek_next(const struct Parser* parser);
static struct Token advance(struct Parser* parser);
static bool match(struct Parser* parser, enum TokenType type);
static struct Token consume(enum TokenType type, struct Parser* parser);

static bool parse_subshell(struct Parser* parser, struct Expr* expr);
static bool parse_terminal(struct Parser* parser, struct Expr* expr);
static bool parse_not_expr(struct Parser* parser, struct Expr* expr);
static bool parse_pipeline(struct Parser* parser, struct Expr* expr);
static bool handle_redirection(struct Parser* parser, struct Command* command,
                               struct Token redir);
static bool parse_command(struct Parser* parser, struct Expr* expr);
static bool parse_expr(struct Parser* parser, struct Expr* expr);

static bool skip_line_terminator(struct Parser* parser);
static bool parse_statement(struct Parser* parser, struct Stmt* stmt);

struct Parser parser_new(const char* input, bool repl_mode) {
    const struct Parser parser = {.lexer = lexer_new(input, repl_mode),
                                  .input = input,
                                  .program = make_program(),
                                  .error = false,
                                  .is_subparser = false};
    return parser;
}

void reset_parser(const char* input, struct Parser* parser) {
    reset_lexer(input, parser->lexer);
    parser->error = false;
    parser->input = input;
    parser->program = make_program();
}

void free_parser(const struct Parser* parser) {
    free_lexer(parser->lexer);
    free(parser->lexer);
}

bool parse_program(struct Parser* parser) {
    if (!parser->is_subparser) {
        parser->current_token = lexer_next_token(parser->lexer);
        parser->next_token = lexer_next_token(parser->lexer);
    }
    while (peek_tt(parser) != TOKEN_EOF) {
        if (parser->error) {
            return false;
        }
        struct Stmt stmt;
        parse_statement(parser, &stmt);
        add_statement(&parser->program, stmt);

        if (peek_tt(parser) == TOKEN_RPAREN && parser->is_subparser)
            break;
    }

    if (parser->error) {
        return false;
    }
    return true;
}

static struct Parser make_subparser(const struct Parser* parser) {
    struct Parser subparser = *parser;
    subparser.is_subparser = true;
    subparser.program = make_program();
    return subparser;
}

static bool skip_line_terminator(struct Parser* parser) {
    while (peek_tt(parser) == TOKEN_LINE_BREAK ||
           peek_tt(parser) == TOKEN_SEMICOLON) {
        advance(parser);
    }
    return !parser->error;
}

static bool parse_expr(struct Parser* parser, struct Expr* expr) {
    struct Expr left_expr;
    struct Expr *left, *right;

    CHECK(parse_not_expr(parser, &left_expr));

    while (peek_tt(parser) == TOKEN_AND || peek_tt(parser) == TOKEN_OR) {
        const enum TokenType op = advance(parser).type;
        ALLOC_CHECKED(left, sizeof(struct Expr));
        ALLOC_CHECKED(right, sizeof(struct Expr));

        CHECK(parse_not_expr(parser, right));
        *left = left_expr;
        left_expr = (struct Expr){
            .type = op == TOKEN_AND ? EXPR_AND : EXPR_OR,
            .binary = {.left = left, .right = right},
        };
    }

    *expr = left_expr;
    return true;
}

static bool parse_not_expr(struct Parser* parser, struct Expr* expr) {
    const bool is_not_expr = match(parser, TOKEN_NOT);
    struct Expr sub_expr;

    CHECK(parse_pipeline(parser, &sub_expr));
    if (is_not_expr) {
        struct Expr* not_expr;
        ALLOC_CHECKED(not_expr, sizeof(struct Expr));
        *not_expr = sub_expr;

        *expr = (struct Expr){
            .type = EXPR_NOT,
            .binary = {.left = not_expr, .right = NULL},
        };
    } else {
        *expr = sub_expr;
    }
    return true;
}

static bool parse_pipeline(struct Parser* parser, struct Expr* expr) {
    struct Expr left_expr;
    CHECK(parse_terminal(parser, &left_expr));

    while (match(parser, TOKEN_PIPE)) {
        struct Expr *left, *right;
        ALLOC_CHECKED(left, sizeof(struct Expr));
        ALLOC_CHECKED(right, sizeof(struct Expr));

        CHECK(parse_terminal(parser, right));

        *left = left_expr;
        left_expr = (struct Expr){
            .type = EXPR_PIPELINE,
            .binary = {.left = left, .right = right},
        };
    }

    *expr = left_expr;
    return true;
}

static bool parse_terminal(struct Parser* parser, struct Expr* expr) {
    if (peek_tt(parser) == TOKEN_LPAREN) {
        return parse_subshell(parser, expr);
    }
    return parse_command(parser, expr);
}

static bool parse_subshell(struct Parser* parser, struct Expr* expr) {
    advance(parser);
    struct Parser subparser = make_subparser(parser);
    struct Program* subshell;

    if (!parse_program(&subparser)) {
        parser->error = true;
        return false;
    }

    parser->current_token = subparser.current_token;
    parser->next_token = subparser.next_token;
    consume(TOKEN_RPAREN, parser);  // consume ')'

    if (parser->error)
        return false;

    ALLOC_CHECKED(subshell, sizeof(struct Program));
    *subshell = subparser.program;
    *expr = (struct Expr){.type = EXPR_SUBSHELL, .subshell = subshell};
    return true;
}

static bool parse_command(struct Parser* parser, struct Expr* expr) {
    struct Command command = {.command_name = {.components = NULL,
                                               .component_count = 0,
                                               .component_capacity = 0},
                              .arguments = make_arg_list(),
                              .redirection_capacity = 0,
                              .redirection_count = 0,
                              .redirections = NULL};
    bool break_out = false;

    while (!is_at_end(parser) && !break_out) {
        if (parser->error)
            return false;

        const enum TokenType next = peek_tt(parser);
        switch (next) {
            case TOKEN_WORD: {
                if (command.command_name.component_count == 0) {
                    command.command_name = advance(parser).value.word;
                } else {
                    const struct Token argument = advance(parser);
                    add_argument(&command.arguments, argument.value.word);
                }
                break;
            }
            case TOKEN_RPAREN:
                if (parser->is_subparser)
                    break_out = true;
                continue;

            case TOKEN_PIPE:
            case TOKEN_AND:
            case TOKEN_OR:
            case TOKEN_SEMICOLON:
            case TOKEN_LINE_BREAK:
                break_out = true;
                break;

            case TOKEN_REDIRECT:
                handle_redirection(parser, &command, advance(parser));
                break;

            case TOKEN_ERROR:
                parser->error = true;
                return false;
            default:
                CASH_ERROR(EXIT_FAILURE, "IMPOSSIBLE");
                exit(EXIT_FAILURE);
        }
    }

    if (parser->error)
        return false;

    *expr = (struct Expr){.type = EXPR_COMMAND, .command = command};
    return true;
}

static bool handle_redirection(struct Parser* parser, struct Command* command,
                               struct Token redir) {
    struct Redirection redirection = {
        .type = redir.value.redirection.type,
        .left = redir.value.redirection.left,
        .right = redir.value.redirection.right,
        .file_name = {
            .components = NULL, .component_count = 0, .component_capacity = 0}};

    if (redirection.right == -1) {
        redirection.file_name = consume(TOKEN_WORD, parser).value.word;
        if (parser->error)
            return false;
    }

    ADD_LIST(command, redirection_count, redirection_capacity, redirections,
             redirection, struct Redirection);
    return true;
}

static bool parse_statement(struct Parser* parser, struct Stmt* stmt) {
    struct Expr expr;
    CHECK(parse_expr(parser, &expr));
    *stmt = (struct Stmt){.expr = expr};

    if (parser->is_subparser && peek_tt(parser) == TOKEN_RPAREN)
        return true;

    return skip_line_terminator(parser);
}

static bool is_at_end(const struct Parser* parser) {
    return parser->current_token.type == TOKEN_EOF;
}

static struct Token peek(const struct Parser* parser) {
    return parser->current_token;
}

static enum TokenType peek_tt(const struct Parser* parser) {
    return parser->current_token.type;
}

// static struct Token peek_next(const struct Parser* parser) {
//     return parser->next_token;
// }

static struct Token advance(struct Parser* parser) {
    const struct Token curr = parser->current_token;
    if (curr.type == TOKEN_ERROR)
        parser->error = true;
    if (!is_at_end(parser)) {
        parser->current_token = parser->next_token;
        parser->next_token = lexer_next_token(parser->lexer);
    }
    dump_token(&curr);
    return curr;
}

static bool match(struct Parser* parser, enum TokenType type) {
    if (peek(parser).type != type) {
        return false;
    }
    advance(parser);
    return true;
}

static struct Token consume(enum TokenType type, struct Parser* parser) {
    const struct Token token = peek(parser);
    if (token.type == TOKEN_ERROR) {
        parser->error = true;
    } else if (token.type != type) {
        CASH_ERROR(EXIT_FAILURE, "Expected token `%s`, found `%s`\n",
                   token_type_to_string(type),
                   token_type_to_string(token.type));
        parser->error = true;
    }
    advance(parser);
    return token;
}
