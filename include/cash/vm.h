#ifndef CASH_VM_H
#define CASH_VM_H

#include <cash/ast.h>
#include <pwd.h>
#include <stdbool.h>

struct Vm {
    char* current_prompt;
    char* pwd;
    char* old_pwd;
    uid_t uid;
    struct passwd* userpw;
    bool exit;
};

struct Vm make_vm(void);
void free_vm(const struct Vm* vm);

void run_program(struct Vm* vm, const struct Program* program);
void run_command(struct Vm* vm, struct Command* command);

#endif  // CASH_VM_H
