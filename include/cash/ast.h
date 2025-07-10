#ifndef CASH_AST_H
#define CASH_AST_H

#include <cash/string.h>

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

enum ExprType {
    EXPR_SUBSHELL,
    EXPR_PIPELINE,
    EXPR_NOT,
    EXPR_AND,
    EXPR_OR,

    EXPR_COMMAND,
};

struct Expr {
    enum ExprType type;
    union {
        struct Program* subshell;  // EXPR_SUBSHELL
        struct Command command;    // EXPR_COMMAND
        struct {
            struct Expr* left;
            struct Expr* right;
        } binary;
    };
};
void free_expr(const struct Expr* expr);

struct Stmt {
    struct Expr expr;
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

void print_program(const struct Program* program, int indent);
void print_statement(const struct Stmt* stmt, int indent);
void print_expr(const struct Expr* expr, int indent);
void print_command(const struct Command* command);

#endif  // CASH_AST_H
