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
//#include "rapify.tab.h"

bool parse_file(std::istream& f, struct lineref &lineref, Config::class_ &result);

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

void Config::toPlainText(std::ostream& output) {
    if(!hasConfig()) return;

    std::function<void(std::vector<Config::expression>&)> printArray = [&](std::vector<Config::expression>& data) {
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
                break;
            default:
                errorf("Unknown array element type %i.\n", (int)it.type);
            }

        };
        output.seekp(-2, std::ostream::_Seekcur); //remove last ,
        output << "}";
    };

    std::function<void(std::vector<Config::definition>&)> printClass = [&](std::vector<Config::definition>& data) {
        for (auto& it : data) {
            switch (it.type) {
            case Config::rap_type::rap_class: {
                auto& c = std::get<Config::class_>(it.content);

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
                        output << "{\n";
                    printClass(c.content);

                }
            } break;
            case Config::rap_type::rap_var: {
                auto& var = std::get<Config::variable>(it.content);
                auto& exp = var.expression;

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
                    output << var.name << (exp.type == Config::rap_type::rap_array_expansion) ? " += ": " = ";
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
    uint32_t num_entries;

    if (expr.type == Config::rap_type::rap_array) {
        auto& elements = std::get<std::vector<Config::expression>>(expr.value);
        num_entries = elements.size();

        write_compressed_int(num_entries, f_target);

        for (auto& exp : elements) {
            f_target.put(
                static_cast<char>((exp.type == Config::rap_type::rap_string)
                                      ? 0
                                      : ((exp.type == Config::rap_type::rap_float)
                                             ? 1
                                             : ((exp.type == Config::rap_type::rap_int) ? 2 : 3)))
            );
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
        f_target.put(static_cast<char>((var.expression.type == Config::rap_type::rap_string)
                                           ? 0
                                           : ((var.expression.type == Config::rap_type::rap_float) ? 1 : 2)));
    } else {
        f_target.put(static_cast<char>((var.type == Config::rap_type::rap_array) ? 2 : 5));
        if (var.type == Config::rap_type::rap_array_expansion) {
            f_target.write("\x01\0\0\0", 4);
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
        errorf("Failed to preprocess %s.\n", source);
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
