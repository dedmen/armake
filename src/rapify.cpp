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


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
////#include <unistd.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif

#include "filesystem.h"
#include "utils.h"
#include "preprocess.h"
#include "rapify.h"
#include "derapify.h"
#include <sstream>
#include <iostream>
#include <fstream>
#include <iterator>
#include <functional>
#include <algorithm>
//#include "rapify.tab.h"

bool parse_file(std::istream& f, struct lineref &lineref, Config::class_ &result);

std::optional<std::vector<Config::expression>> derapify_array(std::istream &source) {
    std::vector<Config::expression> output;
    uint32_t num_entries = read_compressed_int(source);

    for (int i = 0; i < num_entries; i++) {
        uint8_t type = source.get();

        if (type == 0) {
            std::string value;
            std::getline(source, value, '\0');

            output.emplace_back(Config::rap_type::rap_string, escape_string(value));
        }
        else if (type == 1) {
            float float_value;
            source.read(reinterpret_cast<char*>(&float_value), sizeof(float_value));

            output.emplace_back(Config::rap_type::rap_float, float_value);
        }
        else if (type == 2) {
            int32_t long_value;
            source.read(reinterpret_cast<char*>(&long_value), sizeof(long_value));

            output.emplace_back(Config::rap_type::rap_int, long_value);
        }
        else if (type == 3) {
            auto result = derapify_array(source);

            if (!result) {
                errorf("Failed to derapify subarray.\n");
                return {};
            }
            output.emplace_back(Config::rap_type::rap_array, std::move(*result));
        }
        else {
            errorf("Unknown array element type %i.\n", type);
            return {};
        }
    }

    return output;
}

int derapify_class(std::istream &source, Config::class_ &curClass, int level) {
    if (curClass.name.empty()) {
        source.seekg(16);
    }

    uint32_t fp_tmp = source.tellg();
    std::string inherited;
    std::getline(source, inherited, '\0');

    uint32_t num_entries = read_compressed_int(source);

    if (!curClass.name.empty()) {
        curClass.parent = inherited;
    }

    for (int i = 0; i < num_entries; i++) {
        uint8_t type = source.get();

        if (type == 0) { //class definition
            Config::class_ subclass;


            std::getline(source, subclass.name, '\0');

            uint32_t fp_class;
            source.read(reinterpret_cast<char*>(&fp_class), sizeof(uint32_t));
            fp_tmp = source.tellg();
            source.seekg(fp_class);

            auto success = derapify_class(source, subclass, level + 1);

            if (success) {
                errorf("Failed to derapify class \"%s\".\n", subclass.name.c_str());
                return success;
            }

            curClass.content.emplace_back(Config::rap_type::rap_class, std::move(subclass));
            source.seekg(fp_tmp);
        }
        else if (type == 1) { //value
         //#TODO make enums for these
         //Var has special type
         //0 string
         //1 float
         //2 int

            type = source.get();

            std::string valueName;
            std::getline(source, valueName, '\0');

            Config::rap_type valueType;
            Config::expression valueContent;

            if (type == 0) {
                std::string value;
                std::getline(source, value, '\0');

                valueType = valueContent.type = Config::rap_type::rap_string;
                valueContent.value = escape_string(value);
            } else if (type == 1) {
                float float_value;
                source.read((char*)&float_value, sizeof(float_value));

                valueType = valueContent.type = Config::rap_type::rap_float;
                valueContent.value = float_value;
            } else if (type == 2) {
                int32_t long_value;
                source.read((char*)&long_value, sizeof(long_value));

                valueType = valueContent.type = Config::rap_type::rap_int;
                valueContent.value = long_value;
            } else {
                errorf("Unknown token type %i.\n", type);
                return 1;
            }

            curClass.content.emplace_back(Config::rap_type::rap_var, Config::variable(valueType, std::move(valueName), std::move(valueContent)));

        }
        else if (type == 2 || type == 5) { //array or array append
            if (type == 5)
                source.seekg(4, std::istream::_Seekcur);

            std::string valueName;
            std::getline(source, valueName, '\0');

            auto rapType = type == 2 ? Config::rap_type::rap_array : Config::rap_type::rap_array_expansion;

            auto result = derapify_array(source);
            if (result) { //#TODO throw error if not? derapify array already throws errors

                curClass.content.emplace_back(Config::rap_type::rap_var,
                    Config::variable(rapType, std::move(valueName),
                        Config::expression(rapType, std::move(*result))
                    )
                );
            }


        }
        else if (type == 3 || type == 4) { //3 is class 4 is delete class
            std::string className;
            std::getline(source, className, '\0');

            Config::class_ subclass;
            subclass.name = className;
            subclass.is_delete = type == 4;
            subclass.is_definition = true;

            curClass.content.emplace_back(Config::rap_type::rap_class, std::move(subclass));
        }
        else {
            errorf("Unknown class entry type %i.\n", type);
            return 2;
        }
    }

    return 0;
}

