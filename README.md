## CaSH: A toy Linux shell

Currently supports:
- Parsing of basic commands
- Can recognize basic escape sequences and supports double quoted, single quoted and unquoted strings
- Can execute executables given the full path, or any executable in any directory present in `$PATH`
- Supports arrow key navigation and other functionalities provided by `readline`
- REPL mode and file execution mode

## Usage

The project currently has no unit tests. To compile the shell, a C compiler with atleast C17 support and `cmake` is required.

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

Inside the `build` directory, run the `cash` executable either with no arguments for REPL mode, or with one argument, which is the bash file you wish to execute
```sh
./cash
# or
./cash test.sh
```
