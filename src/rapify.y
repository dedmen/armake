/*
 * Copyright (C)  2016  Felix "KoffeinFlummi" Wiegand
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

%{
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <iostream>
#include <string>

#include "utils.h"
#include "filesystem.h"
#include "preprocess.h"
#include "rapify.h"

#define YYDEBUG 0
#define YYERROR_VERBOSE 1

struct parserStaticData {
    bool allow_val = false;
    bool allow_arr = false;
    bool last_was_class = false;
};

struct YYTypeStruct {
    std::vector<Config::definition> definitions_value;
    Config::class_ class_value;
    Config::variable variable_value;
    Config::expression expression_value;
    int32_t int_value;
    float float_value;
    std::string string_value;
};

struct YYLTYPE;
extern int yylex(YYTypeStruct* yylval_param, YYLTYPE* yylloc, Config::class_ &result, struct lineref &lineref, parserStaticData& staticData, void* yyscanner);
extern int yyparse();
void yyerror(YYLTYPE* yylloc, Config::class_ &result, struct lineref &lineref, parserStaticData& staticData, void* yyscanner, const char* s);

%}

%define api.value.type {struct YYTypeStruct}
%pure-parser

%token<string_value> T_NAME
%token<int_value> T_INT
%token<float_value> T_FLOAT
%token<string_value> T_STRING
%token T_CLASS T_DELETE
%token T_SEMICOLON T_COLON T_COMMA T_EQUALS T_PLUS
%token T_LBRACE T_RBRACE
%token T_LBRACKET T_RBRACKET

%type<definitions_value> definitions
%type<class_value> class_
%type<variable_value> variable
%type<expression_value> expression expressions

%start start

%param {Config::class_ &result} {struct lineref &lineref} {parserStaticData& staticData} {void* yyscanner}
%locations

%%
start: definitions { result = Config::class_($1); }

definitions:  /* empty */ { $$ = std::vector<Config::definition>(); }
            | definitions class_ { $$.emplace_back(Config::rap_type::rap_class, $2); }
            | definitions variable { $$.emplace_back(Config::rap_type::rap_var, $2); }
;

class_:        T_CLASS T_NAME T_LBRACE definitions T_RBRACE T_SEMICOLON { $$ = Config::class_($2, $4, false); }
            | T_CLASS T_NAME T_COLON T_NAME T_LBRACE definitions T_RBRACE T_SEMICOLON { $$ = Config::class_($2, $4, $6, false); }
            | T_CLASS T_NAME T_SEMICOLON { $$ = Config::class_($2, {}, false); }
            | T_CLASS T_NAME T_COLON T_NAME T_SEMICOLON { $$ = Config::class_($2, $4, {}, false); }
            | T_DELETE T_NAME T_SEMICOLON { $$ = Config::class_($2, {}, true); }
;

variable:     T_NAME T_EQUALS expression T_SEMICOLON { $$ = Config::variable(Config::rap_type::rap_var, $1, $3); }
            | T_NAME T_LBRACKET T_RBRACKET T_EQUALS expression T_SEMICOLON { $$ = Config::variable(Config::rap_type::rap_array, $1, $5); }
            | T_NAME T_LBRACKET T_RBRACKET T_PLUS T_EQUALS expression T_SEMICOLON { $$ = Config::variable(Config::rap_type::rap_array_expansion, $1, $6); }
;

expression:   T_INT { $$ = Config::expression(Config::rap_type::rap_int, $1); }
            | T_FLOAT { $$ = Config::expression(Config::rap_type::rap_float, $1); }
            | T_STRING { $$ = Config::expression(Config::rap_type::rap_string, $1); }
            | T_LBRACE expressions T_RBRACE { $$ = Config::expression(Config::rap_type::rap_array, {$2}); }
            | T_LBRACE expressions T_COMMA T_RBRACE { $$ = Config::expression(Config::rap_type::rap_array, {$2}); }
            | T_LBRACE T_RBRACE { $$ = Config::expression(Config::rap_type::rap_array, std::vector<Config::expression>{}); }
;

expressions:  expression { $$ = $1; }
            | expressions T_COMMA expression { $$.addArrayElement($3); }
;
%%


void yyset_extra(void* user_defined, void* yyscanner);
void* yyget_extra(void* yyscanner);
int yylex_init(void** ptr_yy_globals);
int yylex_destroy(void* yyscanner);

bool parse_file(std::istream& f, struct lineref &lineref, Config::class_ &result) {
#if YYDEBUG == 1
    yydebug = 1;
#endif
    void* yyscanner;
    yylex_init(&yyscanner);
    yyset_extra(&f, yyscanner);
    parserStaticData staticData;


    do { 
        if (yyparse(result, lineref, staticData, yyscanner)) {
            return false;
        }
    } while(!f.eof());

    yylex_destroy(yyscanner);

    return true;
}

void yyerror(YYLTYPE* yylloc, Config::class_ &result, struct lineref &lineref, parserStaticData& staticData, void* yyscanner, const char* s) {
    int line = 0;

    auto& inputStream = *static_cast<std::istream*>(yyget_extra(yyscanner));
    inputStream.seekg(0);

    std::string text;

    while (line < yylloc->first_line) {
        std::string newLine;
        std::getline(inputStream, newLine);
        text += newLine;

        line++;
    }

    lerrorf(lineref.file_names[lineref.file_index[yylloc->first_line]].c_str(),
            lineref.line_number[yylloc->first_line], "%s\n", s);

    fprintf(stderr, " %s", text.c_str());
}