std::vector<std::reference_wrapper<Config::class_>> Config::class_::getParents(class_& scope, class_ &entry) {
    auto& parName = entry.parent;
    std::vector<std::reference_wrapper<Config::class_>> ret;

    while (!parName.empty()) {
        for (auto& it : scope.content) {
            if (it.type != rap_type::rap_class) continue;
            auto& c = std::get<Config::class_>(it.content);
            if (c.name == parName) {
                ret.emplace_back(c);
                parName = c.parent;
                break;
            }
        }
    }
    return ret;
}

bool iequals(std::string_view a, std::string_view b)
{
    return std::equal(a.begin(), a.end(),
        b.begin(), b.end(),
        [](char a, char b) {
        return tolower(a) == tolower(b);
    });
}

std::optional<std::reference_wrapper<Config::definition>> Config::class_::getEntry(class_& curClass, std::initializer_list<std::string_view> path) {

    std::string_view subelement = *path.begin();

    auto found = std::find_if(curClass.content.begin(), curClass.content.end(), [&subelement](const definition& def) {
        if (def.type == rap_type::rap_var) {
            auto& c = std::get<variable>(def.content);
            if (iequals(c.name, subelement)) return true;
        }
        else if (def.type == rap_type::rap_class) {
            auto& c = std::get<class_>(def.content);
            if (iequals(c.name, subelement)) return true;
        }
        return false;
    });
    if (found == curClass.content.end()) {
        return {};
    }

    if (path.size() > 1) {
        if (found->type != rap_type::rap_class) return {}; //not at end and can't go further
        auto& c = std::get<class_>(found->content);

        auto parents = getParents(curClass, c);

        for (auto& it : parents) {
            auto subresult = it.get().getEntry(c, std::initializer_list<std::string_view>(path.begin() + 1, path.end()));
            if (subresult)
                return subresult;
        }
    }
    return *found;
}

std::map<std::string, std::reference_wrapper<Config::variable>> Config::class_::getEntries(class_& scope, class_& entry) {
    std::map<std::string, std::reference_wrapper<Config::variable>> ret;
    auto parents = getParents(scope, entry);

    for (auto& it : parents) {
        for (auto& elem : it.get().content) {
            if (elem.type != rap_type::rap_var) continue;
            auto& c = std::get<Config::variable>(elem.content);
            ret.insert_or_assign(c.name, c);
        }
    }

    for (auto& elem : entry.content) {
        if (elem.type != rap_type::rap_var) continue;
        auto& c = std::get<Config::variable>(elem.content);
        ret.insert_or_assign(c.name, c);
    }
    return ret;
}
std::map<std::string, std::reference_wrapper<Config::class_>> Config::class_::getSubclasses(class_& scope, class_& entry) {
    std::map<std::string, std::reference_wrapper<Config::class_>> ret;
    auto parents = getParents(scope, entry);

    for (auto& it : parents) {
        for (auto& elem : it.get().content) {
            if (elem.type != rap_type::rap_class) continue;
            auto& c = std::get<Config::class_>(elem.content);
            ret.insert_or_assign(c.name, c);
        }
    }

    for (auto& elem : entry.content) {
        if (elem.type != rap_type::rap_class) continue;
        auto& c = std::get<Config::class_>(elem.content);
        ret.insert_or_assign(c.name, c);
    }
    return ret;
}

std::optional<std::reference_wrapper<Config::class_>> Config::class_::getClass(std::initializer_list<std::string_view> path) {
    auto entry = getEntry(*this,path);
    if (!entry) return {};
    auto& def = *entry;
    if (def.get().type == rap_type::rap_class)
        return std::get<class_>(def.get().content);
    return {};
}

std::vector<std::reference_wrapper<Config::class_>> Config::class_::getSubClasses() {
    std::vector<std::reference_wrapper<Config::class_>> ret;
    for (auto& it : content) {
        if (it.type == rap_type::rap_class)
            ret.emplace_back(it);
    }
    return ret;
}

