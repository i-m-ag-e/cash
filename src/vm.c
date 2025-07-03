#include <cash/error.h>
#include <cash/string.h>
#include <cash/util.h>
#include <cash/vm.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern bool REPL_MODE;
extern char *const *environ;

static void update_prompt(struct VM *vm);

static void change_dir(struct VM *vm, struct String name,
                       const struct Command *command);

static bool is_path(const char *cmd);
static bool is_executable(const char *path);

static char *find_in_path(const char *cmd);

struct VM make_vm(void) {
    struct passwd *userpw = getpwuid(getuid());
    return (struct VM){.current_prompt = make_new_prompt(userpw->pw_name),
                       .uid = userpw->pw_uid,
                       .userpw = userpw};
}

void free_vm(const struct VM *vm) {
    free(vm->current_prompt);
}

void run_program(struct VM *vm, const struct Program *program) {
    for (int i = 0; i < program->statement_count; ++i) {
        run_command(vm, &program->statements[i].command);
    }
}

void run_command(struct VM *vm, struct Command *command) {
    const struct String command_name = to_string(&command->command_name);

    // use custom implementation of cd
    if (strncmp(command_name.string, "cd", command_name.length) == 0) {
        change_dir(vm, command_name, command);
        free_string(&command_name);
        return;
    }

    if (strncmp(command_name.string, "ls", command_name.length) == 0) {
        struct ShellString color_arg = make_string();
        add_string_literal(&color_arg, STRING_COMPONENT_LITERAL, "--color=auto",
                           12, 0);
        add_argument(&command->arguments, color_arg);
    }

    printf("Name: %s\n", command_name.string);
    char **args =
        malloc((command->arguments.argument_count + 2) * sizeof(*args));
    if (!args) {
        cash_error(EXIT_FAILURE, "could not allocate memory for arguments%s\n",
                   "");
        exit(EXIT_FAILURE);
    }

    args[0] = strndup(command_name.string, command_name.length);
    printf("arg 0: (len %d) %s\n", command_name.length, args[0]);

    for (int i = 0; i < command->arguments.argument_count; ++i) {
        const struct String arg = to_string(&command->arguments.arguments[i]);
        printf("arg %d: (len %d) %s\n", i + 1, arg.length, arg.string);

        args[i + 1] = arg.string;
    }
    args[command->arguments.argument_count + 1] = NULL;
    printf("-----------------\n");

    char *executable = NULL;
    if (is_path(command_name.string)) {
        if (!is_executable(command_name.string)) {
            cash_error(EXIT_FAILURE, "the path `%s` is not an executable\n",
                       command_name.string);
            goto exit_command;
        }
        executable = strndup(command_name.string, command_name.length);
    } else {
        char *res = find_in_path(command_name.string);
        if (res == NULL) {
            executable = strndup(command_name.string, command_name.length);
        } else {
            executable = strdup(res);
        }
        free(res);
    }

    const pid_t pid = fork();

    if (pid == 0) {
        const int res = execve(executable, args, environ);
        if (res == -1) {
            cash_perror(EXIT_FAILURE, "execve", "%s", "");
        }
    }

    waitpid(pid, NULL, 0);
exit_command:
    free(executable);
    free_string(&command_name);
    for (int i = 0; i < command->arguments.argument_count + 1; ++i) {
        free(args[i]);
    }
    free(args);
}

static void update_prompt(struct VM *vm) {
    free(vm->current_prompt);
    vm->current_prompt = make_new_prompt(vm->userpw->pw_name);
}

static void change_dir(struct VM *vm, struct String name,
                       const struct Command *command) {
    if (command->arguments.argument_count > 1) {
        cash_error(EXIT_FAILURE,
                   "cd: too many arguments (one expected, got %d)\n",
                   command->arguments.argument_count);
        return;
    }
    int result = 0;
    if (command->arguments.argument_count == 0) {
        result = chdir(vm->userpw->pw_dir);
    } else {
        const struct String arg = to_string(&command->arguments.arguments[0]);
        result = chdir(arg.string);
        free_string(&arg);
    }

    if (result == -1) {
        cash_perror(EXIT_FAILURE, "cd", "");
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
