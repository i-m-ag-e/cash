#ifndef CASH_STRING_H_
#define CASH_STRING_H_

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

struct ShellString make_string();
void add_string_literal(struct ShellString* str, enum StringComponentType type,
                        const char* literal, int length, int escapes);
void add_string_component(struct ShellString* str,
                          enum StringComponentType type, const char* value,
                          int length);
void free_string_component(struct StringComponent* component);
void free_string(struct ShellString* str);

struct String expand_component(const struct StringComponent* component);
struct String to_string(const struct ShellString* str);

void print_string(const struct ShellString* str);
void print_string_component(const struct StringComponent* component);

#endif