std::optional<int32_t> Config::class_::getInt(std::initializer_list<std::string_view> path) {
    auto entry = getEntry(*this, path);
    if (!entry) return {};
    auto& def = *entry;
    if (def.get().type == rap_type::rap_var) {
        auto& var = std::get<variable>(def.get().content);
        if (var.type == rap_type::rap_int)
            return std::get<int32_t>(var.expression.value);
    }
    return {};
}
std::optional<float> Config::class_::getFloat(std::initializer_list<std::string_view> path) {
    auto entry = getEntry(*this, path);
    if (!entry) return {};
    auto& def = *entry;
    if (def.get().type == rap_type::rap_var) {
        auto& var = std::get<variable>(def.get().content);
        if (var.type == rap_type::rap_int)
            return std::get<float>(var.expression.value);
    }
    return {};
}
std::optional<std::string> Config::class_::getString(std::initializer_list<std::string_view> path) {
    auto entry = getEntry(*this, path);
    if (!entry) return {};
    auto& def = *entry;
    if (def.get().type == rap_type::rap_var) {
        auto& var = std::get<variable>(def.get().content);
        if (var.type == rap_type::rap_int)
            return std::get<std::string>(var.expression.value);
    }
    return {};
}

std::vector<std::string> Config::class_::getArrayOfStrings(std::initializer_list<std::string_view> path) {
    
    auto entry = getEntry(*this, path);
    if (!entry) return {};
    auto& def = *entry;
    if (def.get().type == rap_type::rap_var) {
        auto& var = std::get<variable>(def.get().content);
        if (var.type == rap_type::rap_array) {
            auto& arr = std::get<std::vector<expression>>(var.expression.value);
            std::vector<std::string> ret;
            for (auto& it : arr) {
                if (it.type != rap_type::rap_string) continue;
                ret.push_back(std::get<std::string>(it.value));
            }
            return ret;
        }
    }
    return {};

}

std::vector<float> Config::class_::getArrayOfFloats(std::initializer_list<std::string_view> path) {
    auto entry = getEntry(*this, path);
    if (!entry) return {};
    auto& def = *entry;
    if (def.get().type == rap_type::rap_var) {
        auto& var = std::get<variable>(def.get().content);
        if (var.type == rap_type::rap_array) {
            auto& arr = std::get<std::vector<expression>>(var.expression.value);
            std::vector<float> ret;
            for (auto& it : arr) {
                if (it.type != rap_type::rap_float) continue;
                ret.push_back(std::get<float>(it.value));
            }
            return ret;
        }
    }
    return {};
}

Config Config::fromPreprocessedText(std::istream &input, lineref& lineref) {
    Config output;
    output.config = std::make_shared<class_>();
    input.seekg(0);
    auto result = parse_file(input, lineref, *output.config);

    if (!result) {
        errorf("Failed to parse config.\n");
        return {};
    }
    return output;
}

Config Config::fromBinarized(std::istream & input) {
    if (!Rapifier::isRapified(input)) {
        errorf("Source file is not a rapified config.\n");
        return {};
    }

    Config output;
    output.config = std::make_shared<class_>();
    input.seekg(0);


    auto success = derapify_class(input, *output.config, 0);

    if (success) {
        errorf("Failed to parse config.\n");
        return {};
    }
    return output;
}

void Config::toBinarized(std::ostream& output) {
    if (!hasConfig()) return;

    Rapifier::rapify(getConfig(), output);
}

