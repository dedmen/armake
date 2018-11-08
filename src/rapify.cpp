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
#include <sstream>
//#include "rapify.tab.h"

struct class_ new_class(std::string name, std::string parent, std::vector<definition> content, bool is_delete) {
    struct class_ result;

    result.name = name;
    result.parent = parent;
    result.is_delete = is_delete;
    result.content = std::move(content);

    return result;
}


struct variable new_variable(rap_type type, std::string name, struct expression expression) {
    struct variable result;

    result.type = type;
    if (type == rap_type::rap_array_expansion) __debugbreak();
    result.name = std::move(name);
    result.expression = std::move(expression);

    return result;
}


expression new_expression(rap_type type, void *value) {
    struct expression result;

    result.type = type;
    if (type == rap_type::rap_int) {
        result.value = *static_cast<int *>(value);
    } else if (type == rap_type::rap_float) {
        result.value = *static_cast<float *>(value);
    } else if (type == rap_type::rap_string) {
        result.value = *static_cast<std::string*>(value);
    } else if (type == rap_type::rap_array || type == rap_type::rap_array_expansion) {
        auto vec = std::vector<expression>();
        if (value) //might be empty array
            vec.emplace_back(*static_cast<struct expression *>(value));
        result.value = std::move(vec);
    }

    return result;
}

void rapify_expression(struct expression &expr, FILE *f_target) {
    uint32_t num_entries;

    if (expr.type == rap_type::rap_array) {
        auto& elements = std::get<std::vector<expression>>(expr.value);
        num_entries = elements.size();

        write_compressed_int(num_entries, f_target);

        for (auto& exp : elements) {
            fputc((char)((exp.type == rap_type::rap_string) ? 0 :
                ((exp.type == rap_type::rap_float) ? 1 :
                ((exp.type == rap_type::rap_int) ? 2 : 3))), f_target);
            rapify_expression(exp, f_target);
        }
    } else if (expr.type == rap_type::rap_int) {
        fwrite(&std::get<int>(expr.value), 4, 1, f_target);
    } else if (expr.type == rap_type::rap_float) {
        fwrite(&std::get<float>(expr.value), 4, 1, f_target);
    } else {
        fwrite(std::get<std::string>(expr.value).c_str(), std::get<std::string>(expr.value).length() + 1, 1, f_target);
    }
}


void rapify_variable(struct variable &var, FILE *f_target) {
    if (var.type == rap_type::rap_var) {
        fputc(1, f_target);
        fputc((char)((var.expression.type == rap_type::rap_string) ? 0 : ((var.expression.type == rap_type::rap_float) ? 1 : 2 )), f_target);
    } else {
        fputc((char)((var.type == rap_type::rap_array) ? 2 : 5), f_target);
        if (var.type == rap_type::rap_array_expansion) {
            fwrite("\x01\0\0\0", 4, 1, f_target);
        }
    }

    fwrite(var.name.c_str(), var.name.length() + 1, 1, f_target);
    rapify_expression(var.expression, f_target);
}


void rapify_class(struct class_ &class__, FILE *f_target) {
    uint32_t fp_temp;

    if (class__.content.empty()) {
        // extern or delete class
        fputc((char)(class__.is_delete ? 4 : 3), f_target);
        fwrite(class__.name.c_str(), class__.name.length() + 1, 1, f_target);
        return;
    }
    
    if (!class__.parent.empty())
        fwrite(class__.parent.c_str(), class__.parent.length() + 1, 1, f_target);
    else
        fputc(0, f_target);

    uint32_t num_entries = class__.content.size();

    write_compressed_int(num_entries, f_target);
    for (auto& def : class__.content) {
        if (def.type == rap_type::rap_var) {
            auto& c = std::get<variable>(def.content);
            rapify_variable(c, f_target);
        } else {
            auto& c = std::get<class_>(def.content);
            if (!c.content.empty()) {
                fputc(0, f_target);
                fwrite(c.name.c_str(),
                    c.name.length() + 1, 1, f_target);
                c.offset_location = ftell(f_target);
                fwrite("\0\0\0\0", 4, 1, f_target);
            } else {
                rapify_class(c, f_target);
            }
        }
    }

    for (auto& def : class__.content) {
        if (def.type != rap_type::rap_class) continue;
        auto& c = std::get<class_>(def.content);
        if (c.content.empty())  continue;
        
        fp_temp = ftell(f_target);
        fseek(f_target, c.offset_location, SEEK_SET);
        fwrite(&fp_temp, sizeof(uint32_t), 1, f_target);
        fseek(f_target, 0, SEEK_END);

        rapify_class(c, f_target);
    }
}

