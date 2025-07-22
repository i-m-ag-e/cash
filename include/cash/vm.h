#ifndef CASH_VM_H
#define CASH_VM_H

#include <cash/ast.h>
#include <cash/job_control.h>
#include <pwd.h>
#include <stdbool.h>
#include <termios.h>

typedef int (*BuiltinFunc)(struct Vm* vm, const struct RawCommand* raw_command);
extern const char* BUILTIN_NAMES[];
extern const BuiltinFunc BUILTIN_FUNCS[];
extern const int BUILTIN_COUNT;

int is_builtin(const char* name);

struct Vm {
    char* current_prompt;
    char* pwd;
    char* old_pwd;
    uid_t uid;
    struct passwd* userpw;
    bool exit;
    int previous_exit_code;

    pid_t shell_pgid;
    struct termios shell_term_state;
    bool repl_mode;
    bool notified_this_time;

    struct Job* job_list;
    struct Process* current_processes;

    int argc;
    char** argv;
};

struct Vm make_vm(int argc, char** argv);
void free_vm(const struct Vm* vm);

int run_program(struct Vm* vm, const struct Program* program);

#endif  // CASH_VM_H