void Config::toPlainText(std::ostream& output, std::string_view indent) {

    uint8_t indentLevel = 0;
    if(!hasConfig()) return;

    auto pushIndent = [&]() {
        for (int i = 0; i < indentLevel; ++i) {
            output.write(indent.data(), indent.length());
        }
    };

    std::function<void(std::vector<Config::expression>&)> printArray = [&](std::vector<Config::expression>& data) {
        if (data.empty()) {
            output << "{}";
            return;
        }

        output << "{";
        for (auto& it : data) {
            switch (it.type) {
            case Config::rap_type::rap_string:
                output << "\"" << std::get<std::string>(it.value) << "\", ";
                break;
            case Config::rap_type::rap_int:
                output << std::get<int>(it.value) << ", ";
                break;
            case Config::rap_type::rap_float:
                output << std::get<float>(it.value) << ", ";
                break;
            case Config::rap_type::rap_array:
                printArray(std::get<std::vector<Config::expression>>(it.value));
                output << ", ";
                break;
            default:
                errorf("Unknown array element type %i.\n", (int)it.type);
            }
        }
        output.seekp(-2, std::ostream::_Seekcur); //remove last ,
        output << "}";
    };

    std::function<void(std::vector<Config::definition>&)> printClass = [&](std::vector<Config::definition>& data) {
        for (auto& it : data) {
            switch (it.type) {
            case Config::rap_type::rap_class: {
                auto& c = std::get<Config::class_>(it.content);
                pushIndent();
                if (c.is_delete || c.is_definition) {
                    if (c.is_delete)
                        output << "delete " << c.name << ";\n";
                    else
                        output << "class " << c.name << ";\n";
                } else {
                    output << "class " << c.name;
                    if (!c.parent.empty())
                        output << ": " << c.parent << " {\n";
                    else
                        output << " {\n";
                    indentLevel++;
                    printClass(c.content);
                    indentLevel--;
                    pushIndent();
                    output << "};\n";
                    if (indentLevel == 0)
                        output << "\n"; //Seperate classes on root level. Just for fancyness
                }
            } break;
            case Config::rap_type::rap_var: {
                auto& var = std::get<Config::variable>(it.content);
                auto& exp = var.expression;
                pushIndent();
                switch (exp.type) {
                case Config::rap_type::rap_string:
                    output << var.name << " = \"" << std::get<std::string>(exp.value) << "\";\n";
                    break;
                case Config::rap_type::rap_int:
                    output << var.name << " = " << std::get<int>(exp.value) << ";\n";
                    break;
                case Config::rap_type::rap_float:
                    output << var.name << " = " << std::get<float>(exp.value) << ";\n";
                    break;
                case Config::rap_type::rap_array:
                case Config::rap_type::rap_array_expansion:
                    output << var.name << (exp.type == Config::rap_type::rap_array_expansion ? "[] += ": "[] = ");
                    printArray(std::get<std::vector<Config::expression>>(exp.value));
                    output << ";\n";
                    break;
                }
            } break;
            default:
                errorf("Unknown class entry type %i.\n", (int)it.type);
            }
        };
    };
    printClass(getConfig().content);
}

void Rapifier::rapify_expression(Config::expression &expr, std::ostream &f_target) {

    if (expr.type == Config::rap_type::rap_array) {
        auto& elements = std::get<std::vector<Config::expression>>(expr.value);
        uint32_t num_entries = elements.size();

        write_compressed_int(num_entries, f_target);

        for (auto& exp : elements) {
            switch (exp.type) {
                case Config::rap_type::rap_string: f_target.put(0); break;
                case Config::rap_type::rap_float: f_target.put(1); break;
                case Config::rap_type::rap_int: f_target.put(2); break;
                case Config::rap_type::rap_array: f_target.put(3); break;
            }
            rapify_expression(exp, f_target);
        }
    } else if (expr.type == Config::rap_type::rap_int) {
        f_target.write(reinterpret_cast<char*>(&std::get<int>(expr.value)), 4);
    } else if (expr.type == Config::rap_type::rap_float) {
        f_target.write(reinterpret_cast<char*>(&std::get<float>(expr.value)), 4);
    } else {
        f_target.write(
            std::get<std::string>(expr.value).c_str(),
            std::get<std::string>(expr.value).length() + 1
        );
    }
}

void Rapifier::rapify_variable(Config::variable &var, std::ostream &f_target) {
    if (var.type == Config::rap_type::rap_var) {
        f_target.put(1);
        switch (var.expression.type) {
            case Config::rap_type::rap_string: f_target.put(0); break;
            case Config::rap_type::rap_float: f_target.put(1); break;
            case Config::rap_type::rap_int: f_target.put(2); break;
        }
    } else {
        switch (var.type) {
            case Config::rap_type::rap_array: f_target.put(2); break;
            case Config::rap_type::rap_array_expansion: 
                f_target.put(5);
                f_target.write("\x01\0\0\0", 4);//last 3 bytes are unused by engine
                break;
        }
    }

    f_target.write(var.name.c_str(), var.name.length() + 1);
    rapify_expression(var.expression, f_target);
}

