#include <cash/ast.h>
#include <cash/error.h>
#include <cash/parser/lexer.h>
#include <cash/parser/parser.h>
#include <cash/parser/token.h>
#include <cash/repl.h>
#include <cash/vm.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdio.h>
#include <stdlib.h>

extern bool REPL_MODE;
extern char **environ;

void run_repl(void) {
    char *line = readline("$> ");
    struct Parser parser = parser_new(line, true);
    if (!line) {
        printf("Exit.\n");
        goto exit_repl;
    }

    while (1) {
        add_history(line);
        lexer_lex_full(&parser.lexer);
        bool success = parse_program(&parser);
        struct Program program = parser.program;
        if (success) {
            print_program(&program);
            printf("\n");

            run_program(&program);
        } else {
            program = make_program();
        }

        free_program(&program);
        free(line);

        line = readline("$> ");
        if (!line) {
            printf("Exit.\n");
            goto exit_repl;
        }
        reset_parser(line, &parser);
    }

exit_repl:
    free_parser(&parser);
    clear_history();
    free(line);
}
