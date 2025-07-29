#include <assert.h>
#include <cash/error.h>
#include <cash/memory.h>
#include <cash/string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cash/ast.h"

extern bool is_repl_mode;

struct ShellString make_string(void) {
    return (struct ShellString){
        .component_capacity = 0, .component_count = 0, .components = NULL};
}

void add_string_literal(struct ShellString *str, enum StringComponentType type,
                        const char *literal, int length, int escapes) {
    assert(type == STRING_COMPONENT_LITERAL || type == STRING_COMPONENT_DQ ||
           type == STRING_COMPONENT_SQ);
    if (length > 0) {
        add_string_component(
            str, (struct StringComponent){.literal = strndup(literal, length),
                                          .type = type,
                                          .length = length,
                                          .escapes = escapes});
    }
}

void add_string_component(struct ShellString *str,
                          struct StringComponent comp) {
    ADD_LIST(str, component_count, component_capacity, components, comp,
             struct StringComponent);
}

void add_string_component_from_value(struct ShellString *str,
                                     enum StringComponentType type,
                                     const char *value, int length) {
    assert(type == STRING_COMPONENT_BRACED_SUB ||
           type == STRING_COMPONENT_VAR_SUB ||
           type == STRING_COMPONENT_COMMAND_SUBSTITUTION);
    char *val = strndup(value, length);

    if (length <= 0)
        return;

    struct StringComponent component;
    switch (type) {
        case STRING_COMPONENT_BRACED_SUB: {
            component = (struct StringComponent){
                .type = type,
                .length = length,
                .braced_substitution = val,
            };
            break;
        }
        case STRING_COMPONENT_VAR_SUB: {
            component = (struct StringComponent){
                .type = type,
                .length = length,
                .var_substitution = val,
            };
            break;
        }
        default: {
            component = (struct StringComponent){
                .type = STRING_COMPONENT_COMMAND_SUBSTITUTION,
                .length = length,
                .command_substitution = NULL};
            break;
        }
    }

    add_string_component(str, component);
}

void add_command_substitution(struct ShellString *str,
                              struct Program *program) {
    assert(program != NULL);
    add_string_component(str, (struct StringComponent){
                                  .type = STRING_COMPONENT_COMMAND_SUBSTITUTION,
                                  .length = 0,
                                  .command_substitution = program});
}

void free_string_component(const struct StringComponent *component) {
    switch (component->type) {
        case STRING_COMPONENT_BRACED_SUB:
            free(component->braced_substitution);
            break;
        case STRING_COMPONENT_VAR_SUB:
            free(component->var_substitution);
            break;
        case STRING_COMPONENT_LITERAL:
        case STRING_COMPONENT_DQ:
        case STRING_COMPONENT_SQ:
            free(component->literal);
            break;
        case STRING_COMPONENT_COMMAND_SUBSTITUTION:
            free_program(component->command_substitution);
            free(component->command_substitution);
            break;
    }
}

void free_shell_string(const struct ShellString *str) {
    for (int i = 0; i < str->component_count; ++i)
        free_string_component(&str->components[i]);
    free(str->components);
}

void free_string(const struct String *string) {
    free(string->string);
}

char *grow_string(char *str, int new_size) {
    char *new_str = realloc(str, new_size * sizeof(char));
    if (!new_str) {
        CASH_ERROR(EXIT_FAILURE,
                   "failed to allocate enough memory for string%s\n", "");
        exit(EXIT_FAILURE);
    }
    return new_str;
}

void append(struct String *string, const char *value) {
    append_n(string, value, (int)strlen(value));
}

void append_n(struct String *string, const char *value, int length) {
    string->string = grow_string(string->string, string->length + length);
    memcpy(&string->string[string->length], value, length);
    string->length += length;
}

void append_n_terminate(struct String *string, const char *value, int length) {
    append_n(string, value, length + 1);
    string->string[string->length] = '\0';
}

bool is_null_string(struct ShellString *str) {
    return str->component_count == 0 ||
           (str->component_count == 1 && str->components[0].length == 0 &&
            str->components[0].type != STRING_COMPONENT_COMMAND_SUBSTITUTION);
}

struct String strip(const struct String *string) {
    int length = string->length;
    int start = 0;

    while (start < string->length && isspace(string->string[start])) {
        start++;
        length--;
    }

    int end = string->length - 1;
    while (end >= start && isspace(string->string[end])) {
        end--;
        length--;
    }

    return (struct String){.string = strndup(&string->string[start], length),
                           .length = length};
}