void Rapifier::rapify_class(Config::class_ &class__, std::ostream &f_target) {
    uint32_t fp_temp;

    if (class__.content.empty()) {
        // extern or delete class
        f_target.put(static_cast<char>(class__.is_delete ? 4 : 3));
        f_target.write(class__.name.c_str(), class__.name.length() + 1);
        return;
    }
    
    if (!class__.parent.empty())
        f_target.write(class__.parent.c_str(), class__.parent.length() + 1);
    else
        f_target.put(0);

    uint32_t num_entries = class__.content.size();

    write_compressed_int(num_entries, f_target);
    for (auto& def : class__.content) {
        if (def.type == Config::rap_type::rap_var) {
            auto& c = std::get<Config::variable>(def.content);
            rapify_variable(c, f_target);
        } else {
            auto& c = std::get<Config::class_>(def.content);
            if (!c.content.empty()) {
                f_target.put(0);
                f_target.write(c.name.c_str(),
                    c.name.length() + 1);
                c.offset_location = f_target.tellp();
                f_target.write("\0\0\0\0", 4);
            } else {
                rapify_class(c, f_target);
            }
        }
    }
    //#TODO mikero writes a filesize marker here. 4 bytes
    //Arma won't read cuz num_entries

    for (auto& def : class__.content) {
        if (def.type != Config::rap_type::rap_class) continue;
        auto& c = std::get<Config::class_>(def.content);
        if (c.content.empty())  continue;
        
        fp_temp = f_target.tellp();
        f_target.seekp(c.offset_location);
        
        f_target.write(reinterpret_cast<char*>(&fp_temp), sizeof(uint32_t));
        f_target.seekp(0, std::ofstream::end);

        rapify_class(c, f_target);
    }
}

bool Rapifier::isRapified(std::istream& input) {
    auto sourcePos = input.tellg();
    char buffer[4];
    input.read(buffer, 4);
    input.seekg(sourcePos);
    return strncmp(buffer, "\0raP", 4) == 0;
}

int Rapifier::rapify_file(const char* source, const char* target) {

    std::ifstream sourceFile(source, std::ifstream::in | std::ifstream::binary);

    if (strcmp(target, "-") == 0) {
        return rapify_file(sourceFile, std::cout, source);
    }
    else {
        std::ofstream targetFile(target, std::ofstream::out | std::ofstream::binary);
        return rapify_file(sourceFile, targetFile, source);
    }


}

int Rapifier::rapify_file(std::istream &source, std::ostream &target, const char* sourceFileName) {
    /*
     * Resolves macros/includes and rapifies the given file. If source and
     * target are identical, the target is overwritten.
     *
     * Returns 0 on success and a positive integer on failure.
     */
    current_target = sourceFileName;

    // Check if the file is already rapified
    if (isRapified(source)) {
        std::copy(std::istreambuf_iterator<char>(source),
            std::istreambuf_iterator<char>(),
            std::ostream_iterator<char>(target));
        return 0;
    }
    Preprocessor preproc;

    std::list<constant> constants;
    std::stringstream fileToPreprocess;
    int success = preproc.preprocess(sourceFileName, source, fileToPreprocess, constants);

    current_target = sourceFileName;

    if (success) {
        errorf("Failed to preprocess %s.\n", sourceFileName);
        return success;
    }

#if 0
    char dump_name[2048];
    sprintf(dump_name, "armake_preprocessed_%u.dump", (unsigned)time(NULL));

    std::copy(std::istreambuf_iterator<char>(fileToPreprocess),
        std::istreambuf_iterator<char>(),
        std::ostream_iterator<char>(std::ofstream(dump_name)));
#endif

    auto parsedConfig = Config::fromPreprocessedText(fileToPreprocess, preproc.getLineref());

    if (!parsedConfig.hasConfig()) {
        errorf("Failed to parse config.\n");
        return 1;
    }

    rapify(parsedConfig.getConfig(), target);

    return 0;
}

int Rapifier::rapify(Config::class_& cls, std::ostream& output) {
    
    uint32_t enum_offset = 0;
    output.write("\0raP", 4);
    output.write("\0\0\0\0\x08\0\0\0", 8);
    output.write(reinterpret_cast<char*>(&enum_offset), 4); // this is replaced later

    rapify_class(cls, output);

    enum_offset = output.tellp();
    output.write("\0\0\0\0", 4); // fuck enums
    output.seekp(12);
    output.write(reinterpret_cast<char*>(&enum_offset), 4);
    return 0; //#TODO this can't fail...
}
