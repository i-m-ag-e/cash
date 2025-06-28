#include <cash/error.h>
#include <cash/string.h>
#include <cash/vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern bool REPL_MODE;
extern char *const *environ;

static bool is_path(const char *cmd);
static bool is_executable(const char *path);

static char *find_in_path(const char *cmd);

void run_program(struct Program *program) {
    for (int i = 0; i < program->statement_count; ++i) {
        run_command(&program->statements[i].command);
    }
}

void run_command(struct Command *command) {
    struct String command_name = to_string(&command->command_name);
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
        struct String arg = to_string(&command->arguments.arguments[i]);
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
        if (!res) {
            executable = strndup(command_name.string, command_name.length);
        } else {
            executable = res;
        }
    }

    pid_t pid = fork();

    if (pid == 0) {
        int res = execve(executable, args, environ);
        if (res == -1) {
            cash_perror(EXIT_FAILURE, "execve", "%s", "");
        }
    }

    waitpid(pid, NULL, 0);
exit_command:
    free(executable);
    free(command_name.string);
    for (int i = 0; i < command->arguments.argument_count; ++i) {
        free(args[i]);
    }
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
        size_t len = strlen(dir) + strlen(cmd) + 2;
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
