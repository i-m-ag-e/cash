#include <assert.h>
#include <cash/ast.h>
#include <cash/error.h>
#include <cash/job_control.h>
#include <cash/memory.h>
#include <cash/string.h>
#include <cash/util.h>
#include <cash/vm.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CHECK_ALLOC(ptr)                                                  \
    do {                                                                  \
        if (!(ptr)) {                                                     \
            CASH_ERROR(EXIT_FAILURE, "Memory allocation failed%s\n", ""); \
            exit(EXIT_FAILURE);                                           \
        }                                                                 \
    } while (0)

extern bool is_repl_mode;
extern char **environ;

static void make_process(struct Vm *vm, struct Command *command,
                         struct Process *process);
static int make_process_list(struct Vm *vm, struct Expr *expr,
                             struct Process ***process_list);
static int make_job(struct Vm *vm, struct Expr *expr, struct Job *job);

static struct RawRedirection get_redirection(struct Vm *vm,
                                             const struct Redirection *redir);
static int get_final_command(struct Vm *vm, struct Command *command,
                             struct RawCommand *raw_command);

static int exec_expression(struct Vm *vm, struct Expr *expr, struct Job **job);

static void backup_fds(int *saved_in, int *saved_out, int *saved_err);
static void restore_fds(int saved_in, int saved_out, int saved_err);
static int run_command(struct Vm *vm, struct Expr *expr, struct Job **job);

static void update_prompt(struct Vm *vm);

static bool is_path(const char *cmd);
static bool is_executable(const char *path);

static char *find_in_path(const char *cmd);

struct UnsplitString {
    struct String expansion;
    bool is_expanded;
    bool was_quoted;
};

static struct UnsplitString expand_component(
    struct Vm *vm, const struct StringComponent *component);
static struct UnsplitString expand_var_sub(
    const struct Vm *vm, const struct StringComponent *component);
static struct UnsplitString expand_command_sub(
    struct Vm *vm, const struct StringComponent *component);
static struct UnsplitString expand_string(
    const struct Vm *vm, const struct StringComponent *component);
static struct String to_string(struct Vm *vm, const struct ShellString *string);

static int tilde_expansion(const struct Vm *vm, const char *source, int len,
                           char **dest, int *total_size);

struct WordList {
    char **words;
    int word_count;
    int word_capacity;
};
static struct String into_words(struct Vm *vm, const struct ShellString *string,
                                struct WordList *word_list);

static int change_dir(struct Vm *vm, const struct RawCommand *command);
static int exit_shell(struct Vm *vm, const struct RawCommand *raw_command);

static const bool kIsEscapableInDQ[128] = {
    ['"'] = true,
    ['\\'] = true,
    ['$'] = true,
    ['`'] = true,
};

const char *BUILTIN_NAMES[] = {"cd", "exit", "jobs", "fg"};
const BuiltinFunc BUILTIN_FUNCS[] = {change_dir, exit_shell, list_jobs, fg};
const int BUILTIN_COUNT = sizeof(BUILTIN_NAMES) / sizeof(BUILTIN_NAMES[0]);

static void handle_signal(int sig) {
    CASH_DEBUG(RED "yo %d (%s) (pid: %d) (control with: %d)\n" RESET, sig,
               strsignal(sig), getpid(), tcgetpgrp(STDIN_FILENO));
    signal(sig, SIG_IGN);
}

struct Vm make_vm(int argc, char **argv, bool repl_mode, bool background) {
    struct passwd *userpw = getpwuid(getuid());
    char *cwd = get_cwd();

