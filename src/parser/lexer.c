#include <assert.h>
#include <cash/error.h>
#include <cash/parser/lexer.h>
#include <cash/parser/token.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "cash/ast.h"
#include "cash/memory.h"

extern bool repl_mode;

static char peek(const struct Lexer* lexer);
// static char peek_next(struct Lexer* lexer);
static char advance(struct Lexer* lexer);
static bool match(struct Lexer* lexer, char c);
static bool is_at_end(const struct Lexer* lexer);

static struct Token make_token(enum TokenType type, struct Lexer* lexer);
static struct Token make_error(struct Lexer* lexer);
static struct Token make_eof(const struct Lexer* lexer);

static void skip_ws(struct Lexer* lexer);
static struct Token consume_lines(struct Lexer* lexer);

static struct Token consume_string(struct Lexer* lexer);
static void consume_sq_string(struct Lexer* lexer);
static void consume_dq_string(struct Lexer* lexer);
static void consume_unquoted_string(struct Lexer* lexer);
static void consume_substitution(struct Lexer* lexer);

static struct Token lexer_lex(struct Lexer* lexer);

static void lexer_push_token(struct Lexer* lexer, struct Token token);
static struct Token lexer_pop_token(struct Lexer* lexer);
static void lexer_reset_queue(struct Lexer* lexer);

static const bool kPunctuation[128] = {
    ['>'] = true,  ['|'] = true,  ['<'] = true,  ['('] = true, [')'] = true,
    ['\''] = true, ['"'] = true,  [';'] = true,  ['&'] = true, ['`'] = true,
    ['$'] = true,  ['\t'] = true, ['\n'] = true, [' '] = true};
// ">|<()'\";&`$\t\n"

struct Lexer* lexer_new(const char* input, bool repl_mode) {
    struct Lexer* lexer = malloc(sizeof(struct Lexer));
    *lexer = (struct Lexer){
        .repl_mode = repl_mode,
        .error = false,
        .input = input,
        .token_start = 0,
        .position = 0,

        .first_line = 1,
        .first_column = 1,
        .last_column = 1,
        .last_line = 1,

        .token_queue = NULL,
        .token_queue_head = 0,
        .token_queue_size = 0,
        .token_queue_capacity = 0,

        .substitution_in_quotes = false,
        .continue_string = false,
    };
    return lexer;
}

void reset_lexer(const char* input, struct Lexer* lexer) {
    lexer->error = false;
    lexer->input = input;
    lexer->token_start = lexer->position = 0;
    lexer->first_column = lexer->first_line = lexer->last_column =
        lexer->last_line = 1;
    lexer_reset_queue(lexer);
    lexer->continue_string = false;
    lexer->substitution_in_quotes = false;
}

void free_lexer(const struct Lexer* lexer) {
    free(lexer->token_queue);
}

struct Token lexer_next_token(struct Lexer* lexer) {
    if (lexer->repl_mode) {
        if (lexer->token_queue_size == 0)
            return make_eof(lexer);
        const struct Token token = lexer_pop_token(lexer);
        return token;
    }
    return lexer_lex(lexer);
}

void lexer_lex_full(struct Lexer* lexer) {
    while (true) {
        const struct Token token = lexer_lex(lexer);
        lexer_push_token(lexer, token);
        if (token.type == TOKEN_EOF)
            break;
    }
}

static struct Token lexer_lex(struct Lexer* lexer) {
#define CHAR(c, ttype)                       \
    case c:                                  \
        do {                                 \
            advance(lexer);                  \
            return make_token(ttype, lexer); \
        } while (0)

    skip_ws(lexer);
    lexer->first_column = lexer->last_column;
    lexer->first_line = lexer->first_line;
    lexer->token_start = lexer->position;

    if (is_at_end(lexer))
        return make_eof(lexer);

    switch (peek(lexer)) {
        CHAR('(', TOKEN_LPAREN);
        CHAR(')', TOKEN_RPAREN);
        CHAR(';', TOKEN_SEMICOLON);
        CHAR('!', TOKEN_NOT);
        case '&':
            advance(lexer);
            return match(lexer, '&') ? make_token(TOKEN_AND, lexer)
                                     : make_token(TOKEN_AMP, lexer);
        case '|':
            advance(lexer);
            return match(lexer, '|') ? make_token(TOKEN_OR, lexer)
                                     : make_token(TOKEN_PIPE, lexer);
        case '\n':
            return consume_lines(lexer);
        default:
            break;
    }
    return consume_string(lexer);
}

static char peek(const struct Lexer* lexer) {
    return lexer->input[lexer->position];
}

// static char peek_next(struct Lexer* lexer) {
//     if (is_at_end(lexer))
//         return '\0';
//     return lexer->input[lexer->position + 1];
// }

static char advance(struct Lexer* lexer) {
    if (is_at_end(lexer))
        return '\0';
    return lexer->input[lexer->position++];
}

static bool is_at_end(const struct Lexer* lexer) {
    return peek(lexer) == '\0';
}

static void skip_ws(struct Lexer* lexer) {
    while (peek(lexer) != '\n' && isspace(peek(lexer))) {
        lexer->last_column++;
        advance(lexer);
    }
}

static struct Token consume_lines(struct Lexer* lexer) {
    do {
        advance(lexer);
        lexer->last_line++;
        lexer->last_column = 1;
        skip_ws(lexer);
    } while (peek(lexer) == '\n');
    struct Token token = make_token(TOKEN_LINE_BREAK, lexer);
    token.last_line = token.first_line + 1;
    token.last_column = 1;
    token.lexeme_length = 1;
    return token;
}

