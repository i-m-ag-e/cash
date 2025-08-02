#ifndef CASH_STRING_H
#define CASH_STRING_H

#include <stdbool.h>

struct Program;

enum StringComponentType {
    STRING_COMPONENT_LITERAL,
    STRING_COMPONENT_DQ,
    STRING_COMPONENT_SQ,
    STRING_COMPONENT_BRACED_SUB,
    STRING_COMPONENT_VAR_SUB,
    STRING_COMPONENT_COMMAND_SUBSTITUTION,
};

struct StringComponent {
    enum StringComponentType type;

    union {
        char* literal;
        char* var_substitution;
        char* braced_substitution;
        struct Program* command_substitution;
    };

    bool quoted;  // used for example when a variable substitution is inside a
                  // double-quoted string
    int escapes;
    int length;
};

struct ShellString {
    struct StringComponent* components;
    int component_count;
    int component_capacity;
};

struct String {
    char* string;
    int length;
};

struct StringView {
    const char* string;
    int length;
};

struct ShellString make_string(void);
void add_string_literal(struct ShellString* str, enum StringComponentType type,
                        const char* literal, int length, int escapes);
void add_string_component(struct ShellString* str,
                          struct StringComponent component);
void add_string_component_from_value(struct ShellString* str,
                                     enum StringComponentType type,
                                     const char* value, int length,
                                     bool quoted);
void add_command_substitution(struct ShellString* str, struct Program* program,
                              bool quoted);

char* grow_string(char* str, int new_size);
void append(struct String* string, const char* value);
void append_n(struct String* string, const char* value, int length);
void append_n_terminate(struct String* string, const char* value, int length);

bool is_null_string(struct ShellString* str);
struct String strip_end(const struct String* string);

void free_string_component(const struct StringComponent* component);
void free_shell_string(const struct ShellString* str);
void free_string(const struct String* string);

#ifndef NDEBUG
void print_string(const struct ShellString* string, int indent);
void print_string_component(const struct StringComponent* component,
                            int indent);
#endif

#endif  // CASH_STRING_H
