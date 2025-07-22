#include <cash/ast.h>
#include <cash/error.h>
#include <cash/parser/parser.h>
#include <cash/repl.h>
#include <cash/util.h>
#include <cash/vm.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool repl_mode = false;

int main(const int argc, char* argv[]) {
    if (argc == 1) {
        if (!isatty(STDIN_FILENO)) {
            char* input = read_all_stdin();
            run_string(input, 0, argv);
            free(input);
        } else {
            repl_mode = true;
            struct Repl repl = make_repl(0, argv);
            run_repl(&repl);
            free_repl(&repl);
        }
    } else {
        if (strcmp(argv[1], "-c") == 0) {
            if (argc < 3) {
                CASH_ERROR(EXIT_FAILURE, "-c requires an argument\n%s", "");
                return EXIT_FAILURE;
            }

            repl_mode = false;
            run_string(argv[2], 0, argv);
        } else {
            run_file(argv[1], argc - 2, argv + 1);
        }
    }
}