static bool match(struct Lexer* lexer, char c) {
    if (peek(lexer) == c) {
        advance(lexer);
        return true;
    }
    return false;
}

static struct Token make_token(enum TokenType type, struct Lexer* lexer) {
    switch (type) {
        case TOKEN_WORD:
        case TOKEN_LINE_BREAK:
            break;
        default:
            lexer->last_column += lexer->position - lexer->token_start;
            break;
    }

    const struct Token token = {
        .type = type,
        .first_line = lexer->first_line,
        .last_line = lexer->last_line,
        .last_column = lexer->last_column,
        .first_column = lexer->first_column,
        .lexeme = lexer->input + lexer->token_start,
        .lexeme_length = lexer->position - lexer->token_start};

    return token;
}

static struct Token make_error(struct Lexer* lexer) {
    struct Token tok = make_eof(lexer);
    tok.type = TOKEN_ERROR;
    lexer->error = true;
    return tok;
}

static struct Token make_eof(const struct Lexer* lexer) {
    const struct Token token = {
        .type = TOKEN_EOF,
        .first_line = lexer->first_line,
        .last_line = lexer->last_line,
        .last_column = lexer->last_column,
        .first_column = lexer->first_column,
        .lexeme = "",
        .lexeme_length = 0,
    };
    return token;
}

static struct Token consume_string(struct Lexer* lexer) {
    lexer->continue_string = true;
    lexer->current_string = make_string();
    while (true) {
        if (lexer->error)
            return make_error(lexer);

        if (lexer->substitution_in_quotes) {
            lexer->substitution_in_quotes = false;
            consume_dq_string(lexer);
            continue;
        }

        const char c = peek(lexer);
        if (c == '\'')
            consume_sq_string(lexer);
        else if (c == '"') {
            advance(lexer);
            consume_dq_string(lexer);
        } else if (c == '$')
            consume_substitution(lexer);
        else if (!is_at_end(lexer) && !kPunctuation[(int)c])
            consume_unquoted_string(lexer);
        else
            break;
    }
    lexer->last_column += lexer->position - lexer->token_start;
    struct Token token = make_token(TOKEN_WORD, lexer);
    token.value.word = lexer->current_string;
    lexer->continue_string = false;
    return token;
}

static void consume_unquoted_string(struct Lexer* lexer) {
    char c;
    const int string_start = lexer->position;
    int escapes = 0;
    while (!is_at_end(lexer) && !kPunctuation[(int)(c = peek(lexer))]) {
        if (c == '\\') {
            escapes++;
            advance(lexer);
        }
        advance(lexer);
    }

    lexer->substitution_in_quotes = false;
    add_string_literal(&lexer->current_string, STRING_COMPONENT_LITERAL,
                       &lexer->input[string_start],
                       lexer->position - string_start, escapes);
}

static void consume_dq_string(struct Lexer* lexer) {
    const int string_start = lexer->position;
    int escapes = 0;
    while (!is_at_end(lexer) && peek(lexer) != '"') {
        if (peek(lexer) == '$') {
            add_string_literal(&lexer->current_string, STRING_COMPONENT_DQ,
                               &lexer->input[string_start],
                               lexer->position - string_start, escapes);
            lexer->substitution_in_quotes = true;
            consume_substitution(lexer);
            return;
        }
        if (peek(lexer) == '\\') {
            advance(lexer);
            escapes++;
        }
        advance(lexer);
    }

    if (is_at_end(lexer)) {
        CASH_ERROR(EXIT_FAILURE, "unexpected <eof> in string literal%s\n", "");
        lexer->error = true;
        return;
    }
    advance(lexer);

    add_string_literal(&lexer->current_string, STRING_COMPONENT_DQ,
                       &lexer->input[string_start],
                       lexer->position - string_start - 1, escapes);
}

static void consume_sq_string(struct Lexer* lexer) {
    advance(lexer);
    const int string_start = lexer->position;
    while (!is_at_end(lexer) && peek(lexer) != '\'') {
        advance(lexer);
    }

    if (is_at_end(lexer)) {
        CASH_ERROR(EXIT_FAILURE, "unexpected <eof> in string literal%s\n", "");
        lexer->error = true;
        return;
    }
    advance(lexer);

    add_string_literal(&lexer->current_string, STRING_COMPONENT_SQ,
                       &lexer->input[string_start],
                       lexer->position - string_start - 1, 0);
}

static void consume_substitution(struct Lexer* lexer) {
    advance(lexer);  // '$'

    if (peek(lexer) == '?') {
        add_string_component(&lexer->current_string, STRING_COMPONENT_VAR_SUB,
                             "?", 1);
        advance(lexer);
        return;
    }

    const int name_start = lexer->position;
    while (!is_at_end(lexer)) {
        const char c = peek(lexer);
        if (c != '_' && !isalnum(c))
            break;

        advance(lexer);
    }

    add_string_component(&lexer->current_string, STRING_COMPONENT_VAR_SUB,
                         &lexer->input[name_start],
                         lexer->position - name_start);
}

static void lexer_push_token(struct Lexer* lexer, struct Token token) {
    ADD_LIST(lexer, token_queue_size, token_queue_capacity, token_queue, token,
             struct Token);
}

static struct Token lexer_pop_token(struct Lexer* lexer) {
    const struct Token token = lexer->token_queue[lexer->token_queue_head];
    lexer->token_queue_head++;
    lexer->token_queue_size--;
    return token;
}

static void lexer_reset_queue(struct Lexer* lexer) {
    lexer->token_queue_head = 0;
    lexer->token_queue_size = 0;
}
