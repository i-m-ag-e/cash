## CaSH: A toy Linux shell

Currently, it can:

- Parse basic commands
- Recognize basic escape sequences and supports double-quoted, single quoted and unquoted strings
- Execute executables given the full path, or any executable in any directory present in `$PATH`
- Support arrow key navigation and other functionalities provided by `readline` (including history of current session)
- Execute in REPL or file execution mode
- Handle `&&` and `||` lists and the `!` operator
- Handle execution in a subshell (using `()`)
- Expand environment variables (using `$` only, `${}` doesn't work yet)
- Handle shell builtings:
    - `cd` to change directories (supports `-` to switch to previous directory)
    - `exit` to exit the shell
- Set `$OLDPWD` and `$PWD` environment variables, whenever directory changes
- Set `$?` environment variable to the exit status of the last command executed
- Handle piped lists of commands (using `|`)
- Handle redirections (`i` and `j` are file descriptors, `file` is a path):
    - `[i]> file`
    - `[i]>> file`
    - `[i]&> file`
    - `[i]&>> file`
    - `[i]< file`
    - `[i]<> file`
    - `[i]>&[j]` 

## Major Problems
- Does not handle signals (like `SIGINT`, `SIGTERM`, etc.) yet
- Has very, very, very messy error handling. (mostly because of my inexperience in doing so in C).

## Usage

The project currently has no unit tests. To compile the shell, a C compiler with atleast C17 support and `cmake` is
required.

Clone the repository

```sh
git clone https://github.com/i-m-ag-e/cash.git
```

Create a directory to build the shell into

```sh
cd cash
mkdir build
cd build
```

Create the CMake configuration files into the build directory and build the repo

```sh
cmake ..
cmake --build .
```

Inside the `build` directory, run the `cash` executable either with no arguments for REPL mode, or with one argument,
which is the bash file you wish to execute

```sh
./cash
# or
./cash test.sh
```
