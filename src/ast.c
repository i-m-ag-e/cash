#include <assert.h>
#include <cash/ast.h>
#include <cash/colors.h>
#include <cash/memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NDEBUG
#define INDENT "    "

static const char *kIndents[] = {
    "",
    INDENT,
    INDENT INDENT,
    INDENT INDENT INDENT,
    INDENT INDENT INDENT INDENT,
    INDENT INDENT INDENT INDENT INDENT,
    INDENT INDENT INDENT INDENT INDENT INDENT,
    INDENT INDENT INDENT INDENT INDENT INDENT INDENT,
};
#endif

extern bool repl_mode;

struct ArgumentList make_arg_list(void) {
    return (struct ArgumentList){
        .argument_capacity = 0, .argument_count = 0, .arguments = NULL};
}

void add_argument(struct ArgumentList *list, struct ShellString arg) {
    ADD_LIST(list, argument_count, argument_capacity, arguments, arg,
             struct ShellString);
}

void free_arg_list(const struct ArgumentList *list) {
    for (int i = 0; i < list->argument_count; ++i)
        free_shell_string(&list->arguments[i]);
    free(list->arguments);
}

void free_expr(const struct Expr *expr) {
    switch (expr->type) {
        case EXPR_COMMAND:
            free_shell_string(&expr->command.command_name);
            free_arg_list(&expr->command.arguments);
            break;

        case EXPR_SUBSHELL:
            free_program(expr->subshell);
            break;

        case EXPR_PIPELINE:
        case EXPR_AND:
        case EXPR_OR:
            free_expr(expr->binary.left);
            free_expr(expr->binary.right);
            free(expr->binary.left);
            free(expr->binary.right);
            break;

        case EXPR_NOT:
            free_expr(expr->binary.left);
            free(expr->binary.left);
            break;
    }
}

void free_stmt(const struct Stmt *stmt) {
    free_expr(&stmt->expr);
}

struct Program make_program(void) {
    return (struct Program){
        .statement_capacity = 0, .statement_count = 0, .statements = NULL};
}

void add_statement(struct Program *program, struct Stmt stmt) {
    ADD_LIST(program, statement_count, statement_capacity, statements, stmt,
             struct Stmt);
}

void free_program(const struct Program *program) {
    for (int i = 0; i < program->statement_count; ++i)
        free_stmt(&program->statements[i]);
    free(program->statements);
}

#ifndef NDEBUG
void print_string_component(const struct StringComponent *component) {
    switch (component->type) {
        case STRING_COMPONENT_LITERAL:
            fprintf(stderr, MAGENTA "%s" RESET, component->literal);
            break;
        case STRING_COMPONENT_DQ:
            fprintf(stderr, BOLD BLUE "\"%s\"" RESET, component->literal);
            break;
        case STRING_COMPONENT_SQ:
            fprintf(stderr, BOLD CYAN "'%s'" RESET, component->literal);
            break;
        case STRING_COMPONENT_VAR_SUB:
            fprintf(stderr, GREEN "$%s" RESET, component->var_substitution);
            break;
        case STRING_COMPONENT_BRACED_SUB:
            fprintf(stderr, GREEN "$%s" RESET, component->braced_substitution);
            break;
        case STRING_COMPONENT_COMMAND_SUBSTITUTION:
            fprintf(stderr, "command sub");
            break;
    }
}

void print_string(const struct ShellString *string) {
    for (int i = 0; i < string->component_count; ++i)
        print_string_component(&string->components[i]);
}

void print_program(const struct Program *program, int indent) {
    fprintf(stderr, "Program([<len: %d>\n", program->statement_count);
    for (int i = 0; i < program->statement_count; ++i) {
        fprintf(stderr, "%s", kIndents[indent + 1]);
        print_statement(&program->statements[i], indent + 1);
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "%s])", kIndents[indent]);
}

void print_expr(const struct Expr *expr, int indent) {
    if (expr->background)
        fprintf(stderr, BOLD YELLOW "(background)" RESET);
    switch (expr->type) {
        case EXPR_SUBSHELL:
            fprintf(stderr, "Subshell( ");
            print_program(expr->subshell, indent + 1);
            fprintf(stderr, " )");
            break;

        case EXPR_PIPELINE:
        case EXPR_AND:
        case EXPR_OR:
            fprintf(stderr, "Binary( %s\n%s",
                    expr->type == EXPR_PIPELINE ? "|"
                    : expr->type == EXPR_AND    ? "&&"
                                                : "||",
                    kIndents[indent + 1]);
            print_expr(expr->binary.left, indent + 1);
            fprintf(stderr, ",\n%s", kIndents[indent + 1]);
            print_expr(expr->binary.right, indent + 1);
            fprintf(stderr, " )");
            break;

        case EXPR_NOT:
            fprintf(stderr, "Not( ");
            print_expr(expr->binary.left, indent + 1);
            fprintf(stderr, " )");
            break;

        case EXPR_COMMAND:
            print_command(&expr->command);
            break;
    }
}

void print_statement(const struct Stmt *stmt, int indent) {
    fprintf(stderr, "Stmt( ");
    print_expr(&stmt->expr, indent);
    fprintf(stderr, " )");
}

void print_command(const struct Command *command) {
    fprintf(stderr, "Command(<args: %d> " BOLD CYAN,
            command->arguments.argument_count);
    print_string(&command->command_name);
    fprintf(stderr, "%s" RESET, command->arguments.argument_count ? " " : "");
    for (int i = 0; i < command->arguments.argument_count; ++i) {
        print_string(&command->arguments.arguments[i]);
        fprintf(stderr, " ");
    }

    for (int i = 0; i < command->redirection_count; ++i) {
        print_redirection(&command->redirections[i]);
        fprintf(stderr, " ");
    }
    fprintf(stderr, ")");
}

void print_redirection(const struct Redirection *redirection) {
    fprintf(stderr, "( ");
    if (redirection->left != -1) {
        fprintf(stderr, CYAN "%d" RESET, redirection->left);
    }

    switch (redirection->type) {
        case REDIRECT_IN:
            fprintf(stderr, YELLOW "<" RESET);
            break;
        case REDIRECT_OUT:
            fprintf(stderr, YELLOW ">" RESET);
            break;
        case REDIRECT_OUT_DUPLICATE:
            fprintf(stderr, YELLOW ">&" RESET);
            break;
        case REDIRECT_OUTERR:
            fprintf(stderr, YELLOW "&>" RESET);
            break;
        case REDIRECT_APPEND_OUT:
            fprintf(stderr, YELLOW ">>" RESET);
            break;
        case REDIRECT_APPEND_OUTERR:
            fprintf(stderr, YELLOW "&>>" RESET);
            break;
        case REDIRECT_INOUT:
            fprintf(stderr, YELLOW "<>" RESET);
            break;
    }

    if (redirection->right != -1) {
        fprintf(stderr, CYAN "%d" RESET, redirection->right);
    } else {
        fprintf(stderr, " ");
        print_string(&redirection->file_name);
    }
    fprintf(stderr, " )");
}
#endif