    pid_t shell_pgid = 0;
    struct termios term_state = {0};
    // assert(!background || !repl_mode);
    if (repl_mode) {
        while (tcgetpgrp(STDIN_FILENO) != (shell_pgid = getpgrp())) {
            kill(-shell_pgid, SIGTTIN);
        }

        signal(SIGINT, handle_signal);
        signal(SIGQUIT, handle_signal);
        signal(SIGTTOU, handle_signal);
        signal(SIGSTOP, handle_signal);
        signal(SIGTSTP, handle_signal);
        signal(SIGTTIN, handle_signal);
        signal(SIGCHLD, SIG_DFL);

        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) == -1) {
            CASH_PERROR(EXIT_FAILURE, "setpgid",
                        "could not set process group id%s", "");
            exit(EXIT_FAILURE);
        }

        if (!background) {
            tcsetpgrp(STDIN_FILENO, shell_pgid);
        }
        tcgetattr(STDIN_FILENO, &term_state);
    }

    setenv("PWD", cwd, 1);
    setenv("OLDPWD", cwd, 1);

    return (struct Vm){
        .current_prompt = make_new_prompt(userpw->pw_name),
        .pwd = cwd,
        .old_pwd = strdup(cwd),
        .uid = userpw->pw_uid,
        .userpw = userpw,
        .exit = false,
        .previous_exit_code = 0,

        .repl_mode = repl_mode,
        .shell_pgid = shell_pgid,
        .shell_term_state = term_state,
        .is_subshell = false,

        .argc = argc,
        .argv = argv,
    };
}

void free_vm(const struct Vm *vm) {
    struct Job *next_job;
    for (struct Job *job = vm->job_list; job != NULL; job = next_job) {
        next_job = job->next_job;
        free_job(job);
        free(job);
    }
    free(vm->current_prompt);
    free(vm->old_pwd);
    free(vm->pwd);
}

int run_program(struct Vm *vm, const struct Program *program) {
    for (int i = 0; i < program->statement_count; ++i) {
        struct Job *job = NULL;

        exec_expression(vm, &program->statements[i].expr, &job);

        if (vm->is_subshell) {
            if (vm->job_list != job)
                continue;
            // while (!job_is_completed(job) && job_is_stopped(job)) {
            //     CASH_DEBUG(RED "sombody help me\n" RESET);
            //     kill(-vm->shell_pgid, SIGSTOP);
            //     put_job_in_foreground(vm, job, true);
            //     CASH_DEBUG(RED "aaaaaaaaaaaaaaah\n" RESET);
            //     do_job_notification(vm);
            //     break;
            // }
        }

        // run_command(vm, &program->statements[i].command);
    }
    if (vm->is_subshell)
        exit(vm->previous_exit_code);

    if (!vm->notified_this_time)
        do_job_notification(vm);
    vm->notified_this_time = false;
    return vm->previous_exit_code;
}

static int get_final_command(struct Vm *vm, struct Command *command,
                             struct RawCommand *raw_command) {
    if (command->is_subshell) {
        *raw_command = (struct RawCommand){.is_subshell = true,
                                           .as_subshell = command->as_subshell};
    } else {
        char *executable = NULL;
        char **args = NULL;
        struct WordList word_list = {
            .words = NULL, .word_count = 0, .word_capacity = 0};

        if (command->as_cmd.command_name.component_count != 0) {
            into_words(vm, &command->as_cmd.command_name, &word_list);
            const char *command_name = word_list.words[0];
            CASH_DEBUG("Name: %s\n", command_name);

            if (strcmp(command_name, "ls") == 0) {
                struct ShellString new_arg = {.components = NULL,
                                              .component_count = 0,
                                              .component_capacity = 0};
                add_string_literal(&new_arg, STRING_COMPONENT_LITERAL,
                                   "--color=auto", strlen("--color=auto"), 0);
                add_argument(&command->as_cmd.arguments, new_arg);
            }

            for (int i = 0; i < command->as_cmd.arguments.argument_count; ++i) {
                into_words(vm, &command->as_cmd.arguments.arguments[i],
                           &word_list);
            }
            ADD_LIST(&word_list, word_count, word_capacity, words, NULL,
                     char *);

            if (is_path(command_name)) {
                if (!is_executable(command_name)) {
                    CASH_ERROR(EXIT_FAILURE,
                               "the path `%s` is not an executable\n",
                               command_name);
                    return EXIT_FAILURE;
                }
                executable = strdup(command_name);
            } else {
                char *res = find_in_path(command_name);
                if (res == NULL) {
                    executable = strdup(command_name);
                } else {
                    executable = res;
                }
            }
        }

        CASH_DEBUG("Name: %s\n", executable);
        for (int i = 0; i < word_list.word_count; ++i) {
            CASH_DEBUG("arg %d: %s\n", i, word_list.words[i]);
        }
        *raw_command = (struct RawCommand){
            .is_subshell = false,
            .as_cmd = {
                .name = executable,
                .args = word_list.words,
                .args_count = command->as_cmd.arguments.argument_count + 1}};
    }

    struct RawRedirection *redirs =
        malloc((command->redirection_count) * sizeof(struct RawRedirection));
    CHECK_ALLOC(redirs);
    for (int i = 0; i < command->redirection_count; ++i) {
        struct Redirection *redir = &command->redirections[i];
        redirs[i] = get_redirection(vm, redir);
    }

    raw_command->redirs_count = command->redirection_count;
    raw_command->redirs = redirs;

    return 0;
}

