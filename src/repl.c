#include <cash/ast.h>
#include <cash/parser/lexer.h>
#include <cash/parser/parser.h>
#include <cash/repl.h>
#include <cash/util.h>
#include <cash/vm.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

extern bool repl_mode;
extern char** environ;

static bool get_line(struct Repl* repl);

struct Repl make_repl(int argc, char** argv) {
    return (struct Repl){.parser = parser_new("", true),
                         .vm = make_vm(argc, argv),
                         .line = NULL};
}

void run_repl(struct Repl* repl) {
    while (1) {
        if (!get_line(repl)) {
            break;
        }

        reset_parser(repl->line, &repl->parser);
        lexer_lex_full(repl->parser.lexer);

        const bool success = parse_program(&repl->parser);
        if (repl->parser.error) {
            repl->vm.previous_exit_code = EXIT_FAILURE;
            continue;
        }

        struct Program program = repl->parser.program;
        if (success) {
#ifndef NDEBUG
            print_program(&program, 0);
            printf("\n");
#endif

            run_program(&repl->vm, &program);
            free_program(&program);

            if (repl->vm.exit) {
                break;
            }
        }
    }
}

void free_repl(const struct Repl* repl) {
    free_parser(&repl->parser);
    clear_history();
    free(repl->line);
    free_vm(&repl->vm);
}

static bool get_line(struct Repl* repl) {
    free(repl->line);
    repl->line = readline(repl->vm.current_prompt);
    if (repl->line) {
        add_history(repl->line);
        return true;
    }
    return false;
}
