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

#pragma once


#include "preprocess.h"
#include <variant>
#include <vector>


#define MAXCLASSES 4096

enum class rap_type {
    rap_class,
    rap_var,
    rap_array,
    rap_array_expansion,
    rap_string,
    rap_int,
    rap_float
};
//enum {
//    TYPE_CLASS,
//    TYPE_VAR,
//    TYPE_ARRAY,
//    TYPE_ARRAY_EXPANSION,
//    TYPE_STRING,
//    TYPE_INT,
//    TYPE_FLOAT
//};

struct definitions {
    struct definition *head;
};

struct class_ {
    std::string name;
    std::string parent;
    bool is_delete;
    long offset_location;
    std::vector<definition> content;
};

struct expression {
    expression() {}
    expression(rap_type t, int32_t x): type(t), value(x) {}
    expression(rap_type t, float x): type(t), value(x) {}
    expression(rap_type t, std::string x): type(t), value(std::move(x)) {}
    expression(rap_type t, std::vector<expression> x): type(t), value(std::move(x)) {}
    rap_type type;
    std::variant<int32_t, float, std::string, std::vector<expression>> value;
    void addArrayElement(expression exp) {
        if (std::holds_alternative<std::vector<expression>>(value)) {
            std::get<std::vector<expression>>(value).emplace_back(std::move(exp));
            return;
        }
            

        std::vector<expression> newVal;

        std::visit([&newVal](auto&& arg) {

            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int>)
                newVal.emplace_back(rap_type::rap_int, arg);
            else if constexpr (std::is_same_v<T, float>)
                newVal.emplace_back(rap_type::rap_float, arg);
            else if constexpr (std::is_same_v<T, std::string>)
                newVal.emplace_back(rap_type::rap_string, arg);
        }, value);
        newVal.emplace_back(std::move(exp));
        value = newVal;
        type = rap_type::rap_array;
    }
};

struct variable {
    rap_type type;
    std::string name;
    struct expression expression;
};

struct definition {
    definition(){}
    definition(rap_type t, variable v) : type(t), content(std::move(v)) {}
    definition(rap_type t, class_ c): type(t), content(std::move(c)) {}
    rap_type type;
    std::variant<variable, class_> content;
};


std::optional<struct class_> parse_file(std::istream &f, struct lineref &lineref);

struct class_ new_class(std::string name, std::string parent, std::vector<definition> content, bool is_delete);

struct variable new_variable(rap_type type, std::string name, struct expression expression);

expression new_expression(rap_type type, void *value);

void rapify_expression(struct expression &expr, FILE *f_target);

void rapify_variable(struct variable &var, FILE *f_target);

void rapify_class(struct class_ &class_, FILE *f_target);

int rapify_file(char *source, char *target);