static struct RawRedirection get_redirection(struct Vm *vm,
                                             const struct Redirection *redir) {
    struct RawRedirection raw_redir = {
        .left = redir->left,
        .right = redir->right,
        .err_to_out = false,
        .file_name = NULL,
        .flags = -1,
    };
    if (redir->file_name.component_count != 0) {
        raw_redir.file_name = to_string(vm, &redir->file_name).string;
    }

    switch (redir->type) {
        case REDIRECT_OUT:
        case REDIRECT_OUTERR:
            if (redir->left == -1)
                raw_redir.left = STDOUT_FILENO;

            raw_redir.flags = O_WRONLY | O_CREAT | O_TRUNC;
            raw_redir.err_to_out = (redir->type == REDIRECT_OUTERR);
            break;

        case REDIRECT_IN:
            if (redir->left == -1)
                raw_redir.left = STDIN_FILENO;
            raw_redir.flags = O_RDONLY;
            break;

        case REDIRECT_APPEND_OUT:
        case REDIRECT_APPEND_OUTERR:
            if (redir->left == -1)
                raw_redir.left = STDOUT_FILENO;
            raw_redir.flags = O_WRONLY | O_CREAT | O_APPEND;
            raw_redir.err_to_out = (redir->type == REDIRECT_APPEND_OUTERR);
            break;

        case REDIRECT_OUT_DUPLICATE:
            if (redir->left == -1)
                raw_redir.left = STDOUT_FILENO;
            assert(raw_redir.right != -1);
            raw_redir.flags = O_WRONLY | O_CREAT | O_TRUNC;
            break;

        case REDIRECT_INOUT:
            if (redir->left == -1)
                raw_redir.left = STDIN_FILENO;
            assert(redir->right == -1);
            raw_redir.flags = O_RDWR | O_CREAT;
            break;

        default:
            CASH_ERROR(EXIT_FAILURE, "unimplemented redirection type%s", "");
            exit(EXIT_FAILURE);
    }
    return raw_redir;
}

static void backup_fds(int *saved_in, int *saved_out, int *saved_err) {
    *saved_in = dup(STDIN_FILENO);
    if (*saved_in == -1) {
        CASH_PERROR(EXIT_FAILURE, "dup", "could not duplicate stdin%s", "");
        exit(EXIT_FAILURE);
    }
    *saved_out = dup(STDOUT_FILENO);
    if (*saved_out == -1) {
        CASH_PERROR(EXIT_FAILURE, "dup", "could not duplicate stdout%s", "");
        exit(EXIT_FAILURE);
    }
    *saved_err = dup(STDERR_FILENO);
    if (*saved_err == -1) {
        CASH_PERROR(EXIT_FAILURE, "dup", "could not duplicate stderr%s", "");
        exit(EXIT_FAILURE);
    }
}

