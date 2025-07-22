#ifndef CASH_JOB_CONTROL_H
#define CASH_JOB_CONTROL_H

#include <cash/ast.h>
#include <cash/string.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <termios.h>

struct RawRedirection {
    int flags;
    int left;
    int right;
    int err_to_out;
    char *file_name;
};

struct RawCommand {
    char *name;
    char **args;
    int args_count;
    struct RawRedirection *redirs;
    int redirs_count;
};
void free_raw_command(const struct RawCommand *raw_command);

struct Process {
    struct Process *next_process;
    struct RawCommand raw_command;
    pid_t pid;
    int status;
    bool completed;
    bool stopped;
    bool terminated;
};
void free_process(struct Process *process);

struct Vm;

struct Job {
    struct Job *next_job;
    struct Process *first_process;
    char *command;

    int job_id;
    pid_t pgid;

    bool background;
    bool notified;
    struct termios term_state;

    int stdout, stdin, stderr;
};
void free_job(struct Job *job);

void add_job(struct Vm *vm, struct Job *job);
struct Job *get_job_by_id(struct Vm *vm, int job_id);

bool job_is_stopped(const struct Job *job);
bool job_is_completed(const struct Job *job);
bool job_was_terminated(const struct Job *job);

void remove_completed_jobs(struct Vm *vm);
void update_status(struct Vm *vm);
void do_job_notification(struct Vm *vm);
int list_jobs(struct Vm *vm, const struct RawCommand *raw_command);
int fg(struct Vm *vm, const struct RawCommand *raw_command);

void launch_process(struct Vm *vm, struct Process *process, pid_t pgid,
                    pid_t pid, int in, int out, int err, bool foreground);
void launch_job(struct Vm *vm, struct Job *job, bool foreground);

void format_job_info(struct Job *job, const char *state, FILE *stream);

#endif  // CASH_JOB_CONTROL_H
