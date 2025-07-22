#include <assert.h>
#include <cash/ast.h>
#include <cash/error.h>
#include <cash/job_control.h>
#include <cash/string.h>
#include <cash/vm.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

extern bool repl_mode;
extern char **environ;

static void setup_redirections(struct RawCommand *raw_command);

static int mark_process_status(struct Vm *vm, pid_t pid, int status);

static void wait_for_job(struct Vm *vm, struct Job *job);
static void put_job_in_foreground(struct Vm *vm, struct Job *job, bool cont);
static void put_job_in_background(struct Job *job, bool cont);

static void mark_job_as_running(struct Job *job);
static void continue_job(struct Vm *vm, struct Job *job, bool foreground);

static void format_job_info_if_bkg(struct Job *job, const char *state);

void free_raw_command(const struct RawCommand *raw_command) {
    free(raw_command->name);
    for (int i = 0; i < raw_command->args_count; ++i) {
        free(raw_command->args[i]);
    }
    free(raw_command->args);
    for (int i = 0; i < raw_command->redirs_count; ++i) {
        free(raw_command->redirs[i].file_name);
    }
    free(raw_command->redirs);
}

void free_process(struct Process *process) {
    free_raw_command(&process->raw_command);
}

void free_job(struct Job *job) {
    struct Process *process = job->first_process;
    while (process != NULL) {
        struct Process *next_process = process->next_process;
        free_process(process);
        free(process);
        process = next_process;
    }
    free(job->command);
}

void add_job(struct Vm *vm, struct Job *job) {
    job->next_job = vm->job_list;
    vm->job_list = job;
    job->job_id = job->next_job ? job->next_job->job_id + 1 : 1;
}

struct Job *get_job_by_id(struct Vm *vm, int job_id) {
    struct Job *job = vm->job_list;
    while (job != NULL) {
        if (job->job_id == job_id) {
            return job;
        }
        job = job->next_job;
    }
    return NULL;
}

bool job_is_stopped(const struct Job *job) {
    struct Process *process = job->first_process;

    for (; process != NULL; process = process->next_process) {
        if (!process->completed && !process->stopped) {
            return false;
        }
    }
    return true;
}

bool job_is_completed(const struct Job *job) {
    struct Process *process = job->first_process;

    for (; process != NULL; process = process->next_process) {
        if (!process->completed) {
            return false;
        }
    }
    return true;
}

bool job_was_terminated(const struct Job *job) {
    struct Process *process = job->first_process;

    for (; process != NULL; process = process->next_process) {
        if (!process->terminated) {
            return false;
        }
    }
    return true;
}

void format_job_info(struct Job *job, const char *state, FILE *stream) {
    fprintf(stream, "[%d] (%d) %s\t\t%s\n", job->job_id, (int)job->pgid, state,
            job->command);
}

static void format_job_info_if_bkg(struct Job *job, const char *state) {
    if (job->background && repl_mode) {
        format_job_info(job, state, stderr);
    }
}

static void setup_redirections(struct RawCommand *raw_command) {
    for (int i = 0; i < raw_command->redirs_count; ++i) {
        const struct RawRedirection *redir = &raw_command->redirs[i];
        int left = redir->left;
        int right = redir->right;
        assert(left != -1);

        if (redir->file_name == NULL) {
            assert(right != -1);
            if (dup2(right, left) == -1) {
                CASH_PERROR(EXIT_FAILURE, "dup2",
                            "could not duplicate fd %d to %d", right, left);
                exit(EXIT_FAILURE);
            }
        } else {
            assert(right == -1);
            int fd = open(redir->file_name, redir->flags, 0644);
            if (fd == -1) {
                CASH_PERROR(EXIT_FAILURE, "open", "could not open %s",
                            redir->file_name);
                exit(EXIT_FAILURE);
            }

            right = fd;
            if (dup2(right, left) == -1) {
                CASH_PERROR(EXIT_FAILURE, "dup2",
                            "could not duplicate fd %d to %d", right, left);
                exit(EXIT_FAILURE);
            }

            close(fd);
        }

        if (redir->err_to_out) {
            if (dup2(STDOUT_FILENO, STDERR_FILENO) == -1) {
                CASH_PERROR(EXIT_FAILURE, "dup2",
                            "could not duplicate stdout to stderr%s", "");
                exit(EXIT_FAILURE);
            }
        }
    }
}