static void restore_fds(int saved_in, int saved_out, int saved_err) {
    if (dup2(saved_in, STDIN_FILENO) == -1) {
        CASH_PERROR(EXIT_FAILURE, "dup2", "could not restore stdin%s", "");
        exit(EXIT_FAILURE);
    }
    if (dup2(saved_out, STDOUT_FILENO) == -1) {
        CASH_PERROR(EXIT_FAILURE, "dup2", "could not restore stdout%s", "");
        exit(EXIT_FAILURE);
    }
    if (dup2(saved_err, STDERR_FILENO) == -1) {
        CASH_PERROR(EXIT_FAILURE, "dup2", "could not restore stderr%s", "");
        exit(EXIT_FAILURE);
    }

    close(saved_in);
    close(saved_out);
    close(saved_err);
}

int run_command(struct Vm *vm, struct Expr *expr, struct Job **jobp) {
    if (!vm->repl_mode)
        remove_completed_jobs(vm);

    struct Job *job = malloc(sizeof(struct Job));
    CHECK_ALLOC(job);
    make_job(vm, expr, job);
    *jobp = job;
    assert(job->first_process->next_process == NULL);
    for (struct Process *process = job->first_process; process != NULL;
         process = process->next_process) {
        if (process->raw_command.is_subshell) {
            CASH_DEBUG("subshell command: %s\n",
                       process->raw_command.as_subshell.text);
        } else {
            CASH_DEBUG("command: %s\n", process->raw_command.as_cmd.name);
            CASH_DEBUG("proc: %s\n", process->raw_command.as_cmd.name);
            for (int i = 0; i < process->raw_command.as_cmd.args_count; ++i) {
                CASH_DEBUG("\tArg %d: %s\n", i,
                           process->raw_command.as_cmd.args[i]);
            }
        }
    }

    struct RawCommand *raw_command = &job->first_process->raw_command;
    int builtin =
        raw_command->is_subshell ? -1 : is_builtin(raw_command->as_cmd.name);
    CASH_DEBUG("builtin: %d\n", builtin);

    if (builtin != -1) {
        int saved_in, saved_out, saved_err;
        backup_fds(&saved_in, &saved_out, &saved_err);
        setup_redirections(raw_command);
        int res = BUILTIN_FUNCS[builtin](vm, raw_command);
        restore_fds(saved_in, saved_out, saved_err);

        vm->previous_exit_code = res % 0xFF;
        return vm->previous_exit_code;
    }

    launch_job(vm, job, !expr->background);

    CASH_DEBUG("oui\n");
    if (!expr->background)
        vm->previous_exit_code = job->first_process->status % 0xFF;
    CASH_DEBUG("vm->previous_exit_code: %d\n", vm->previous_exit_code);
    return vm->previous_exit_code;
}

