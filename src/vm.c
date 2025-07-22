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

extern bool repl_mode;
extern char **environ;

static void make_process(struct Vm *vm, const struct Command *command,
                         struct Process *process);
static int make_process_list(struct Vm *vm, const struct Expr *expr,
                             struct Process ***process_list);
static int make_job(struct Vm *vm, const struct Expr *expr, struct Job *job);

static struct RawRedirection get_redirection(const struct Vm *vm,
                                             const struct Redirection *redir);
static int get_final_command(const struct Vm *vm, const struct Command *command,
                             struct RawCommand *raw_command);

static int exec_expression(struct Vm *vm, struct Expr *expr);
static int run_command(struct Vm *vm, struct Expr *expr);
static int run_subshell(struct Vm *vm, struct Program *program);

static struct String expand_component(const struct Vm *vm,
                                      const struct StringComponent *component);
static struct String to_string(const struct Vm *vm,
                               const struct ShellString *string);

static void update_prompt(struct Vm *vm);

static bool is_path(const char *cmd);
static bool is_executable(const char *path);

static char *find_in_path(const char *cmd);

static int tilde_expansion(const struct Vm *vm, const char *source, int len,
                           char **dest, int *total_size);

static int change_dir(struct Vm *vm, const struct RawCommand *command);
static int exit_shell(struct Vm *vm, const struct RawCommand *raw_command);

static const bool kIsEscapableInDQ[] = {
    ['"'] = true,
    ['\\'] = true,
    ['$'] = true,
    ['`'] = true,
};

const char *BUILTIN_NAMES[] = {"cd", "exit", "jobs", "fg"};
const BuiltinFunc BUILTIN_FUNCS[] = {change_dir, exit_shell, list_jobs, fg};
const int BUILTIN_COUNT = sizeof(BUILTIN_NAMES) / sizeof(BUILTIN_NAMES[0]);

struct Vm make_vm(int argc, char **argv) {
    struct passwd *userpw = getpwuid(getuid());
    char *cwd = get_cwd();

    pid_t shell_pgid;
    if (repl_mode) {
        while (tcgetpgrp(STDIN_FILENO) != (shell_pgid = getpgrp())) {
            kill(-shell_pgid, SIGTTIN);
        }
    }

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);

    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) == -1) {
        CASH_PERROR(EXIT_FAILURE, "setpgid", "could not set process group id%s",
                    "");
        exit(EXIT_FAILURE);
    }

    tcsetpgrp(STDIN_FILENO, shell_pgid);
    struct termios term_state;
    tcgetattr(STDIN_FILENO, &term_state);

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
        exec_expression(vm, &program->statements[i].expr);
        // run_command(vm, &program->statements[i].command);
    }

    if (!vm->notified_this_time)
        do_job_notification(vm);
    vm->notified_this_time = false;
    return vm->previous_exit_code;
}

static int get_final_command(const struct Vm *vm, const struct Command *command,
                             struct RawCommand *raw_command) {
    char *executable = NULL;
    char **args = NULL;
    if (command->command_name.component_count != 0) {
        const struct String command_name =
            to_string(vm, &command->command_name);

        CASH_DEBUG("Name: %s\n", command_name.string);
        args = malloc((command->arguments.argument_count + 2) * sizeof(*args));
        if (!args) {
            CASH_ERROR(EXIT_FAILURE,
                       "could not allocate memory for arguments%s\n", "");
            exit(EXIT_FAILURE);
        }

        args[0] = strndup(command_name.string, command_name.length);
        CASH_DEBUG("arg 0: (len %d) %s\n", command_name.length, args[0]);

        for (int i = 0; i < command->arguments.argument_count; ++i) {
            const struct String arg =
                to_string(vm, &command->arguments.arguments[i]);
            CASH_DEBUG("arg %d: (len %d) %s\n", i + 1, arg.length, arg.string);

            args[i + 1] = arg.string;
        }
        args[command->arguments.argument_count + 1] = NULL;
        CASH_DEBUG("-----------------\n");

        if (is_path(command_name.string)) {
            if (!is_executable(command_name.string)) {
                CASH_ERROR(EXIT_FAILURE, "the path `%s` is not an executable\n",
                           command_name.string);
                free_string(&command_name);
                return EXIT_FAILURE;
            }
            executable = strndup(command_name.string, command_name.length);
        } else {
            char *res = find_in_path(command_name.string);
            if (res == NULL) {
                executable = strndup(command_name.string, command_name.length);
            } else {
                executable = res;
            }
        }

        free_string(&command_name);
    }

    struct RawRedirection *redirs =
        malloc((command->redirection_count) * sizeof(struct RawRedirection));
    CHECK_ALLOC(redirs);
    for (int i = 0; i < command->redirection_count; ++i) {
        struct Redirection *redir = &command->redirections[i];
        redirs[i] = get_redirection(vm, redir);
    }

    *raw_command =
        (struct RawCommand){.name = executable,
                            .args = args,
                            .args_count = command->arguments.argument_count + 1,
                            .redirs_count = command->redirection_count,
                            .redirs = redirs};

    return 0;
}