void launch_process(struct Vm *vm, struct Process *process, pid_t pgid,
                    pid_t pid, int in, int out, int err, bool foreground) {
    int builtin = is_builtin(process->raw_command.name);
    if (vm->repl_mode && builtin == -1) {
        if (pgid == 0) {
            pgid = pid;
        }
        setpgid(pid, pgid);
        if (foreground) {
            tcsetpgrp(STDIN_FILENO, pgid);
        }

        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
    }

    if (in != STDIN_FILENO) {
        dup2(in, STDIN_FILENO);
        close(in);
    }
    if (out != STDOUT_FILENO) {
        dup2(out, STDOUT_FILENO);
        close(out);
    }
    if (err != STDERR_FILENO) {
        dup2(err, STDERR_FILENO);
        close(err);
    }

    setup_redirections(&process->raw_command);

    if (builtin != -1) {
        int res = BUILTIN_FUNCS[builtin](vm, &process->raw_command);
        exit(res);
    }

    execve(process->raw_command.name, process->raw_command.args, environ);
    CASH_PERROR(EXIT_FAILURE, "execve",
                "could not execute %s: ", process->raw_command.name);
    exit(EXIT_FAILURE);
}

void launch_job(struct Vm *vm, struct Job *job, bool foreground) {
    struct Process *process;
    pid_t pid;
    int pipefd[2];
    int in = job->stdin;
    int out = job->stdout;

    add_job(vm, job);

    for (process = job->first_process; process != NULL;
         process = process->next_process) {
        if (process->next_process != NULL) {
            if (pipe(pipefd) == -1) {
                CASH_PERROR(EXIT_FAILURE, "pipe",
                            "could not create pipe for job%s", "");
                exit(EXIT_FAILURE);
            }
            out = pipefd[1];
        } else {
            out = job->stdout;
        }

        pid = fork();
        if (pid < 0) {
            CASH_PERROR(EXIT_FAILURE, "fork",
                        "could not fork process for job%s", "");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            launch_process(vm, process, job->pgid, pid, in, out, job->stderr,
                           foreground);
        } else {
            process->pid = pid;
            if (repl_mode) {
                if (job->pgid == 0)
                    job->pgid = pid;
                setpgid(pid, job->pgid);
            }
        }

        if (in != job->stdin)
            close(in);
        if (out != job->stdout)
            close(out);

        in = pipefd[0];
    }

    format_job_info_if_bkg(job, "launched");

    if (!repl_mode) {
        if (!foreground) {
            fprintf(stderr,
                    YELLOW
                    "trying to run job %d in background in non-interactive "
                    "mode%s\n" RESET,
                    job->job_id, "");
        }
        wait_for_job(vm, job);
    } else if (foreground) {
        put_job_in_foreground(vm, job, false);
    } else {
        put_job_in_background(job, false);
    }
}

static void wait_for_job(struct Vm *vm, struct Job *job) {
    int status;
    pid_t pid;

    do {
        pid = waitpid(WAIT_ANY, &status, WUNTRACED);
    } while (!mark_process_status(vm, pid, status) && !job_is_stopped(job) &&
             !job_is_completed(job));
}

static void put_job_in_foreground(struct Vm *vm, struct Job *job, bool cont) {
    job->background = false;
    tcsetpgrp(STDIN_FILENO, job->pgid);

    if (cont) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &job->term_state);
        if (kill(-job->pgid, SIGCONT) == -1) {
            CASH_PERROR(EXIT_FAILURE, "kill", "could not continue job %ld",
                        (long)job->pgid);
        }
    }

    wait_for_job(vm, job);
    tcsetpgrp(STDIN_FILENO, vm->shell_pgid);

    tcgetattr(STDIN_FILENO, &job->term_state);
    tcsetattr(STDIN_FILENO, TCSADRAIN, &vm->shell_term_state);
}

static void put_job_in_background(struct Job *job, bool cont) {
    job->background = true;
    if (cont) {
        if (kill(-job->pgid, SIGCONT) == -1) {
            CASH_PERROR(EXIT_FAILURE, "kill", "could not continue job %ld",
                        (long)job->pgid);
        }
    }
}

static int mark_process_status(struct Vm *vm, pid_t pid, int status) {
    struct Job *job;
    struct Process *process;

    if (pid == 0 || errno == ECHILD) {
        return -1;  // No more processes to wait for
    } else if (pid < 0) {
        CASH_PERROR(EXIT_FAILURE, "waitpid", "could not wait for process %ld\n",
                    (long)pid);
        return -1;
    }

    for (job = vm->job_list; job != NULL; job = job->next_job) {
        for (process = job->first_process; process != NULL;
             process = process->next_process) {
            if (process->pid == pid) {
                process->status = status;

                if (WIFSTOPPED(status)) {
                    process->stopped = true;
                } else {
                    process->completed = true;
                    if (WIFSIGNALED(status)) {
                        process->terminated = true;
                        fprintf(stderr, "Process %ld terminated by signal %d\n",
                                (long)pid, WTERMSIG(status));
                    }
                }

                return 0;
            }
        }
    }

    CASH_ERROR(EXIT_FAILURE, "No process with PID %d\n", (int)pid);
    return -1;
}

