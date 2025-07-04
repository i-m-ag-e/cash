#ifndef CASH_AST_H
#define CASH_AST_H

#include <cash/string.h>

enum AstNodeType {
    AST_PROGRAM,
    AST_STATEMENT,
    AST_COMMAND,
    AST_PIPELINE,
    // AST_SUBSTITUTION,
};

struct ArgumentList {
    struct ShellString* arguments;
    int argument_count;
    int argument_capacity;
};
struct ArgumentList make_arg_list();
void add_argument(struct ArgumentList* list, struct ShellString arg);
void free_arg_list(const struct ArgumentList* list);

struct Command {
    struct ShellString command_name;
    struct ArgumentList arguments;
};

struct Stmt {
    struct Command command;
};
void free_stmt(const struct Stmt* stmt);

struct Program {
    struct Stmt* statements;
    int statement_count;
    int statement_capacity;
};
struct Program make_program();
void add_statement(struct Program* program, struct Stmt stmt);
void free_program(const struct Program* program);

void print_program(const struct Program* program);
void print_statement(const struct Stmt* stmt);
void print_command(const struct Command* command);

#endif  // CASH_AST_H
