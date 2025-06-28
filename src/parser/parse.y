/* %define api.location.type {YYLTYPE} */
%locations
%define api.pure full

%code requires {
#include <stdio.h>
#include <stdlib.h>
#include <cash/ast.h>

#define YY_DECL                                                        \
    int cash_lex(YYSTYPE* lvalp, YYLTYPE* llocp, yyscan_t yyscanner, \
                 struct Driver* driver)

#define yylex(lvalp, llocp, yyscanner, driver)  cash_lex(lvalp, llocp, yyscanner, driver)

typedef void* yyscan_t;
struct Driver;
// extern YYLTYPE llocp;
}

%code provides {
    #include <cash/parser/parser_driver.h>
    // #include <lexer.yy.h>

    int cash_lex(YYSTYPE* lvalp, YYLTYPE* llocp, yyscan_t yyscanner, struct Driver* driver);
    void yyerror(YYLTYPE* llocp, struct Driver* driver, yyscan_t yyscanner, const char*);
}

%union {
    const char* name;
    struct ShellString word;
    struct ArgumentList arg_list;
    struct Command command;
    struct Stmt stmt;
    struct Program program;
    long number;
};

%token <word> WORD
%token <number> NUMBER
%token <name> IDENTIFIER
%token CMD_SUB_START    "$("
%token CMD_SUB_END      ")"
%token SEMICOLON        ";"
%token LINE_BREAK       "\n"
%token PIPE             "|"
%token RE_IN            "<"
%token RE_OUT           ">"
%token RE_OUT_FORCE     ">|"
%token RE_OUT_APPEND    ">>"
%token HEREDOC          "<<"
%token INDENT_HEREDOC   "<<-"
%token RE_OUT_ALL       "&>"
%token RE_OUT_ALL_APPEND "&>>"
%token RE_IN_STDINOUT   "<&"
%token READ_WRITE       "<>"
%token REDIRECT_APPEND  ">>="

/* %type <program> program */
%type <stmt> statement
%type <stmt> statement_with_terminate
%type <arg_list> command_args
%type <command> command

/* %lex-param  { YYSTYPE *lvalp, YYLTYPE* llocp } */
%param      { yyscan_t yyscanner }
%param      { struct Driver* driver }

%start program

%%

program:
    statements              
;

statements:
    %empty                              
  | statement                           { add_statement(&driver->program, $1); }
  | statement_with_terminate
        { add_statement(&driver->program, $1); }
    statements 

statement:
    command     { $$ = (struct Stmt){ .command = $1 }; }
;

statement_with_terminate:
    statement command_terminate
;

command_terminate:
      SEMICOLON
    | SEMICOLON LINE_BREAK
    | LINE_BREAK
;

command_args:
    %empty              { $$ = make_arg_list(); }
  | command_args WORD   { add_argument(&$1, $2);
                          $$ = $1;
                        }
;

command:
    WORD command_args   { $$ = (struct Command){ .command_name = $1, .arguments = $2 }; }
;

%%

void yyerror(YYLTYPE* llocp, struct Driver* driver, yyscan_t scanner, const char* msg) {
    fprintf(stderr, "Error at (%d-%d):(%d-%d) - %s\n",
            llocp->first_line, llocp->last_line, llocp->first_column, llocp->last_column, msg);
}
