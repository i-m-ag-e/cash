#include <assert.h>
#include <cash/ast.h>
#include <cash/error.h>
#include <cash/memory.h>
#include <cash/string.h>
#include <cash/util.h>
#include <cash/vm.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
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
extern char *const *environ;

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
static void free_raw_command(const struct RawCommand *raw_command);

struct Pipeline {
    struct RawCommand *commands;
    int command_count;
    int command_capacity;
};
static int flatten_pipeline(const struct Vm *vm, const struct Expr *expr,
                            struct Pipeline *pipeline);
static void free_pipeline(const struct Pipeline *pipeline);

static int spawn_process(int in, int out, struct RawCommand *raw_command);

static struct RawRedirection get_redirection(const struct Vm *vm,
                                             const struct Redirection *redir);
static int get_final_command(const struct Vm *vm, const struct Command *command,
                             struct RawCommand *raw_command);

static int exec_expression(struct Vm *vm, struct Expr *expr);
static pid_t run_raw_command(struct Vm *vm, struct RawCommand *raw_command,
                             int in, int out);
static pid_t run_raw_command_in_subshell(struct Vm *vm,
                                         struct RawCommand *raw_command, int in,
                                         int out);
static int run_subshell(struct Vm *vm, struct Program *program);
static int exec_pipeline(struct Vm *vm, struct Pipeline *pipeline);

static struct String expand_component(const struct Vm *vm,
                                      const struct StringComponent *component);
static struct String to_string(const struct Vm *vm,
                               const struct ShellString *string);

static void update_prompt(struct Vm *vm);

static void change_dir(struct Vm *vm, const struct RawCommand *command);

static bool is_path(const char *cmd);
static bool is_executable(const char *path);

static char *find_in_path(const char *cmd);

static int tilde_expansion(const struct Vm *vm, const char *source, int len,
                           char **dest, int *total_size);

struct Vm make_vm(void) {
    struct passwd *userpw = getpwuid(getuid());
    char *cwd = get_cwd();
    setenv("PWD", cwd, 1);
    setenv("OLDPWD", cwd, 1);
    return (struct Vm){.current_prompt = make_new_prompt(userpw->pw_name),
                       .pwd = cwd,
                       .old_pwd = strdup(cwd),
                       .uid = userpw->pw_uid,
                       .userpw = userpw,
                       .exit = false,
                       .previous_exit_code = 0};
}

void free_vm(const struct Vm *vm) {
    free(vm->current_prompt);
    free(vm->old_pwd);
    free(vm->pwd);
}

static void free_raw_command(const struct RawCommand *raw_command) {
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

static void free_pipeline(const struct Pipeline *pipeline) {
    for (int i = 0; i < pipeline->command_count; ++i) {
        free_raw_command(&pipeline->commands[i]);
    }
    free(pipeline->commands);
}

int run_program(struct Vm *vm, const struct Program *program) {
    for (int i = 0; i < program->statement_count; ++i) {
        exec_expression(vm, &program->statements[i].expr);
        // run_command(vm, &program->statements[i].command);
    }
    return vm->previous_exit_code;
}

static int spawn_process(int in, int out, struct RawCommand *raw_command) {
    // printf("Spawing command: %s ", name);
    // for (int i = 0; args[i] != NULL; ++i) {
    //     printf("%s ", args[i]);
    // }
    // printf("\n");
    const pid_t pid = fork();
    if (pid < 0) {
        CASH_PERROR(EXIT_FAILURE, "fork", "could not fork process %s",
                    raw_command->name);
        return -1;
    }

    if (pid == 0) {
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
                                "could not duplicate stdout to stderr");
                    exit(EXIT_FAILURE);
                }
            }
        }

        const int res = execve(raw_command->name, raw_command->args, environ);
        if (res == -1) {
            CASH_PERROR(EXIT_FAILURE, "execvp", "could not execute %s",
                        raw_command->name);
            exit(EXIT_FAILURE);
        }
    }

    return pid;
}

