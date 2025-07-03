#ifndef CASH_VM_H_
#define CASH_VM_H_

#include <cash/ast.h>
#include <pwd.h>

struct VM {
    char* current_prompt;
    uid_t uid;
    struct passwd* userpw;
};

struct VM make_vm(void);
void free_vm(const struct VM* vm);

void run_program(struct VM* vm, const struct Program* program);
void run_command(struct VM* vm, struct Command* command);

#endif