static int exec_expression(struct Vm *vm, struct Expr *expr,
                           struct Job **jobp) {
    switch (expr->type) {
        case EXPR_COMMAND:
            return run_command(vm, expr, jobp);

        case EXPR_NOT: {
            if (exec_expression(vm, expr->binary.left, jobp) == 0) {
                vm->previous_exit_code = 1;
                return 1;
            }
            vm->previous_exit_code = 0;
            return 0;
        }

        case EXPR_AND:
        case EXPR_OR: {
            const int left = exec_expression(vm, expr->binary.left, jobp);
            if ((left == 0 && expr->type == EXPR_AND) ||
                (left != 0 && expr->type == EXPR_OR)) {
                vm->previous_exit_code =
                    exec_expression(vm, expr->binary.right, jobp);
                return vm->previous_exit_code;
            } else {
                vm->previous_exit_code = left;
                return left;
            }
        }

        case EXPR_PIPELINE: {
            struct Job *job = malloc(sizeof(struct Job));
            CHECK_ALLOC(job);
            int res = make_job(vm, expr, job);
            *jobp = job;
            int i = 0;
            for (struct Process *process = job->first_process; process != NULL;
                 (process = process->next_process), ++i) {
                struct RawCommand *raw_command = &process->raw_command;
                CASH_DEBUG("Command %d:\n\tName: %s\n", i,
                           raw_command->as_cmd.name);
                for (int j = 0; j < raw_command->as_cmd.args_count; ++j) {
                    CASH_DEBUG("\tArg %d: %s\n", j,
                               raw_command->as_cmd.args[j]);
                }
            }
            CASH_DEBUG("res: %d\n", res);
            if (res == 0) {
                launch_job(vm, job, !expr->background);
                if (!expr->background) {
                    assert(job_is_completed(job));
                    for (struct Process *process = job->first_process;
                         process != NULL; process = process->next_process) {
                        vm->previous_exit_code = process->status % 0xFF;
                        res = vm->previous_exit_code;
                    }
                }
            } else
                CASH_ERROR(EXIT_FAILURE, "could not make job%s", "");
            return res;
        }

        default:
            CASH_ERROR(EXIT_FAILURE, "unimplemented%s", "");
            exit(EXIT_FAILURE);
    }
}

static void make_process(struct Vm *vm, struct Command *command,
                         struct Process *process) {
    struct RawCommand raw_command;
    get_final_command(vm, command, &raw_command);

    *process = (struct Process){
        .next_process = NULL,
        .raw_command = raw_command,
        .pid = 0,
        .status = 0,
        .completed = false,
        .stopped = false,
    };
}

static int make_process_list(struct Vm *vm, struct Expr *expr,
                             struct Process ***process_list) {
    switch (expr->type) {
        case EXPR_COMMAND: {
            struct Process *new_process = malloc(sizeof(struct Process));
            CHECK_ALLOC(new_process);
            make_process(vm, &expr->command, new_process);
            **process_list = new_process;
            *process_list = &new_process->next_process;
            return 0;
        }

        case EXPR_PIPELINE: {
            int l = make_process_list(vm, expr->binary.left, process_list);
            if (l != 0)
                return l;
            return make_process_list(vm, expr->binary.right, process_list);
        }

        default:
            CASH_ERROR(EXIT_FAILURE,
                       "(internal error) impossible expression type to "
                       "make_process_list%s",
                       "");
            exit(EXIT_FAILURE);
    }
}

static int make_subshell_job(struct Vm *vm, struct Program *program, int in,
                             int out, int err, struct Job *jobp) {
    struct Process *process = malloc(sizeof(struct Process));
    CHECK_ALLOC(process);
    *process = (struct Process){
        .next_process = NULL,
        .raw_command =
            (struct RawCommand){
                .is_subshell = true,
                .as_subshell = *program,
            },
        .pid = 0,
        .status = 0,
        .completed = false,
        .stopped = false,
    };
    *jobp = (struct Job){
        .next_job = NULL,
        .first_process = process,
        .command = strndup(program->text, program->text_length),
        .pgid = 0,
        .notified = false,
        .term_state = vm->shell_term_state,
        .stdin = in,
        .stdout = out,
        .stderr = err,
    };
    return 0;
}

static int make_job(struct Vm *vm, struct Expr *expr, struct Job *jobp) {
    struct Job job = {
        .first_process = NULL,
        .next_job = NULL,
        .command = strndup(expr->expr_text.string, expr->expr_text.length),
        .pgid = 0,
        .notified = false,
        .term_state = vm->shell_term_state,
        .stdout = STDOUT_FILENO,
        .stdin = STDIN_FILENO,
        .stderr = STDERR_FILENO,
    };
    struct Process **proc_list = &job.first_process;
    make_process_list(vm, expr, &proc_list);
    *jobp = job;
    return 0;
}

