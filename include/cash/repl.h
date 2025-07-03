#ifndef REPL_H
#define REPL_H

#include <cash/parser/parser.h>
#include <cash/vm.h>

struct Repl {
    struct Parser parser;
    struct VM vm;
    char* line;
};

struct Repl make_repl(void);
void run_repl(struct Repl* repl);
void free_repl(struct Repl* repl);

#endif