static int get_final_command(const struct Vm *vm, const struct Command *command,
                             struct RawCommand *raw_command) {
    char *executable = NULL;
    char **args = NULL;
    if (command->command_name.component_count != 0) {
        const struct String command_name =
            to_string(vm, &command->command_name);

        printf("Name: %s\n", command_name.string);
        args = malloc((command->arguments.argument_count + 2) * sizeof(*args));
        if (!args) {
            CASH_ERROR(EXIT_FAILURE,
                       "could not allocate memory for arguments%s\n", "");
            exit(EXIT_FAILURE);
        }

        args[0] = strndup(command_name.string, command_name.length);
        printf("arg 0: (len %d) %s\n", command_name.length, args[0]);

        for (int i = 0; i < command->arguments.argument_count; ++i) {
            const struct String arg =
                to_string(vm, &command->arguments.arguments[i]);
            printf("arg %d: (len %d) %s\n", i + 1, arg.length, arg.string);

            args[i + 1] = arg.string;
        }
        args[command->arguments.argument_count + 1] = NULL;
        printf("-----------------\n");

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

int run_command(struct Vm *vm, struct Command *command, int in, int out) {
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

    int status;
    pid_t pid = run_raw_command(vm, &raw_command, in, out);
    waitpid(pid, &status, 0);
    free_raw_command(&raw_command);

    vm->previous_exit_code = status % 0xFF;
    return vm->previous_exit_code;
}

static pid_t run_raw_command(struct Vm *vm, struct RawCommand *raw_command,
                             int in, int out) {
    // use custom implementation of cd and exit
    if (strcmp(raw_command->name, "cd") == 0) {
        change_dir(vm, raw_command);
        return 0;
    }

    if (strcmp(raw_command->name, "exit") == 0) {
        vm->exit = true;
        return 0;
    }

    if (strcmp(raw_command->args[0], "ls") == 0) {
        char *color_arg = malloc((strlen("--color=auto") + 1) * sizeof(char));
        CHECK_ALLOC(color_arg);
        strcpy(color_arg, "--color=auto");

        char **new_args = realloc(
            raw_command->args, (raw_command->args_count + 2) * sizeof(char *));
        CHECK_ALLOC(new_args);
        new_args[raw_command->args_count] = color_arg;
        new_args[raw_command->args_count + 1] = NULL;
        raw_command->args_count++;
        raw_command->args = new_args;
    }

    return spawn_process(in, out, raw_command);
}

static pid_t run_raw_command_in_subshell(struct Vm *vm,
                                         struct RawCommand *raw_command, int in,
                                         int out) {
    pid_t pid = fork();
    if (pid == 0) {
        if (in != STDIN_FILENO) {
            dup2(in, STDIN_FILENO);
            close(in);
        }
        if (out != STDOUT_FILENO) {
            dup2(out, STDOUT_FILENO);
            close(out);
        }

        int status;
        pid_t subpid =
            run_raw_command(vm, raw_command, STDIN_FILENO, STDOUT_FILENO);
        waitpid(subpid, &status, 0);
        exit(status);
    }

    return pid;
}

static int exec_expression(struct Vm *vm, struct Expr *expr) {
    switch (expr->type) {
        case EXPR_COMMAND:
            return run_command(vm, &expr->command, STDIN_FILENO, STDOUT_FILENO);

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
            struct Pipeline pipeline = {NULL, 0, 0};
            int res = flatten_pipeline(vm, expr, &pipeline);
            for (int i = 0; i < pipeline.command_count; ++i) {
                struct RawCommand raw_command = pipeline.commands[i];
                printf("Command %d:\n\tName: %s\n", i, raw_command.name);
                for (int j = 0; j < raw_command.args_count; ++j) {
                    printf("\tArg %d: %s\n", j, raw_command.args[j]);
                }
            }
            printf("res: %d\n", res);
            if (res == 0)
                res = exec_pipeline(vm, &pipeline);
            free_pipeline(&pipeline);
            return res;
        }

        default:
            CASH_ERROR(EXIT_FAILURE, "unimplemented%s", "");
            exit(EXIT_FAILURE);
    }
}

static int run_subshell(struct Vm *vm, struct Program *program) {
    printf(GREEN "Entering subshell\n" RESET);
    const pid_t pid = fork();

    if (pid == 0) {
        int status = run_program(vm, program);
        exit(status);
    }

    int status;
    waitpid(pid, &status, 0);
    printf(GREEN "Exiting subshell\n" RESET);
    return status;
}

static int flatten_pipeline(const struct Vm *vm, const struct Expr *expr,
                            struct Pipeline *pipeline) {
    struct RawCommand placeholder = {NULL, NULL, 0, NULL, 0};
    if (expr->binary.left->type == EXPR_PIPELINE) {
        const int res = flatten_pipeline(vm, expr->binary.left, pipeline);
        if (res != 0)
            return res;
    } else {
        assert(expr->binary.left->type == EXPR_COMMAND);
        ADD_LIST(pipeline, command_count, command_capacity, commands,
                 placeholder, struct RawCommand);
        get_final_command(vm, &expr->binary.left->command,
                          &pipeline->commands[pipeline->command_count - 1]);
    }

    assert(expr->binary.right->type == EXPR_COMMAND);

    ADD_LIST(pipeline, command_count, command_capacity, commands, placeholder,
             struct RawCommand);
    get_final_command(vm, &expr->binary.right->command,
                      &pipeline->commands[pipeline->command_count - 1]);
    return 0;
}

static int exec_pipeline(struct Vm *vm, struct Pipeline *pipeline) {
    int pipefd[2];
    int in = STDIN_FILENO, i;
    pid_t *pids = malloc(pipeline->command_count * sizeof(pid_t));
    int status;
    for (i = 0; i < pipeline->command_count - 1; ++i) {
        struct RawCommand *cmd = &pipeline->commands[i];
        pipe(pipefd);
        pids[i] = run_raw_command_in_subshell(vm, cmd, in, pipefd[1]);

        close(pipefd[1]);
        in = pipefd[0];
    }

    pids[i] = run_raw_command_in_subshell(vm, &pipeline->commands[i], in,
                                          STDOUT_FILENO);
    // (in, STDOUT_FILENO, pipeline->commands[i].name,
    //                         pipeline->commands[i].args);
    waitpid(pids[i], &status, 0);

    for (i = 0; i < pipeline->command_count - 1; ++i) {
        waitpid(pids[i], NULL, 0);
    }
    free(pids);
    return status;
}

struct String expand_component(const struct Vm *vm,
                               const struct StringComponent *component) {
    char *string = NULL;

    switch (component->type) {
        case STRING_COMPONENT_VAR_SUB: {
            if (component->length == 1 &&
                component->var_substitution[0] == '?') {
                const int length =
                    snprintf(NULL, 0, "%d", vm->previous_exit_code);
                char *buf = malloc((length + 1) * sizeof(char));
                if (!buf) {
                    CASH_ERROR(EXIT_FAILURE, "Memory allocation failed%s\n",
                               "");
                    exit(EXIT_FAILURE);
                }
                snprintf(buf, length + 1, "%d", vm->previous_exit_code);
                return (struct String){.string = buf, .length = length};
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
        // printf("expanded (%d): %.*s\n", expanded.length,
        // expanded.length,
        //        expanded.string);
        const int is_last_comp = i + 1 == string->component_count;
        const int new_alloc_size = total_size + expanded.length + is_last_comp;
        // printf("new alloc size: %d\n", new_alloc_size);
        str = grow_string(str, new_alloc_size);
        // printf("copying at position %d in (%d) \"%.*s\"\n",
        // total_size,
        //        total_size, total_size, str);
        strncpy(&str[total_size], expanded.string, expanded.length);
        total_size += expanded.length;

        if (is_last_comp) {
            // printf("setting %d to NULL\n", new_alloc_size);
            str[new_alloc_size - 1] = '\0';
        }

        free(expanded.string);
    }
    // printf("final str: %s\n", str);
    // for (int i = 0; i < total_size + 1; ++i) {
    //     printf("%d: (%d) %c\n", i, str[i], str[i]);
    // }

    return (struct String){.string = str, .length = total_size};
}

static void update_prompt(struct Vm *vm) {
    free(vm->current_prompt);
    vm->current_prompt = make_new_prompt(vm->userpw->pw_name);
}

static void change_dir(struct Vm *vm, const struct RawCommand *command) {
    if (command->args_count > 2) {
        CASH_ERROR(EXIT_FAILURE,
                   "cd: too many arguments (one expected, got %d)\n",
                   command->args_count - 1);
        return;
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
        return;
    }

    update_prompt(vm);
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
        strncpy(&(*dest)[*total_size], expansion, expansion_size);
        *total_size += expansion_size;
    }
    return end;
}