int list_jobs(struct Vm *vm, const struct RawCommand *raw_command) {
    (void)raw_command;  // for now, does nothing; can be extended to handle the
                        // normal options that can be passed
    struct Job *job = vm->job_list;

    if (job == NULL) {
        return 0;
    }

    update_status(vm);
    for (; job != NULL; job = job->next_job) {
        if (job_was_terminated(job)) {
            job->notified = true;
            format_job_info(job, "Terminated", stdout);
        } else if (job_is_completed(job)) {
            format_job_info(job, "Completed", stdout);
        } else if (job_is_stopped(job)) {
            job->notified = true;
            format_job_info(job, "Stopped", stdout);
        } else {
            format_job_info(job, "Running", stdout);
        }
    }
    remove_completed_jobs(vm);
    vm->notified_this_time = true;
    return 0;
}

int fg(struct Vm *vm, const struct RawCommand *raw_command) {
    if (!repl_mode) {
        CASH_ERROR(EXIT_FAILURE,
                   "fg: no job control in non-interactive mode%s\n", "");
        return 1;
    }
    if (vm->job_list == NULL) {
        CASH_ERROR(EXIT_FAILURE, "fg: no current job%s\n", "");
        return 1;
    }

    int job_id = -1;
    if (raw_command->args_count > 1) {
        long n;
        char *end;
        if (raw_command->args[1][0] == '%') {
            n = strtol(raw_command->args[1] + 1, &end, 10);
        } else {
            n = strtol(raw_command->args[1], &end, 10);
        }

        if (*end != '\0' || n < 1 || n > INT_MAX) {
            CASH_ERROR(EXIT_FAILURE, "fg: invalid job id `%s`\n",
                       raw_command->args[1]);
            return 1;
        }
        job_id = (int)n;
    }

    struct Job *job = job_id == -1 ? vm->job_list : get_job_by_id(vm, job_id);
    if (job == NULL) {
        CASH_ERROR(EXIT_FAILURE, "fg: no such job `%s`\n",
                   raw_command->args_count > 1 ? raw_command->args[1] : "");
        return 1;
    }

    if (repl_mode)
        printf("%s\n", job->command);
    continue_job(vm, job, true);
    return 0;
}

void update_status(struct Vm *vm) {
    pid_t pid;
    int status;

    do {
        pid = waitpid(WAIT_ANY, &status, WUNTRACED | WNOHANG);
    } while (mark_process_status(vm, pid, status) == 0);
}

void do_job_notification(struct Vm *vm) {
    struct Job *job = vm->job_list;
    struct Job *jlast = NULL, *jnext;

    update_status(vm);

    for (; job != NULL; job = jnext) {
        jnext = job->next_job;

        if (job_was_terminated(job)) {
            if (!job->notified)
                format_job_info_if_bkg(job, "Terminated");
            if (jlast != NULL) {
                // delete from list
                jlast->next_job = jnext;
            } else {
                vm->job_list = jnext;
            }
            free_job(job);
            free(job);
        } else if (job_is_completed(job)) {
            format_job_info_if_bkg(job, "Completed");
            if (jlast != NULL) {
                // delete form list
                jlast->next_job = jnext;
            } else {
                vm->job_list = jnext;
            }
            free_job(job);
            free(job);
        } else if (job_is_stopped(job) && !job->notified) {
            format_job_info_if_bkg(job, "Stopped");
            job->notified = true;
            jlast = job;
        } else {
            jlast = job;
        }
    }
}

void remove_completed_jobs(struct Vm *vm) {
    struct Job *job = vm->job_list;
    struct Job *jlast = NULL, *jnext;

    for (; job != NULL; job = jnext) {
        jnext = job->next_job;

        if (job_is_completed(job)) {
            if (jlast != NULL) {
                // delete from list
                jlast->next_job = jnext;
            } else {
                vm->job_list = jnext;
            }
            free_job(job);
            free(job);
        } else {
            jlast = job;
        }
    }
}

static void mark_job_as_running(struct Job *job) {
    job->notified = false;
    for (struct Process *process = job->first_process; process != NULL;
         process = process->next_process) {
        process->stopped = false;
    }
}

static void continue_job(struct Vm *vm, struct Job *job, bool foreground) {
    mark_job_as_running(job);
    if (foreground) {
        put_job_in_foreground(vm, job, true);
    } else {
        put_job_in_background(job, true);
    }
}
