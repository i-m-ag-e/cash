#include <assert.h>
#include <cash/error.h>
#include <cash/memory.h>
#include <cash/string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>

extern bool REPL_MODE;

static void add_component(struct ShellString *str,
                          struct StringComponent comp) {
    ADD_LIST(str, component_count, component_capacity, components, comp);
}

struct ShellString make_string() {
    return (struct ShellString){
        .component_capacity = 0, .component_count = 0, .components = NULL};
}

void add_string_literal(struct ShellString *str, enum StringComponentType type,
                        const char *literal, int length, int escapes) {
    assert(type == STRING_COMPONENT_LITERAL || type == STRING_COMPONENT_DQ ||
           type == STRING_COMPONENT_SQ);
    add_component(str,
                  (struct StringComponent){.literal = strndup(literal, length),
                                           .type = STRING_COMPONENT_LITERAL,
                                           .length = length,
                                           .escapes = escapes});
}

void add_string_component(struct ShellString *str,
                          enum StringComponentType type, const char *value,
                          int length) {
    assert(type == STRING_COMPONENT_BRACED_SUB ||
           type == STRING_COMPONENT_VAR_SUB || type == STRING_COMPONENT_DQ ||
           type == STRING_COMPONENT_SQ);
    char *val = strndup(value, length);
    struct StringComponent component = {.type = type, .length = length};
    switch (type) {
        case STRING_COMPONENT_BRACED_SUB:
            component.braced_substitution = val;
            break;
        case STRING_COMPONENT_VAR_SUB:
            component.var_substitution = val;
            break;
        case STRING_COMPONENT_DQ:
        case STRING_COMPONENT_SQ:
            component.literal = val;
            break;
        default:
            break;
    }

    add_component(str, component);
}

void free_string_component(struct StringComponent *component) {
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
            assert(false);
            break;
    }
}

void free_shell_string(struct ShellString *str) {
    for (int i = 0; i < str->component_count; ++i)
        free_string_component(&str->components[i]);
    free(str->components);
}

void free_string(const struct String* string) {
    free(string->string);
}

static char *grow_string(char *str, int new_size) {
    str = realloc(str, new_size * sizeof(char));
    if (!str) {
        cash_error(EXIT_FAILURE,
                   "failed to allocate enough memory for string%s\n", "");
        exit(EXIT_FAILURE);
    }
    return str;
}

struct String expand_component(const struct StringComponent *component) {
    char *string = NULL;

    switch (component->type) {
        case STRING_COMPONENT_LITERAL:
        case STRING_COMPONENT_DQ: {
            int i, start = 0, total_size = 0;
            for (i = 0; i < component->length; ++i) {
                if (component->literal[i] == '\\') {
                    if (i + 1 == component->length)
                        break;

                    int length = i - start + 1;
                    string = grow_string(string, total_size + length);
                    strncpy(&string[total_size], &component->literal[start],
                            length - 1);
                    string[total_size + length - 1] = component->literal[i + 1];
                    total_size += length;
                    start = i + 2;
                    i++;
                }
            }
            if (start != i) {
                int length = i - start;
                string = grow_string(string, total_size + length);
                strncpy(&string[total_size], &component->literal[start],
                        length);
                total_size += length;
            }

            return (struct String){string, total_size};
        }

        case STRING_COMPONENT_SQ:
            return (struct String){
                strndup(component->literal, component->length),
                component->length};

        default:
            return (struct String){.string = "", .length = 0};
    }
}

// TODO: can make expand_component write directly to a single allocated string,
// instead of allocating a new one for each compoenent and then freeing it
struct String to_string(const struct ShellString *string) {
    char *str = NULL;
    int total_size = 0;

    for (int i = 0; i < string->component_count; ++i) {
        const struct String expanded = expand_component(&string->components[i]);
        // printf("expanded (%d): %.*s\n", expanded.length, expanded.length,
        //        expanded.string);
        const int is_last_comp = i + 1 == string->component_count;
        const int new_alloc_size = total_size + expanded.length + is_last_comp;
        // printf("new alloc size: %d\n", new_alloc_size);
        str = grow_string(str, new_alloc_size);
        // printf("copying at position %d in (%d) \"%.*s\"\n", total_size,
        //        total_size, total_size, str);
        strncpy(&str[total_size], expanded.string, expanded.length);
        total_size += expanded.length;

        if (is_last_comp) {
            // printf("setting %d to NULL\n", new_alloc_size);
            str[new_alloc_size - 1] = '\0';
        }

        free(expanded.string);
    }
    // printf("final str: %s\n", str);
    // for (int i = 0; i < total_size + 1; ++i) {
    //     printf("%d: (%d) %c\n", i, str[i], str[i]);
    // }

    return (struct String){.string = str, .length = total_size};
}