int rapify_file(char *source, char *target) {
    /*
     * Resolves macros/includes and rapifies the given file. If source and
     * target are identical, the target is overwritten.
     *
     * Returns 0 on success and a positive integer on failure.
     */

    extern const char *current_target;
    FILE *f_target;
    int i;
    int datasize;
    int success;
    char buffer[4096];
    uint32_t enum_offset = 0;

    current_target = source;

    // Check if the file is already rapified
    FILE *f_temp;
    f_temp = fopen(source, "rb");
    if (!f_temp) {
        errorf("Failed to open %s.\n", source);
        return 1;
    }

    fread(buffer, 4, 1, f_temp);
    if (strncmp(buffer, "\0raP", 4) == 0) {
        if ((strcmp(source, target)) == 0) {
            fclose(f_temp);
            return 0;
        }

        if (strcmp(target, "-") == 0) {
            f_target = stdout;
        } else {
            f_target = fopen(target, "wb");
            if (!f_target) {
                errorf("Failed to open %s.\n", target);
                fclose(f_temp);
                return 2;
            }
        }

        fseek(f_temp, 0, SEEK_END);
        datasize = ftell(f_temp);

        fseek(f_temp, 0, SEEK_SET);
        for (i = 0; datasize - i >= sizeof(buffer); i += sizeof(buffer)) {
            fread(buffer, sizeof(buffer), 1, f_temp);
            fwrite(buffer, sizeof(buffer), 1, f_target);
        }
        fread(buffer, datasize - i, 1, f_temp);
        fwrite(buffer, datasize - i, 1, f_target);

        fclose(f_temp);
        if (strcmp(target, "-") != 0)
            fclose(f_target);

        return 0;
    } else {
        fclose(f_temp);
    }

    Preprocessor preproc;

    std::list<constant> constants;
    std::stringstream fileToPreprocess;
    success = preproc.preprocess(source, fileToPreprocess, constants);

    current_target = source;

    if (success) {
        errorf("Failed to preprocess %s.\n", source);
        return success;
    }

#if 0
    FILE *f_dump;

    char dump_name[2048];
    sprintf(dump_name, "armake_preprocessed_%u.dump", (unsigned)time(NULL));
    printf("Done with preprocessing, dumping preprocessed config to %s.\n", dump_name);

    f_dump = fopen(dump_name, "wb");
    fseek(f_temp, 0, SEEK_END);
    datasize = ftell(f_temp);

    fseek(f_temp, 0, SEEK_SET);
    for (i = 0; datasize - i >= sizeof(buffer); i += sizeof(buffer)) {
        fread(buffer, sizeof(buffer), 1, f_temp);
        fwrite(buffer, sizeof(buffer), 1, f_dump);
    }

    fread(buffer, datasize - i, 1, f_temp);
    fwrite(buffer, datasize - i, 1, f_dump);

    fclose(f_dump);
#endif

    fileToPreprocess.seekg(0);
    auto result = parse_file(fileToPreprocess, preproc.getLineref());

    if (!result) {
        errorf("Failed to parse config.\n");
        return 1;
    }

#ifdef _WIN32
    char temp_name2[2048];
#endif

    // Rapify file
    if (strcmp(target, "-") == 0) {
#ifdef _WIN32
        if (!GetTempFileName(".", "amk", 0, temp_name2)) {
            errorf("Failed to get temp file name (system error %i).\n", GetLastError());
            return 1;
        }
        f_target = fopen(temp_name2, "wb+");
#else
        f_target = tmpfile();
#endif

        if (!f_target) {
            errorf("Failed to open temp file.\n");
#ifdef _WIN32
            DeleteFile(temp_name2);
#endif
            return 1;
        }
    } else {
        f_target = fopen(target, "wb+");
        if (!f_target) {
            errorf("Failed to open %s.\n", target);
            fclose(f_temp);
            return 2;
        }
    }
    fwrite("\0raP", 4, 1, f_target);
    fwrite("\0\0\0\0\x08\0\0\0", 8, 1, f_target);
    fwrite(&enum_offset, 4, 1, f_target); // this is replaced later

    rapify_class(*result, f_target);

    enum_offset = ftell(f_target);
    fwrite("\0\0\0\0", 4, 1, f_target); // fuck enums
    fseek(f_target, 12, SEEK_SET);
    fwrite(&enum_offset, 4, 1, f_target);

    if (strcmp(target, "-") == 0) {
        fseek(f_target, 0, SEEK_END);
        datasize = ftell(f_target);

        fseek(f_target, 0, SEEK_SET);
        for (i = 0; datasize - i >= sizeof(buffer); i += sizeof(buffer)) {
            fread(buffer, sizeof(buffer), 1, f_target);
            fwrite(buffer, sizeof(buffer), 1, stdout);
        }
        fread(buffer, datasize - i, 1, f_target);
        fwrite(buffer, datasize - i, 1, stdout);
    }

    fclose(f_temp);
    fclose(f_target);

#ifdef _WIN32
    if (strcmp(target, "-") == 0)
        DeleteFile(temp_name2);
#endif

    //free_class(result);

    return 0;
}
