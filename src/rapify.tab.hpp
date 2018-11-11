/* A Bison parser, made by GNU Bison 3.1.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

#ifndef YY_YY_RAPIFY_TAB_HPP_INCLUDED
# define YY_YY_RAPIFY_TAB_HPP_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif
/* "%code requires" blocks.  */


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




/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    T_NAME = 258,
    T_INT = 259,
    T_FLOAT = 260,
    T_STRING = 261,
    T_CLASS = 262,
    T_DELETE = 263,
    T_SEMICOLON = 264,
    T_COLON = 265,
    T_COMMA = 266,
    T_EQUALS = 267,
    T_PLUS = 268,
    T_LBRACE = 269,
    T_RBRACE = 270,
    T_LBRACKET = 271,
    T_RBRACKET = 272
  };
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef YYTypeStruct YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif

/* Location type.  */
#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE YYLTYPE;
struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif



int yyparse (Config::class_ &result, struct lineref &lineref, parserStaticData& staticData, void* yyscanner);

#endif /* !YY_YY_RAPIFY_TAB_HPP_INCLUDED  */
