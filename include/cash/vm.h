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
    int previous_exit_code;
};

struct Vm make_vm(void);
void free_vm(const struct Vm* vm);

int run_program(struct Vm* vm, const struct Program* program);
int run_command(struct Vm* vm, struct Command* command);

#endif  // CASH_VM_H