struct UnsplitString expand_component(struct Vm *vm,
                                      const struct StringComponent *component) {
    switch (component->type) {
        case STRING_COMPONENT_VAR_SUB:
            return expand_var_sub(vm, component);
        case STRING_COMPONENT_LITERAL:
        case STRING_COMPONENT_DQ:
            return expand_string(vm, component);
        case STRING_COMPONENT_SQ:
            return (struct UnsplitString){
                .expansion = {strndup(component->literal, component->length),
                              component->length},
                false,
                true};
        case STRING_COMPONENT_COMMAND_SUBSTITUTION:
            return expand_command_sub(vm, component);

        default:
            return (struct UnsplitString){.expansion = {"", 0}, false, false};
    }
}

static struct UnsplitString expand_var_sub(
    const struct Vm *vm, const struct StringComponent *component) {
    if (component->length == 1 && component->var_substitution[0] == '?') {
        return (struct UnsplitString){
            .expansion = number_to_string(vm->previous_exit_code),
            .is_expanded = true,
            .was_quoted = component->quoted};
    }

    if (component->length == 1 && component->var_substitution[0] == '#') {
        return (struct UnsplitString){number_to_string(vm->argc), true,
                                      component->quoted};
    }

    int n;
    if ((n = is_number(component->var_substitution)) != -1) {
        if (n > vm->argc)
            return (struct UnsplitString){
                .expansion = {NULL, 0}, true, component->quoted};
        return (struct UnsplitString){
            {.string = strdup(vm->argv[n]), .length = (int)strlen(vm->argv[n])},
            true,
            component->quoted};
    }

    const char *value = getenv(component->var_substitution);
    if (value == NULL) {
        return (struct UnsplitString){{NULL, 0}, true, component->quoted};
    }
    return (struct UnsplitString){
        {.string = strdup(value), .length = (int)strlen(value)},
        true,
        component->quoted};
}

static struct UnsplitString expand_string(
    const struct Vm *vm, const struct StringComponent *component) {
    int i, start = 0, total_size = 0;
    char *string = NULL;

    if (component->type == STRING_COMPONENT_LITERAL &&
        component->literal[0] == '~') {
        start = tilde_expansion(vm, component->literal, component->length,
                                &string, &total_size);
    }

    for (i = start; i < component->length; ++i) {
        if (component->literal[i] == '\\') {
            if (i + 1 == component->length)
                break;
            if (component->type == STRING_COMPONENT_LITERAL ||
                component->literal[i + 1] == '\\' ||
                (component->type == STRING_COMPONENT_DQ &&
                 kIsEscapableInDQ[(unsigned char)component->literal[i + 1]])) {
            } else
                continue;

            const int length = i - start + 1;
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
        const int length = i - start;
        string = grow_string(string, total_size + length);
        strncpy(&string[total_size], &component->literal[start], length);
        total_size += length;
    }

    return (struct UnsplitString){
        .expansion = {string, total_size},
        .is_expanded = true,
        .was_quoted = component->quoted,
    };
}

static struct UnsplitString expand_command_sub(
    struct Vm *vm, const struct StringComponent *component) {
    struct Job *job = malloc(sizeof(struct Job));
    CHECK_ALLOC(job);

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        CASH_PERROR(EXIT_FAILURE, "pipe", "could not create pipe%s", "");
        exit(EXIT_FAILURE);
    }

    make_subshell_job(vm, component->command_substitution, STDIN_FILENO,
                      pipefd[1], STDERR_FILENO, job);
    launch_job(vm, job, true);
    vm->previous_exit_code = job->first_process->status % 0xFF;

    close(pipefd[1]);

    struct String output = read_all_fd(pipefd[0]);
    close(pipefd[0]);

    struct String stripped = strip_end(&output);
    CASH_DEBUG("output is %.*s\n", stripped.length, stripped.string);
    free_string(&output);
    return (struct UnsplitString){
        .expansion = stripped,
        .is_expanded = true,
        .was_quoted = component->quoted,
    };
}

