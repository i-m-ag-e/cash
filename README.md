### For IITGN Technical Council, Summer Siege Submission
- This project requires the `readline` library to be installed. It also assumes a mostly POSIX compliant Linux distribution.
- I have included a `test_shell_features.sh` file that showcases the basic features of the shell. Passing different number of arguments will reflect in the output
```shell
./cash ../test_shell_features.sh 
./cash ../test_shell_features.sh a b c 
```
- Since job control is not active in script execution mode, it can only be seen in REPL mode. I have included a file hello.sh which can be used to test job control. Sample test (bash is needed because the file uses if statements and command substituion)
```shell
$> ./cash
prompt> bash ../hello.sh cmd_1 60 & 
prompt> bash ../hello.sh cmd_2 15 &
prompt> jobs
[2] (94214) Running		bash hello.sh cmd_2 15
[1] (94178) Running		bash hello.sh cmd_1 60
prompt> sleep 15 && jobs
[3] (95758) Completed	sleep 15
[2] (94214) Completed	bash hello.sh cmd_2 15
[1] (94178) Running		bash hello.sh cmd_1 60
prompt> fg 1
bash hello.sh cmd_1 60
hello 20 from cmd_1
hello 21 from cmd_1
hello 22 from cmd_1
...
```

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
    - `jobs` to list all jobs
    - `fg` to bring a background job to the foreground
    - `exit` to exit the shell
- Set `$OLDPWD` and `$PWD` environment variables, whenever directory changes
- Expand `$?` variable to the exit status of the last command executed
- Expand `$#` and `$n` to the number of arguments passed to the shell and the nth argument respectively (only in script execution mode)
- Handle piped lists of commands (using `|`)
- Handle redirections (`i` and `j` are file descriptors, `file` is a path):
    - `[i]> file`
    - `[i]>> file`
    - `[i]&> file`
    - `[i]&>> file`
    - `[i]< file`
    - `[i]<> file`
    - `[i]>&[j]` 
- Handle job control (only in REPL mode):
    - Background jobs using `&`
    - Foreground jobs using `fg`
    - List jobs using `jobs`

## Major Problems
- Comments are not supported (never got around to it, although quite simple to implement).
- Handling of signals is very messy and unpredictable. 
- Subshells (`()`) and AND/OR lists do not work as background processes (the `&` is just ignored).
- The foundation for command substitution is there (recursive parsing, pipes, subshells), but it is not implemented yet.
- Has very, very, very messy error handling. (mostly because of my inexperience in doing so in C).

## Usage

The shell depends on the `readline` library. On most Linux distributions, it can be installed using the package manager. For example:
- **Ubuntu/Debian**: `sudo apt-get install libreadline-dev`
- **Fedora**: `sudo dnf install readline-devel`
- **Arch Linux**: `sudo pacman -S readline`
- **macOS (Homebrew)**: `brew install readline`

Ensure your compiler can find `readline/readline.h`, `readline/history.h` and link with `-lreadline` (should be done already if you installed using one of the above commands).

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
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . 
```

Building in debug mode enables a lot of debug informtion being printed onto the scree, which might not be desirable.

Inside the `build` directory, run the `cash` executable either with no arguments for REPL mode, or with one argument,
which is the bash file you wish to execute

```sh
./cash
# or
./cash test.sh
```

A string can also be excuted using the `-c` flag. Redirection from a file or pipe will also execute the contents of stdin as a program

```sh
./cash -c 'echo "Hello, World!"'
./cash < test.sh
```
