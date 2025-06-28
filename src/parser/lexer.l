%{
#include <cash/ast.h>
#include "parse.tab.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

static int curr_line = 1;
static int curr_col = 1;
static bool continue_string = false;

#define YY_USER_ACTION \
    yylloc->first_line = curr_line; \
    yylloc->first_column = curr_col; \
    for (int i = 0; i < yyleng; ++i) { \
        if (yytext[i] == '\n') { curr_line++; curr_col = 1; } \
        else curr_col++; \
    } \
    yylloc->last_line = curr_line; \
    yylloc->last_column = curr_col;
%}

%option reentrant
/* %option bison-bridge */
%option noyywrap

PUNCTUATION     [;|()&<>`$'" \t\n\\]
NOT_PUNCTUATION [^;|()&<>`$'" \t\n\\]

%x STRING
%x DOUBLE_Q_STRING
%x SINGLE_Q_STRING

%%

-?[0-9]+{PUNCTUATION}   {   assert(!continue_string);
                            yylval->number = strtol(yytext, NULL, 10);
                            unput(yytext[yyleng - 1]);
                            return NUMBER;
                        }

-?[0-9]+{NOT_PUNCTUATION}   { BEGIN(STRING); yymore(); }

[ \t\r]+                ; // { printf("skipping WS\n"); }  // Skip whitespace
\n[ \t\r\n]*            {   return LINE_BREAK; }

";"                     {   return SEMICOLON; }

"|"                     {   return PIPE; }

{NOT_PUNCTUATION}       |
"\""                    |
"'"                     {   assert(!continue_string);
                            yylval->word = make_string();
                            continue_string = true;

                            if (yytext[yyleng - 1] == '"')
                                BEGIN(DOUBLE_Q_STRING);
                            else if (yytext[yyleng - 1] == '\'')
                                BEGIN(SINGLE_Q_STRING);
                            else
                                BEGIN(STRING);
                            yymore();
                        }

"#[^\n]*\n"             ; // comment

.                       {   fprintf(stderr, "Syntax Error: Unknown character '%c'\n", yytext[0]); }
<INITIAL><<EOF>>        {   return YYEOF; }

<STRING>{
    "\""                {   if (yyleng > 1)
                                add_string_literal(&yylval->word, yytext, yyleng - 1);
                            BEGIN(DOUBLE_Q_STRING);    
                        } 
    "'"                 {   if (yyleng > 1)
                                add_string_literal(&yylval->word, yytext, yyleng - 1);
                            BEGIN(SINGLE_Q_STRING);    
                        }
    \\{PUNCTUATION}     {   yymore(); }
    {PUNCTUATION}       {   continue_string = false;
                            
                            if (yyleng > 1)
                                add_string_literal(&yylval->word, yytext, yyleng - 1);

                            unput(yytext[yyleng - 1]);
                            BEGIN(INITIAL);
                            return WORD;
                        }
    .                   {   yymore(); }
    <<EOF>>             {   add_string_literal(&yylval->word, yytext, yyleng);
                            return YYEOF;
                        }
}

<DOUBLE_Q_STRING>{
    "\""                {   add_string_component(&yylval->word, STRING_COMPONENT_DQ, yytext, yyleng - 1);
                            BEGIN(STRING);
                        }

    "\\\""              {   yymore(); }

    .                   {   yymore(); }
    <<EOF>>             {   fprintf(stderr, "Unexpected EOF in string literl\n");
                            exit(EXIT_FAILURE);
                        }
}

<SINGLE_Q_STRING>{
    "'"                 {   add_string_component(&yylval->word, STRING_COMPONENT_SQ, yytext, yyleng - 1);
                            BEGIN(STRING);
                        }
    .                   {   yymore(); }
    <<EOF>>             {   fprintf(stderr, "Unexpected EOF in string literl\n");
                            exit(EXIT_FAILURE);
                        }
}

%%