static const bool IS_IFS_SPACE[128] = {
    [' '] = true,
    ['\t'] = true,
    ['\n'] = true,
};

struct StringView next_word(const struct String *str, int start,
                            int *next_start) {
    int i = start;
    while (i < str->length && !IS_IFS_SPACE[(unsigned char)str->string[i]]) {
        assert(str->string[i] != '\0');
        i++;
    }
    int word_end = i;
    if (i == str->length)
        *next_start = -1;
    else {
        while (i < str->length && IS_IFS_SPACE[(unsigned char)str->string[i]]) {
            i++;
        }
        *next_start = i;
    }
    return (struct StringView){&str->string[start], word_end - start};
}

static struct String into_words(struct Vm *vm, const struct ShellString *string,
                                struct WordList *word_list) {
    struct String str = {NULL, 0};
    struct String total_str = {NULL, 0};

    for (int i = 0; i < string->component_count; ++i) {
        const struct StringComponent *component = &string->components[i];
        const struct UnsplitString expanded = expand_component(vm, component);
        int start = 0;

        append_n(&total_str, expanded.expansion.string,
                 expanded.expansion.length);
        if (expanded.was_quoted) {
            append_n(&str, expanded.expansion.string,
                     expanded.expansion.length);
        } else {
            do {
                struct StringView word =
                    next_word(&expanded.expansion, start, &start);
                append_n(&str, word.string, word.length);

                if (str.length > 0) {
                    ADD_LIST(word_list, word_count, word_capacity, words,
                             str.string, char *);
                    str = (struct String){NULL, 0};
                }
            } while (start != -1);
        }
    }

    if (str.length > 0)
        ADD_LIST(word_list, word_count, word_capacity, words, str.string,
                 char *);
    return total_str;
}

// TODO: can make expand_component write directly to a single
// allocated string, instead of allocating a new one for each
// compoenent and then freeing it
struct String to_string(struct Vm *vm, const struct ShellString *string) {
    char *str = NULL;
    int total_size = 0;

    for (int i = 0; i < string->component_count; ++i) {
        const struct String expanded =
            expand_component(vm, &string->components[i]).expansion;
        const int is_last_comp = i + 1 == string->component_count;
        const int new_alloc_size = total_size + expanded.length + is_last_comp;

        str = grow_string(str, new_alloc_size);
        strncpy(&str[total_size], expanded.string, expanded.length);
        total_size += expanded.length;

        if (is_last_comp) {
            str[new_alloc_size - 1] = '\0';
        }

        free(expanded.string);
    }

    return (struct String){.string = str, .length = total_size};
}

static void update_prompt(struct Vm *vm) {
    free(vm->current_prompt);
    vm->current_prompt = make_new_prompt(vm->userpw->pw_name);
}

static int change_dir(struct Vm *vm, const struct RawCommand *command) {
    if (command->as_cmd.args_count > 2) {
        CASH_ERROR(EXIT_FAILURE,
                   "cd: too many arguments (one expected, got %d)\n",
                   command->as_cmd.args_count - 1);
        return EXIT_FAILURE;
    }
    int result = 0;

    char *old_pwd = vm->old_pwd;
    vm->old_pwd = vm->pwd;
    if (command->as_cmd.args_count == 1) {
        result = chdir(vm->userpw->pw_dir);
        vm->pwd = strdup(vm->userpw->pw_dir);
    } else {
        const char *arg = command->as_cmd.args[1];
        if (strcmp(arg, "-") == 0) {
            result = chdir(old_pwd);
            vm->pwd = old_pwd;
            printf("%s\n", old_pwd);
            old_pwd = NULL;
        } else {
            result = chdir(arg);

            char path[PATH_MAX + 1];
            char *resolved = realpath(".", path);
            if (!resolved) {
                CASH_PERROR(EXIT_FAILURE, "realpath", "%s", "");
                exit(EXIT_FAILURE);
            }
            vm->pwd = strdup(resolved);
        }
    }

    setenv("OLDPWD", vm->old_pwd, 1);
    setenv("PWD", vm->pwd, 1);
    free(old_pwd);

    if (result == -1) {
        CASH_PERROR(EXIT_FAILURE, "cd", "%s", "");
        return EXIT_FAILURE;
    }

    update_prompt(vm);
    return 0;
}