static struct RawRedirection get_redirection(const struct Vm *vm,
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

int run_command(struct Vm *vm, struct Expr *expr) {
    if (!repl_mode)
        remove_completed_jobs(vm);
    struct Command *command = &expr->command;

    struct RawCommand raw_command;
    const int command_expansion = get_final_command(vm, command, &raw_command);
    if (command_expansion != 0) {
        free_raw_command(&raw_command);
        return command_expansion;
    }

    if (raw_command.name == NULL) {
        if (raw_command.redirs_count == 0) {
            free_raw_command(&raw_command);
            return vm->previous_exit_code;
        } else {
            raw_command.name = strdup("/bin/true");
            raw_command.args = malloc(2 * sizeof(char *));
            CHECK_ALLOC(raw_command.args);
            raw_command.args[0] = strdup("true");
            raw_command.args[1] = NULL;
            raw_command.args_count = 1;
        }
    }

    int builtin = is_builtin(raw_command.name);
    if (builtin != -1) {
        int res = BUILTIN_FUNCS[builtin](vm, &raw_command);
        free_raw_command(&raw_command);
        return res;
    }

    if (strcmp(raw_command.args[0], "ls") == 0) {
        char *color_arg = malloc((strlen("--color=auto") + 1) * sizeof(char));
        CHECK_ALLOC(color_arg);
        strcpy(color_arg, "--color=auto");

        char **new_args = realloc(
            raw_command.args, (raw_command.args_count + 2) * sizeof(char *));
        CHECK_ALLOC(new_args);
        new_args[raw_command.args_count] = color_arg;
        new_args[raw_command.args_count + 1] = NULL;
        raw_command.args_count++;
        raw_command.args = new_args;
    }

    struct Process *process = malloc(sizeof(struct Process));
    CHECK_ALLOC(process);
    *process = (struct Process){
        .next_process = NULL,
        .raw_command = raw_command,
        .completed = false,
        .stopped = false,
        .status = 0,
        .pid = 0,
    };
    struct Job *job = malloc(sizeof(struct Job));
    CHECK_ALLOC(job);
    *job = (struct Job){
        .next_job = NULL,
        .first_process = process,
        .command = strndup(expr->expr_text.string, expr->expr_text.length),
        .notified = false,
        .term_state = vm->shell_term_state,
        .stderr = STDERR_FILENO,
        .stdout = STDOUT_FILENO,
        .stdin = STDIN_FILENO,
        .pgid = 0};

    launch_job(vm, job, !expr->background);

    vm->previous_exit_code = process->status % 0xFF;
    return vm->previous_exit_code;
}

static int exec_expression(struct Vm *vm, struct Expr *expr) {
    switch (expr->type) {
        case EXPR_COMMAND:
            return run_command(vm, expr);

        case EXPR_SUBSHELL:
            return run_subshell(vm, expr->subshell);

        case EXPR_NOT: {
            if (exec_expression(vm, expr->binary.left) == 0) {
                vm->previous_exit_code = 1;
                return 1;
            }
            vm->previous_exit_code = 0;
            return 0;
        }

        case EXPR_AND:
        case EXPR_OR: {
            const int left = exec_expression(vm, expr->binary.left);
            if ((left == 0 && expr->type == EXPR_AND) ||
                (left != 0 && expr->type == EXPR_OR)) {
                vm->previous_exit_code =
                    exec_expression(vm, expr->binary.right);
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
            int i = 0;
            for (struct Process *process = job->first_process; process != NULL;
                 (process = process->next_process), ++i) {
                struct RawCommand *raw_command = &process->raw_command;
                CASH_DEBUG("Command %d:\n\tName: %s\n", i, raw_command->name);
                for (int j = 0; j < raw_command->args_count; ++j) {
                    CASH_DEBUG("\tArg %d: %s\n", j, raw_command->args[j]);
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

static int run_subshell(struct Vm *vm, struct Program *program) {
    CASH_DEBUG(GREEN "Entering subshell\n" RESET);
    const pid_t pid = fork();

    if (pid == 0) {
        int status = run_program(vm, program);
        exit(status);
    }

    int status;
    waitpid(pid, &status, 0);
    CASH_DEBUG(GREEN "Exiting subshell\n" RESET);
    return status;
}

static void make_process(struct Vm *vm, const struct Command *command,
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

static int make_process_list(struct Vm *vm, const struct Expr *expr,
                             struct Process ***process_list) {
    if (expr->binary.left->type == EXPR_PIPELINE) {
        int res = make_process_list(vm, expr->binary.left, process_list);
        if (res != 0)
            return res;
    } else {
        assert(expr->binary.left->type == EXPR_COMMAND);
        struct Process *new_process = malloc(sizeof(struct Process));
        CHECK_ALLOC(new_process);
        make_process(vm, &expr->binary.left->command, new_process);
        **process_list = new_process;
        *process_list = &new_process->next_process;
    }

    assert(expr->binary.right->type == EXPR_COMMAND);
    struct Process *new_process = malloc(sizeof(struct Process));
    CHECK_ALLOC(new_process);
    make_process(vm, &expr->binary.right->command, new_process);
    **process_list = new_process;
    *process_list = &new_process->next_process;
    return 0;
}

static int make_job(struct Vm *vm, const struct Expr *expr, struct Job *jobp) {
    struct Job job = {
        .first_process = NULL,
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

struct String expand_component(const struct Vm *vm,
                               const struct StringComponent *component) {
    char *string = NULL;

    switch (component->type) {
        case STRING_COMPONENT_VAR_SUB: {
            if (component->length == 1 &&
                component->var_substitution[0] == '?') {
                return number_to_string(vm->previous_exit_code);
            }

            if (component->length == 1 &&
                component->var_substitution[0] == '#') {
                return number_to_string(vm->argc);
            }

            int n;
            if ((n = is_number(component->var_substitution)) != -1) {
                if (n > vm->argc)
                    return (struct String){NULL, 0};
                return (struct String){.string = strdup(vm->argv[n]),
                                       .length = (int)strlen(vm->argv[n])};
            }

            const char *value = getenv(component->var_substitution);
            if (value == NULL) {
                return (struct String){NULL, 0};
            }
            return (struct String){.string = strdup(value),
                                   .length = (int)strlen(value)};
        }
        case STRING_COMPONENT_LITERAL:
        case STRING_COMPONENT_DQ: {
            int i, start = 0, total_size = 0;

            if (component->type == STRING_COMPONENT_LITERAL &&
                component->literal[0] == '~') {
                start =
                    tilde_expansion(vm, component->literal, component->length,
                                    &string, &total_size);
            }

            for (i = start; i < component->length; ++i) {
                if (component->literal[i] == '\\') {
                    if (i + 1 == component->length)
                        break;
                    if (component->type == STRING_COMPONENT_LITERAL ||
                        component->literal[i + 1] == '\\' ||
                        (component->type == STRING_COMPONENT_DQ &&
                         kIsEscapableInDQ[(unsigned char)
                                              component->literal[i + 1]])) {
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

// TODO: can make expand_component write directly to a single
// allocated string, instead of allocating a new one for each
// compoenent and then freeing it
struct String to_string(const struct Vm *vm, const struct ShellString *string) {
    char *str = NULL;
    int total_size = 0;

    for (int i = 0; i < string->component_count; ++i) {
        const struct String expanded =
            expand_component(vm, &string->components[i]);
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
    if (command->args_count > 2) {
        CASH_ERROR(EXIT_FAILURE,
                   "cd: too many arguments (one expected, got %d)\n",
                   command->args_count - 1);
        return EXIT_FAILURE;
    }
    int result = 0;

    char *old_pwd = vm->old_pwd;
    vm->old_pwd = vm->pwd;
    if (command->args_count == 1) {
        result = chdir(vm->userpw->pw_dir);
        vm->pwd = strdup(vm->userpw->pw_dir);
    } else {
        const char *arg = command->args[1];
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
        return 255;
    }

    update_prompt(vm);
    return 0;
}

static int exit_shell(struct Vm *vm, const struct RawCommand *raw_command) {
    if (raw_command->args_count > 2) {
        CASH_ERROR(EXIT_FAILURE,
                   "exit: too many arguments (one expected, got %d)\n",
                   raw_command->args_count - 1);
        return EXIT_FAILURE;
    }

    if (raw_command->args_count == 1) {
        vm->previous_exit_code = 0;
    } else {
        char *endptr;
        long exit_code = strtol(raw_command->args[1], &endptr, 10);
        if (*endptr != '\0' || exit_code < 0 || exit_code > 255) {
            CASH_ERROR(EXIT_FAILURE, "exit: invalid exit code `%s`\n",
                       raw_command->args[1]);
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
