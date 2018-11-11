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

%option noyywrap
%option yylineno
%option nodebug
%option reentrant
%option bison-bridge

%{
#define YY_DECL int yylex(YYTypeStruct* yylval_param, YYLTYPE* yylloc, Config::class_ &result, struct lineref &lineref, parserStaticData& staticData, void* yyscanner)
//char *buffer, int numBytesRead, int maxBytesToRead
#define YY_INPUT(b,r,s)  auto& inStr = *static_cast<std::istream*>(yyget_extra(yyscanner)); inStr.read(b, s); r = inStr.gcount()
#define YY_NO_UNISTD_H

#include <stdio.h>
#include <stdbool.h>
#include <iostream>
#include "utils.h"
#include "rapify.h"
#include "rapify.tab.hpp"

#define YY_NO_INPUT
#define YY_NO_UNPUT

#define YY_USER_ACTION \
    yylloc->first_line = yylloc->last_line = yylineno;

#define RESET_VARS \
    staticData.allow_val = false; \
    staticData.allow_arr = false; \
    staticData.last_was_class = false;
%}

%%

[ \t] {}
\n {}

";" {RESET_VARS; return T_SEMICOLON;}
":" {auto tmp = staticData.last_was_class; RESET_VARS; staticData.last_was_class = tmp; return T_COLON;}
"," {RESET_VARS; staticData.allow_arr = true; return T_COMMA;}
"+" {RESET_VARS; return T_PLUS;}
"=" {RESET_VARS; staticData.allow_val = true; return T_EQUALS;}
"]" {RESET_VARS; return T_RBRACKET;}
"[" {RESET_VARS; return T_LBRACKET;}
"}" {RESET_VARS; return T_RBRACE;}
"{" {auto tmp = !staticData.last_was_class; RESET_VARS; staticData.allow_arr = tmp; return T_LBRACE;}

"class" {RESET_VARS; staticData.last_was_class = true; return T_CLASS;}
"delete" {RESET_VARS; return T_DELETE;}

\s*[-+]?[0-9]+ {
    if (!staticData.allow_val && !staticData.allow_arr)
        REJECT;
    RESET_VARS;
    yylval->int_value = atol(yytext);
    return T_INT;
}

\s*[-+]?0x[0-9]+ {
    RESET_VARS;
    yylval->int_value = strtol(yytext, NULL, 16);
    return T_INT;
}

\s*[-+]?[0-9]*\.[0-9]+ {
    RESET_VARS;
    yylval->float_value = atof(yytext);
    return T_FLOAT;
}

\s*[-+]?([0-9]*\.)?[0-9]+[eE][-+]?[0-9]+ {
    RESET_VARS;
    yylval->string_value = std::string(yytext);
    return T_STRING;
}

\"(\\.|\"\"|[^"])*\"    {
    RESET_VARS;
    yylval->string_value = unescape_string({yytext, static_cast<size_t>(yyleng)});
    return T_STRING;
}

'(\\.|''|[^'])*' {
    RESET_VARS;
    yylval->string_value = unescape_string({yytext, static_cast<size_t>(yyleng)});
    return T_STRING;
}

[^;{"' \t\n][^;{\n]*/[ \t\n]*; {
    if (!staticData.allow_val)
        REJECT;

    auto trimmedString = trim({yytext, static_cast<size_t>(yyleng)});

    char *endptr;
    strtof(trimmedString.c_str(), &endptr);
    if (*endptr == 0)
        REJECT;

    lnwarningf(lineref.file_names[lineref.file_index[yylineno]].c_str(), lineref.line_number[yylineno],
            "unquoted-string", "String \"%s\" is not quoted properly.\n", yytext);

    RESET_VARS;
    yylval->string_value = trim({yytext, static_cast<size_t>(yyleng)});
    return T_STRING;
}

[^;,{"'} \t\n][^;,{}\n]*/[ \t\n]*[,}] {
    if (!staticData.allow_arr)
        REJECT;

    auto trimmedString = trim({yytext, static_cast<size_t>(yyleng)});

    char *endptr;
    strtof(trimmedString.c_str(), &endptr);
    if (*endptr == 0)
        REJECT;

    lnwarningf(lineref.file_names[lineref.file_index[yylineno]].c_str(), lineref.line_number[yylineno],
            "unquoted-string", "String \"%s\" is not quoted properly.\n", yytext);

    RESET_VARS;
    yylval->string_value = trim({yytext, static_cast<size_t>(yyleng)});
    return T_STRING;
}

[a-zA-Z0-9_]+ {
    if (staticData.allow_arr || staticData.allow_val)
        REJECT;

    bool tmp = staticData.last_was_class;
    RESET_VARS;
    staticData.last_was_class = tmp;

    yylval->string_value = std::string(yytext);
    return T_NAME;
}

. {}

%%