static int exit_shell(struct Vm *vm, const struct RawCommand *raw_command) {
    if (raw_command->as_cmd.args_count > 2) {
        CASH_ERROR(EXIT_FAILURE,
                   "exit: too many arguments (one expected, got %d)\n",
                   raw_command->as_cmd.args_count - 1);
        return EXIT_FAILURE;
    }

    if (raw_command->as_cmd.args_count == 1) {
        vm->previous_exit_code = 0;
    } else {
        char *endptr;
        long exit_code = strtol(raw_command->as_cmd.args[1], &endptr, 10);
        if (*endptr != '\0' || exit_code < 0 || exit_code > 255) {
            CASH_ERROR(EXIT_FAILURE, "exit: invalid exit code `%s`\n",
                       raw_command->as_cmd.args[1]);
            return EXIT_FAILURE;
        }
        vm->previous_exit_code = (int)exit_code;
    }

    vm->exit = true;
    exit(vm->previous_exit_code);
    return vm->previous_exit_code;
}

int is_builtin(const char *name) {
    for (int i = 0; i < BUILTIN_COUNT; ++i) {
        if (strcmp(name, BUILTIN_NAMES[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static bool is_path(const char *cmd) {
    return strchr(cmd, '/') != NULL;
}

static bool is_executable(const char *path) {
    return access(path, X_OK) == 0;
}

static char *find_in_path(const char *cmd) {
    const char *path_env = getenv("PATH");
    if (!path_env)
        return NULL;

    char *paths = strdup(path_env);
    if (!paths)
        return NULL;

    char *dir = strtok(paths, ":");
    while (dir) {
        const size_t len = strlen(dir) + strlen(cmd) + 2;
        char *full_path = malloc(len);
        if (!full_path)
            break;
        snprintf(full_path, len, "%s/%s", dir, cmd);
        // printf("checking path (cmd: %s) %s\n", cmd, full_path);

        if (is_executable(full_path)) {
            free(paths);
            return full_path;
        }

        free(full_path);
        dir = strtok(NULL, ":");
    }

    free(paths);
    return NULL;
}
static int tilde_expansion(const struct Vm *vm, const char *source, int len,
                           char **dest, int *total_size) {
    int end = 1;
    while (end < len && source[end] != '/')
        ++end;

    char *expansion;

    if (end == 1) {
        expansion = vm->userpw->pw_dir;
    } else if (end == 2 && (source[1] == '+' || source[1] == '-')) {
        expansion = source[1] == '+' ? vm->pwd : vm->old_pwd;
    } else {
        char *name_copy = strndup_null_terminated(source + 1, end - 1);
        struct passwd *user = getpwnam(name_copy);
        if (!user)
            expansion = NULL;
        else
            expansion = user->pw_dir;
        free(name_copy);
    }

    if (!expansion) {
        *total_size += end;
        *dest = grow_string(*dest, *total_size);
        strncpy(&(*dest)[*total_size - end], source, end);
    } else {
        const int expansion_size = (int)strlen(expansion);
        *dest = grow_string(*dest, *total_size + expansion_size);
        memcpy(&(*dest)[*total_size], expansion, expansion_size);
        *total_size += expansion_size;
    }
    return end;
}
