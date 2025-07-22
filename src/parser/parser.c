#include <cash/parser/lexer.h>
#include <cash/parser/parser.h>
#include <cash/parser/token.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cash/ast.h"
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
                               struct Token redir, const char** endp);
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
    const char* begin = NULL;
    const char* end;

    CHECK(parse_not_expr(parser, &left_expr));
    if (match(parser, TOKEN_AMP)) {
        left_expr.background = true;
        *expr = left_expr;
        return true;
    }

    while (peek_tt(parser) == TOKEN_AND || peek_tt(parser) == TOKEN_OR) {
        const struct Token tok = advance(parser);
        if (left_expr.type == EXPR_COMMAND &&
            left_expr.command.command_name.component_count == 0 &&
            left_expr.command.redirection_count == 0) {
            // error
            parser->error = true;
            CASH_ERROR(EXIT_FAILURE, "empty command in AND/OR list\n%s", "");
            return false;
        }

        if (begin == NULL)
            begin = tok.lexeme;
        ALLOC_CHECKED(left, sizeof(struct Expr));
        ALLOC_CHECKED(right, sizeof(struct Expr));

        CHECK(parse_not_expr(parser, right));
        end = right->expr_text.string + right->expr_text.length;

        if (right->type == EXPR_COMMAND &&
            right->command.command_name.component_count == 0 &&
            right->command.redirection_count == 0) {
            // error
            parser->error = true;
            CASH_ERROR(EXIT_FAILURE, "empty command in AND/OR list\n%s", "");
            return false;
        }

        *left = left_expr;
        left_expr =
            (struct Expr){.type = tok.type == TOKEN_AND ? EXPR_AND : EXPR_OR,
                          .binary = {.left = left, .right = right},
                          .expr_text = {begin, end - begin},
                          .background = false};
    }

    left_expr.background = match(parser, TOKEN_AMP);
    *expr = left_expr;
    return true;
}

static bool parse_not_expr(struct Parser* parser, struct Expr* expr) {
    const char* begin = peek(parser).lexeme;
    const bool is_not_expr = match(parser, TOKEN_NOT);
    struct Expr sub_expr;

    CHECK(parse_pipeline(parser, &sub_expr));
    if (is_not_expr) {
        if (sub_expr.type == EXPR_COMMAND &&
            sub_expr.command.command_name.component_count == 0 &&
            sub_expr.command.redirection_count == 0) {
            // error
            consume(TOKEN_WORD, parser);
        }

        struct Expr* not_expr;
        ALLOC_CHECKED(not_expr, sizeof(struct Expr));
        *not_expr = sub_expr;

        const char* end = sub_expr.expr_text.string + sub_expr.expr_text.length;
        *expr = (struct Expr){.type = EXPR_NOT,
                              .binary = {.left = not_expr, .right = NULL},
                              .expr_text = {begin, end - begin},
                              .background = false};
    } else {
        *expr = sub_expr;
    }
    return true;
}

static bool parse_pipeline(struct Parser* parser, struct Expr* expr) {
    struct Expr left_expr;
    const char* begin = peek(parser).lexeme;
    const char* end;
    CHECK(parse_terminal(parser, &left_expr));

    while (match(parser, TOKEN_PIPE)) {
        if (left_expr.type == EXPR_COMMAND &&
            left_expr.command.command_name.component_count == 0 &&
            left_expr.command.redirection_count == 0) {
            // error
            parser->error = true;
            CASH_ERROR(EXIT_FAILURE, "empty command in pipeline\n%s", "");
            return false;
        }

        struct Expr *left, *right;
        ALLOC_CHECKED(left, sizeof(struct Expr));
        ALLOC_CHECKED(right, sizeof(struct Expr));

        CHECK(parse_terminal(parser, right));
        end = right->expr_text.string + right->expr_text.length;

        if (right->type == EXPR_COMMAND &&
            right->command.command_name.component_count == 0 &&
            right->command.redirection_count == 0) {
            // error
            parser->error = true;
            CASH_ERROR(EXIT_FAILURE, "empty command in pipeline\n%s", "");
            return false;
        }

        *left = left_expr;
        left_expr = (struct Expr){.type = EXPR_PIPELINE,
                                  .binary = {.left = left, .right = right},
                                  .expr_text = {begin, end - begin},
                                  .background = false};
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
    const char* begin = peek(parser).lexeme;
    const char* end;

    advance(parser);
    struct Parser subparser = make_subparser(parser);
    struct Program* subshell;

    if (!parse_program(&subparser)) {
        parser->error = true;
        return false;
    }

    parser->current_token = subparser.current_token;
    parser->next_token = subparser.next_token;
    struct Token rparen = consume(TOKEN_RPAREN, parser);  // consume ')'
    end = rparen.lexeme + rparen.lexeme_length;

    if (parser->error)
        return false;

    ALLOC_CHECKED(subshell, sizeof(struct Program));
    *subshell = subparser.program;
    *expr = (struct Expr){.type = EXPR_SUBSHELL,
                          .subshell = subshell,
                          .background = false,
                          .expr_text = {begin, end - begin}};
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
    const char* begin = peek(parser).lexeme;
    const char* end;

    while (!is_at_end(parser) && !break_out) {
        if (parser->error)
            return false;

        const struct Token next = peek(parser);
        switch (next.type) {
            case TOKEN_WORD: {
                end = next.lexeme + next.lexeme_length;
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
            case TOKEN_AMP:
                break_out = true;
                break;

            case TOKEN_REDIRECT:
                handle_redirection(parser, &command, advance(parser), &end);
                break;

            case TOKEN_ERROR:
                parser->error = true;
                return false;
            default:
                CASH_ERROR(EXIT_FAILURE, "IMPOSSIBLE%s", "");
                exit(EXIT_FAILURE);
        }
    }

    if (parser->error)
        return false;

    *expr = (struct Expr){.type = EXPR_COMMAND,
                          .command = command,
                          .background = false,
                          .expr_text = {begin, end - begin}};
    return true;
}

static bool handle_redirection(struct Parser* parser, struct Command* command,
                               struct Token redir, const char** endp) {
    *endp = redir.lexeme + redir.lexeme_length;
    struct Redirection redirection = {
        .type = redir.value.redirection.type,
        .left = redir.value.redirection.left,
        .right = redir.value.redirection.right,
        .file_name = {
            .components = NULL, .component_count = 0, .component_capacity = 0}};

    if (redirection.right == -1) {
        struct Token rhs = consume(TOKEN_WORD, parser);
        redirection.file_name = rhs.value.word;
        *endp = rhs.lexeme + rhs.lexeme_length;
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

    if (stmt->expr.type == EXPR_COMMAND &&
        stmt->expr.command.command_name.component_count == 0 &&
        stmt->expr.command.redirection_count == 0) {
        bool skipped = skip_line_terminator(parser);
        if (!is_at_end(parser)) {
            CASH_ERROR(EXIT_FAILURE, "empty command %s", "");
            return false;
        }
        return skipped;
    }

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
    struct Token curr = parser->current_token;
    if (parser->current_token.type == TOKEN_ERROR)
        parser->error = true;
    if (!is_at_end(parser)) {
        parser->current_token = parser->next_token;
        parser->next_token = lexer_next_token(parser->lexer);
    }

#ifndef NDEBUG
    dump_token(&curr);
#endif
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
