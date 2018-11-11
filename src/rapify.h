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
#include <utility>
#include <variant>
#include <vector>
#include <memory>
#include <map>

#define MAXCLASSES 4096

class Config {
public:
    enum class rap_type {
        rap_class,
        rap_var,
        rap_array,
        rap_array_expansion,
        rap_string,
        rap_int,
        rap_float
    };

    struct definition;
    struct expression {
        expression() = default;
        expression(rap_type t, int32_t x) : type(t), value(x) {}
        expression(rap_type t, float x) : type(t), value(x) {}
        expression(rap_type t, std::string x) : type(t), value(std::move(x)) {}
        expression(rap_type t, std::vector<expression> x) : type(t), value(std::move(x)) {}
        expression(rap_type t, expression x) : type(t) {
            if (t == rap_type::rap_array && x.type == rap_type::rap_array) {
                value = x.value;
                return;
            }
            if (t == rap_type::rap_array) {
                addArrayElement(x);
                return;
            }
            __debugbreak(); //#TODO remove

        }
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
        variable() = default;
        variable(rap_type type, std::string name, struct expression expression)
            : type(type), name(std::move(name)), expression(std::move(expression)) {}
        rap_type type;
        std::string name;
        struct expression expression;
    };

    struct class_ {
        class_() = default;
        class_(std::string name, std::string parent, std::vector<definition> content, bool is_delete)
            : name(std::move(name)), parent(std::move(parent)), is_delete(is_delete), content(std::move(content)) {}
        class_(std::string name, std::vector<definition> content, bool is_delete)
            : name(std::move(name)), parent(std::move(parent)), is_delete(is_delete), content(std::move(content)) {}
        class_(std::vector<definition> content)
            : content(std::move(content)) {}

        std::string name;
        std::string parent;
        bool is_delete{ false };
        bool is_definition{ false };
        long offset_location{ 0 };
        std::vector<definition> content;


        static std::vector<std::reference_wrapper<class_>> getParents(class_& scope, class_ &entry);
        static std::optional<std::reference_wrapper<definition>> getEntry(class_& curClass, std::initializer_list<std::string_view> path);
        static std::map<std::string, std::reference_wrapper<Config::variable>> getEntries(class_& scope, class_ &entry);
        static std::map<std::string, std::reference_wrapper<Config::class_>> getSubclasses(class_& scope, class_ &entry);
        std::optional<std::reference_wrapper<class_>> getClass(std::initializer_list<std::string_view> path);
        std::vector<std::reference_wrapper<class_>> getSubClasses();
        std::optional<int32_t> getInt(std::initializer_list<std::string_view> path);
        std::optional<float> getFloat(std::initializer_list<std::string_view> path);
        std::optional<std::string> getString(std::initializer_list<std::string_view> path);
        std::vector<std::string> getArrayOfStrings(std::initializer_list<std::string_view> path);
        std::vector<float> getArrayOfFloats(std::initializer_list<std::string_view> path);


    };

    struct definition {
        definition(rap_type t, variable v) : type(t), content(std::move(v)) {}
        definition(rap_type t, class_ c) : type(t), content(std::move(c)) {}
        rap_type type;
        std::variant<variable, class_> content;
    };

    static Config fromPreprocessedText(std::istream &input, struct lineref &lineref);
    static Config fromBinarized(std::istream &input);
    void toBinarized(std::ostream &output);
    void toPlainText(std::ostream &output, std::string_view indent = "    ");

    bool hasConfig() { return static_cast<bool>(config); }
    class_& getConfig() { return *config; }

    


    operator class_&(){
        return *config;
    }

private:
    std::shared_ptr<class_> config;
};


class Rapifier {
    static void rapify_expression(Config::expression &expr, std::ostream &f_target);

    static void rapify_variable(Config::variable &var, std::ostream &f_target);

    static void rapify_class(Config::class_ &class_, std::ostream &f_target);


public:
    static bool isRapified(std::istream &input);
    static int rapify_file(const char* source, const char* target);
    static int rapify_file(std::istream &source, std::ostream &target, const char* sourceFileName);

    static int rapify(Config::class_& cls, std::ostream& output);
};
