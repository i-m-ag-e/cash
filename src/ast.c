#include <assert.h>
#include <cash/ast.h>
#include <cash/colors.h>
#include <cash/memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool repl_mode;

struct ArgumentList make_arg_list() {
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

void free_stmt(const struct Stmt *stmt) {
    free_shell_string(&stmt->command.command_name);
    free_arg_list(&stmt->command.arguments);
}

struct Program make_program() {
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

void print_string_component(const struct StringComponent *component) {
    switch (component->type) {
        case STRING_COMPONENT_LITERAL:
            printf(MAGENTA "%s" RESET, component->literal);
            break;
        case STRING_COMPONENT_DQ:
            printf(BOLD BLUE "\"%s\"" RESET, component->literal);
            break;
        case STRING_COMPONENT_SQ:
            printf(BOLD CYAN "'%s'" RESET, component->literal);
            break;
        case STRING_COMPONENT_VAR_SUB:
            printf(GREEN "$%s" RESET, component->var_substitution);
            break;
        case STRING_COMPONENT_BRACED_SUB:
            printf(GREEN "$%s" RESET, component->braced_substitution);
            break;
        case STRING_COMPONENT_COMMAND_SUBSTITUTION:
            printf("command sub");
            break;
    }
}

void print_string(const struct ShellString *string) {
    for (int i = 0; i < string->component_count; ++i)
        print_string_component(&string->components[i]);
}

void print_program(const struct Program *program) {
    printf("Program([<len: %d>\n", program->statement_count);
    for (int i = 0; i < program->statement_count; ++i) {
        printf("    ");
        print_statement(&program->statements[i]);
        printf("\n");
    }
    printf("])");
}

void print_statement(const struct Stmt *stmt) {
    printf("Stmt( ");
    print_command(&stmt->command);
    printf(" )");
}

void print_command(const struct Command *command) {
    printf("Command(<args: %d> " BOLD CYAN, command->arguments.argument_count);
    print_string(&command->command_name);
    printf("%s" RESET, command->arguments.argument_count ? " " : "");
    for (int i = 0; i < command->arguments.argument_count; ++i) {
        print_string(&command->arguments.arguments[i]);
        if (i != command->arguments.argument_count - 1)
            printf(" ");
    }
    printf(")");
}
