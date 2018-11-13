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


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <filesystem>
//#include <unistd.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <errno.h>
#include <fts.h>
#endif

#include "args.h"
#include "filesystem.h"
#include "utils.h"
#include "preprocess.h"
#include <fstream>
#include <execution>
#include <variant>


#define IS_MACRO_CHAR(x) ( (x) == '_' || \
    ((x) >= 'a' && (x) <= 'z') || \
    ((x) >= 'A' && (x) <= 'Z') || \
    ((x) >= '0' && (x) <= '9') )

#if __APPLE__
char *strchrnul(const char *s, int c) {
    char *first = strchr(s, c);
    if (first != NULL)
        return first;

    return s + strlen(s);
}
#endif

bool Preprocessor::constants_parse(std::list<constant> &constants, std::string_view  definition, int line) {
    
    char *argstr;
    char *tok;
    char **args;
    int i;
    int len;
    bool quoted;


    //Should be able to replace with by looping substr. Compiler should optimize it down
    const char *ptr = definition.data();
    while (IS_MACRO_CHAR(*ptr))
        ptr++;

    std::string_view name = std::string_view(definition.data(), ptr - definition.data());



    auto found = std::find_if(constants.begin(), constants.end(), [name](const constant& cnst) {
        return cnst.name == name;
    });
    if (found != constants.end()) {
        constants.erase(found);
        lnwarningf(current_target, line, "redefinition-wo-undef",
            "Constant \"%s\" is being redefined without an #undef.\n", name.data());
    }

    auto& c = constants.emplace_back();
    c.name = name;

    c.num_args = 0;
    if (*ptr == '(') {
        argstr = safe_strdup(ptr + 1);
        if (strchr(argstr, ')') == NULL) {
            lerrorf(current_target, line,
                    "Missing ) in argument list of \"%s\".\n", c.name);
            return false;
        }
        *strchr(argstr, ')') = 0;
        ptr += strlen(argstr) + 2;

        args = (char **)safe_malloc(sizeof(char *) * 4);

        tok = strtok(argstr, ",");
        while (tok) {
            if (c.num_args % 4 == 0)
                args = (char **)safe_realloc(args, sizeof(char *) * (c.num_args + 4));
            args[c.num_args] = safe_strdup(tok);
            trim(args[c.num_args], strlen(args[c.num_args]) + 1);
            c.num_args++;
            tok = strtok(NULL, ",");
        }

        free(argstr);
    }

    while (*ptr == ' ' || *ptr == '\t')
        ptr++;

    c.num_occurences = 0;
    if (c.num_args > 0) {
        c.value = safe_strdup("");
        len = 0;

        while (true) {
            quoted = false;

            // Non-tokens
            const char* start = ptr;
            while (*ptr != 0 && !IS_MACRO_CHAR(*ptr) && *ptr != '#')
                ptr++;

            while (len == 0 && (*start == ' ' || *start == '\t'))
                start++;

            if (ptr - start > 0) {
                len += ptr - start;
                c.value += std::string_view(start, ptr - start);
            }

            quoted = *ptr == '#';
            if (quoted) {
                ptr++;

                if (*ptr == '#') {
                    lnwarningf(current_target, line, "excessive-concatenation",
                            "Leading token concatenation operators (##) are not necessary.\n");
                    quoted = false;
                    ptr++;
                }

                while (*ptr == '#' && *(ptr + 1) == '#')
                    ptr += 2;

                if (*ptr == '#') {
                    lerrorf(current_target, line,
                            "Token concatenations cannot be stringized.\n");
                    return false;
                }
            }

            if (*ptr == 0)
                break;

            // Potential tokens
            start = ptr;
            while (IS_MACRO_CHAR(*ptr))
                ptr++;

            tok = safe_strndup(start, ptr - start);
            for (i = 0; i < c.num_args; i++) {
                if (strcmp(tok, args[i]) == 0)
                    break;
            }

            if (i == c.num_args) {
                if (quoted) {
                    lerrorf(current_target, line, "Stringizing is only allowed for arguments.\n");
                    return false;
                }
                len += ptr - start;
                c.value += tok;
            } else {
                size_t length = 0;

                if (quoted) {
                    length = len + 1;
                    len += 2;
                    c.value+= "\"\"";
                } else {
                    length = len;
                }
                c.occurrences.emplace_back(i, length);

                c.num_occurences++;
            }

            free(tok);

            // Handle concatenation
            while (*ptr == '#' && *(ptr + 1) == '#') {
                if (quoted) {
                    lerrorf(current_target, line,
                            "Token concatenations cannot be stringized.\n");
                    return false;
                }

                ptr += 2;
                if (!IS_MACRO_CHAR(*ptr))
                    lnwarningf(current_target, line, "excessive-concatenation",
                            "Trailing token concatenation operators (##) are not necessary.\n");
            }
        }

        ptr = c.value.data() + (c.value.length() - 1);
        //std::string::size_type end = c.value.find_last_not_of("\t ");
        //if (c.value != c.value.substr(0, end + 1))
        //    __debugbreak(); //Did it correctly trim and not cut anything off?
        //c.value = c.value.substr(0, end + 1);

        while ((c.num_occurences == 0 || c.occurrences[c.num_occurences - 1].second < (ptr - c.value.data())) &&
                ptr > c.value.data() && (*ptr == ' ' || *ptr == '\t'))
            ptr--;

        if (c.value != c.value.substr(0, ptr - c.value.data() + 1))
            __debugbreak();

        c.value = c.value.substr(0, ptr - c.value.data() + 1);
    } else {
        c.occurrences.clear();
        c.value = safe_strdup(ptr);

        //trim tabs and spaces on both sides
        if (!c.value.empty()) {
            std::string::size_type begin = c.value.find_first_not_of("\t ");
            std::string::size_type end = c.value.find_last_not_of("\t ");
            c.value = c.value.substr(begin, end - begin + 1);
        }
    }

    if (c.num_args > 0) {
        for (i = 0; i < c.num_args; i++)
            free(args[i]);
        free(args);
    }

    return true;
}

std::optional<std::string> Preprocessor::constants_preprocess(const std::list<constant> &constants, std::string_view source, int line, constant_stack &constant_stack) {
    const char *ptr = source.data();
    const char *start;
    int level;
    char in_string;

    struct constToProcess {
        std::list<constant>::const_iterator constant;
        std::vector<std::string> args;
        std::string processed;
    };

    std::vector<std::variant<std::string_view, constToProcess>> result;

    auto processConstants = [&line, &constant_stack, &result, &constants]() {
        
        std::for_each(std::execution::par_unseq, result.begin(), result.end(), [&](auto& element) {
            if (element.index() == 0) return; //skip stringviews

            auto& cnst = std::get<constToProcess>(element);

            auto substack = constant_stack;
            auto value = constant_value(constants, cnst.constant, cnst.args.size(), cnst.args, line, substack);
            if (!value)
                return; //#TODO exception
            cnst.processed = *value;
        });

        auto finalSize = std::transform_reduce(std::execution::par_unseq, result.begin(), result.end(), static_cast<size_t>(0u),
            [](size_t l, size_t r) {return l + r; },
            [](const std::variant<std::string_view, constToProcess>& var) {

            if (var.index() == 0) return std::get<std::string_view>(var).length();
            return std::get<constToProcess>(var).processed.length();
        });
        std::string res;
        res.reserve(finalSize);

        for (auto& it : result) {
            if (it.index() == 0)
                res += std::get<std::string_view>(it);
            else
                res += std::get<constToProcess>(it).processed;
        }
        return res;
    };


    while (true) {
        // Non-tokens
        start = ptr;
        while (*ptr != 0 && !IS_MACRO_CHAR(*ptr))
            ptr++;

        if (ptr - start > 0) {
            result.emplace_back(std::string_view(start, ptr - start));
        }

        if (*ptr == 0)
            break;

        // Potential tokens
        start = ptr;
        while (IS_MACRO_CHAR(*ptr))
            ptr++;


        auto c = std::find_if(constants.begin(), constants.end(), [name = std::string_view(start, ptr - start)](const constant& cnst) {
            return cnst.name == name;
        });

        if (c == constants.end() || (c->num_args > 0 && *ptr != '(')) {
            result.emplace_back(std::string_view(start, ptr - start));
            continue;
        }

        // prevent infinite loop
        if (std::find(constant_stack.stack.begin(), constant_stack.stack.end(), c) != constant_stack.stack.end())
            continue;

        constToProcess curConst;
        auto& args = curConst.args;
        curConst.constant = c;
        if (*ptr == '(') {
            ptr++;
            start = ptr;

            in_string = 0;
            level = 0;
            while (*ptr != 0) {
                if (in_string) {
                    if (*ptr == in_string)
                        in_string = 0;
                } else if ((*ptr == '"' || *ptr == '\'') && *(ptr - 1) != '\\') {
                    in_string = *ptr;
                } else if (*ptr == '(') {
                    level++;
                } else if (level > 0 && *ptr == ')') {
                    level--;
                } else if (level == 0 && (*ptr == ',' || *ptr == ')')) {

                    args.emplace_back(start, ptr - start);
                    if (*ptr == ')') {
                        break;
                    }
                    start = ptr + 1;
                }
                ptr++;
            }

            if (*ptr == 0) {
                lerrorf(current_target, line,
                        "Incomplete argument list for macro \"%s\".\n", c->name);
                return {};
            } else {
                ptr++;
            }
        }

        //auto value = constant_value(constants, c, num_args, args, line, constant_stack);
        //if (!value)
        //    return {};


        result.emplace_back(std::move(curConst));
    }

    return processConstants();
}

std::optional<std::string> Preprocessor::constant_value(const std::list<constant> &constants, std::list<constant>::const_iterator constant,
        int num_args, std::vector<std::string>& args, int line, constant_stack & constant_stack) {
    int i;
    char *tmp;

    if (num_args != constant->num_args) {
        if (num_args)
            lerrorf(current_target, line,
                    "Macro \"%s\" expects %i arguments, %i given.\n", constant->name, constant->num_args, num_args);
        return {};
    }

    for (i = 0; i < num_args; i++) {
        auto ret = constants_preprocess(constants, args[i], line, constant_stack);
        if (!ret)
            return {};
        args[i] = trim(*ret); //trim tabs and spaces from both ends
    }


    std::string result;
    if (num_args == 0) {
        result = constant->value;
    } else {
        result = "";
        const char *ptr = constant->value.data();
        for (i = 0; i < constant->num_occurences; i++) {
            result += std::string_view(ptr, constant->occurrences[i].second - (ptr - constant->value.data()));
            result += args[constant->occurrences[i].first];
            ptr = constant->value.data() + constant->occurrences[i].second;
        }
        result += ptr;
    }

    constant_stack.stack.push_back(constant);

    auto res = constants_preprocess(constants, result, line, constant_stack);

    constant_stack.stack.pop_back();

    if (res && !res->empty()) {
        //trim tabs and spaces from both ends
        return trim(*res);
    }

    return res;
}

bool matches_includepath(const char *path, const char *includepath, const char *includefolder) {
    /*
     * Checks if a given file can be matched to an include path by traversing
     * backwards through the filesystem until a $PBOPREFIX$ file is found.
     * If the prefix file, together with the diretory strucure, matches the
     * included path, true is returned.
     */

    int i;
    char cwd[2048];
    char prefixpath[2048];
    char prefixedpath[2048];
    char *ptr;
    FILE *f_prefix;

    strncpy(cwd, path, 2048);
    ptr = cwd + strlen(cwd);

    while (strcmp(includefolder, cwd) != 0) {
        while (*ptr != PATHSEP)
            ptr--;
        *ptr = 0;

        strncpy(prefixpath, cwd, 2048);
        strcat(prefixpath, PATHSEP_STR);
        strcat(prefixpath, "$PBOPREFIX$");

        f_prefix = fopen(prefixpath, "rb");
        if (!f_prefix)
            continue;

        fgets(prefixedpath, sizeof(prefixedpath), f_prefix);
        fclose(f_prefix);

        if (prefixedpath[strlen(prefixedpath) - 1] == '\n')
            prefixedpath[strlen(prefixedpath) - 1] = 0;
        if (prefixedpath[strlen(prefixedpath) - 1] == '\r')
            prefixedpath[strlen(prefixedpath) - 1] = 0;
        if (prefixedpath[strlen(prefixedpath) - 1] == '\\')
            prefixedpath[strlen(prefixedpath) - 1] = 0;

        strcat(prefixedpath, path + strlen(cwd));

        for (i = 0; i < strlen(prefixedpath); i++) {
            if (prefixedpath[i] == '/')
                prefixedpath[i] = '\\';
        }

        // compensate for missing leading slash in PBOPREFIX
        if (prefixedpath[0] != '\\')
            return (strcmp(prefixedpath, includepath+1) == 0);
        else
            return (strcmp(prefixedpath, includepath) == 0);
    }

    return false;
}


int find_file_helper(const char *includepath, const char *origin, char *includefolder, char *actualpath, const char *cwd) {
    /*
     * Finds the file referenced in includepath in the includefolder. origin
     * describes the file in which the include is used (used for relative
     * includes). actualpath holds the return pointer. The 4th arg is used for
     * recursion on Windows and should be passed as NULL initially.
     *
     * Returns 0 on success, 1 on error and 2 if no file could be found.
     *
     * Please note that relative includes always return a path, even if that
     * file does not exist.
     */

    // relative include, this shit is easy
    if (includepath[0] != '\\') {
        strncpy(actualpath, origin, 2048);
        char *target = actualpath + strlen(actualpath) - 1;
        while (*target != PATHSEP && target >= actualpath)
            target--;
        strncpy(target + 1, includepath, 2046 - (target - actualpath));

#ifndef _WIN32
        int i;
        for (i = 0; i < strlen(actualpath); i++) {
            if (actualpath[i] == '\\')
                actualpath[i] = '/';
        }
#endif

        return 0;
    }

    char filename[2048];
    const char *ptr = includepath + strlen(includepath);

    while (*ptr != '\\')
        ptr--;
    ptr++;

    strncpy(filename, ptr, 2048);

#ifdef _WIN32
    if (cwd == NULL)
        return find_file_helper(includepath, origin, includefolder, actualpath, includefolder);

    WIN32_FIND_DATA file;
    HANDLE handle = NULL;
    char mask[2048];

    GetFullPathName(includefolder, 2048, includefolder, NULL);

    GetFullPathName(cwd, 2048, mask, NULL);
    sprintf(mask, "%s\\*", mask);

    handle = FindFirstFile(mask, &file);
    if (handle == INVALID_HANDLE_VALUE)
        return 1;

    do {
        if (strcmp(file.cFileName, ".") == 0 || strcmp(file.cFileName, "..") == 0)
            continue;

        if (strcmp(file.cFileName, ".git") == 0)
            continue;

        GetFullPathName(cwd, 2048, mask, NULL);
        sprintf(mask, "%s\\%s", mask, file.cFileName);
        if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!find_file_helper(includepath, origin, includefolder, actualpath, mask)) {
                FindClose(handle);
                return 0;
            }
        } else {
            if (strcmp(filename, file.cFileName) == 0 && matches_includepath(mask, includepath, includefolder)) {
                strncpy(actualpath, mask, 2048);
                FindClose(handle);
                return 0;
            }
        }
    } while (FindNextFile(handle, &file));

    FindClose(handle);
#else
    FTS *tree;
    FTSENT *f;
    char *argv[] = { includefolder, NULL };

    tree = fts_open(argv, FTS_LOGICAL | FTS_NOSTAT, NULL);
    if (tree == NULL)
        return 1;

    while ((f = fts_read(tree))) {
        if (!strcmp(f->fts_name, ".git"))
            fts_set(tree, f, FTS_SKIP);

        switch (f->fts_info) {
            case FTS_DNR:
            case FTS_ERR:
                fts_close(tree);
                return 2;
            case FTS_NS: continue;
            case FTS_DP: continue;
            case FTS_D: continue;
            case FTS_DC: continue;
        }

        if (strcmp(filename, f->fts_name) == 0 && matches_includepath(f->fts_path, includepath, includefolder)) {
            strncpy(actualpath, f->fts_path, 2048);
            fts_close(tree);
            return 0;
        }
    }

    fts_close(tree);
#endif

    // check for file without pboprefix
    strncpy(filename, includefolder, sizeof(filename));
    strncat(filename, includepath, sizeof(filename) - strlen(filename) - 1);
#ifndef _WIN32
    int i;
    for (i = 0; i < strlen(filename); i++) {
        if (filename[i] == '\\')
            filename[i] = '/';
    }
#endif
    if (std::filesystem::exists(filename)) {
        strcpy(actualpath, filename);
        return 0;
    }

    return 2;
}


int find_file(const char *includepath, const char *origin, char *actualpath) {
    /*
     * Finds the file referenced in includepath in the includefolder. origin
     * describes the file in which the include is used (used for relative
     * includes). actualpath holds the return pointer. The 4th arg is used for
     * recursion on Windows and should be passed as NULL initially.
     *
     * Returns 0 on success, 1 on error and 2 if no file could be found.
     *
     * Please note that relative includes always return a path, even if that
     * file does not exist.
     */

    extern struct arguments args;
    int i;
    int success;
    char *temp = (char *)malloc(2048);

    for (i = 0; i < args.num_includefolders; i++) {
        strcpy(temp, args.includefolders[i]);
        success = find_file_helper(includepath, origin, temp, actualpath, NULL);

        if (success != 2) {
            free(temp);
            return success;
        }
    }

    free(temp);

    return 2;
}

//To make reverse iterating the include_stack easier
template <typename T>
struct reversion_wrapper { T& iterable; };

template <typename T>
auto begin(reversion_wrapper<T> w) { return std::rbegin(w.iterable); }

template <typename T>
auto end(reversion_wrapper<T> w) { return std::rend(w.iterable); }

template <typename T>
reversion_wrapper<T> reverse(T&& iterable) { return { iterable }; }


int Preprocessor::preprocess(char *source, std::ostream &f_target, std::list<constant> &constants) {
    /*
     * Writes the contents of source into the target file pointer, while
     * recursively resolving constants and includes using the includefolder
     * for finding included files.
     *
     * Returns 0 on success, a positive integer on failure.
     */

    std::ifstream f_source(source, std::ifstream::in | std::ifstream::binary);

    if (!f_source.is_open() || f_source.fail()) {
        errorf("Failed to open %s.\n", source);
        return 1;
    }

    return preprocess(source, f_source, f_target, constants);
}


int Preprocessor::preprocess(const char* sourceFileName, std::istream &input, std::ostream &output, std::list<constant> &constants) {
    int line = 0;
    int level = 0;
    int level_true = 0;
    int level_comment = 0;

    current_target = sourceFileName;

    if (std::find(include_stack.begin(), include_stack.end(), std::string(sourceFileName)) != include_stack.end()) {

        errorf("Circular dependency detected, printing include stack:\n", sourceFileName);
        fprintf(stderr, "    !!! %s\n", sourceFileName);
        for (auto& it : reverse(include_stack)) {
            fprintf(stderr, "        %s\n", it.c_str()); //#TODO don't print to stderr. Make a global config thingy that contains the error stream (might be file or even network)
        }
        return 1;
    }

    include_stack.emplace_back(sourceFileName);

    // Skip byte order mark if it exists
    if (input.peek() == 0x3f)
        input.seekg(3, std::ifstream::beg);

    int file_index = lineref.file_names.size();
    if (strchr(sourceFileName, PATHSEP) == NULL)
        lineref.file_names.emplace_back(sourceFileName);
    else
        lineref.file_names.emplace_back(strrchr(sourceFileName, PATHSEP) + 1);



    // first constant is file name
    // @todo
    // strcpy(constants[0].name, "__FILE__");
    // if (constants[0].value == 0)
    //     constants[0].value = (char *)safe_malloc(1024);
    // snprintf(constants[0].value, 1024, "\"%s\"", source);

    // strcpy(constants[1].name, "__LINE__");

    // strcpy(constants[2].name, "__EXEC");
    // if (constants[2].value == 0)
    //     constants[2].value = (char *)safe_malloc(1);

    // strcpy(constants[3].name, "__EVAL");
    // if (constants[3].value == 0)
    //     constants[3].value = (char *)safe_malloc(1);

    std::vector<std::tuple<std::string, bool, int>> linesToProcess;




    auto processLine = [&](const std::string& curLine, int line) -> std::string {
        constant_stack c;
        std::optional<std::string> preprocessedLine = constants_preprocess(constants, curLine, line, c);
        if (!preprocessedLine) {
            lerrorf(sourceFileName, line, "Failed to resolve macros.\n");
            //return 1; 
            //#TODO exceptions
        }
        return *preprocessedLine;
    };



    auto processLines = [&]() {
        std::vector<std::string> ret;
        ret.resize(linesToProcess.size());

        std::transform(std::execution::par_unseq, linesToProcess.begin(),linesToProcess.end(), ret.begin(),[&](std::tuple<std::string, bool, int>& element) {
            auto&[str, shouldPreproc, line] = element;
            if (!shouldPreproc)
                return std::move(str);
            else
                return processLine(str, line);
        });

        for (auto& it : ret)
            output.write(it.c_str(), it.length());

        linesToProcess.clear();

    };




    while (true) {
        // get line and add next lines if line ends with a backslash
        std::string curLine;
        std::getline(input, curLine);
        if (curLine.empty() && input.eof()) break;
        // fix windows line endings
        if (!curLine.empty() && curLine.back() == '\r')
            curLine.pop_back();

        line++;
        lineref.file_index.push_back(file_index);
        lineref.line_number.push_back(line);

        if (curLine.empty()) continue;

        if (strncmp(curLine.c_str(), "TEST_SUCCESS", strlen("TEST_SUCCESS")) == 0)
            __debugbreak();

        while (curLine.back() == '\\') {
            curLine.pop_back(); //remove backslash
            if (curLine.back() != '\n')
                curLine += '\n';

            std::string nextLine;
            std::getline(input, nextLine);
            // fix windows line endings
            if (nextLine.back() == '\r')
                nextLine.pop_back(); //remove the \r so that we hit the backslash in the while condition
            
            line++;
            lineref.file_index.push_back(file_index);
            lineref.line_number.push_back(line);

            curLine += nextLine;
        }

        // Add trailing new line if necessary
        if (!curLine.empty() && curLine.back() != '\n')
            curLine += '\n';

        // Check for block comment delimiters
        char in_string = 0;
        for (int i = 0; i < curLine.length(); i++) {
            if (in_string != 0) {
                if (curLine[i] == in_string && curLine[i - 1] != '\\')
                    in_string = 0;
                else
                    continue;
            }
            else {
                if (level_comment == 0 &&
                    (curLine[i] == '"' || curLine[i] == '\'') &&
                    (i == 0 || curLine[i - 1] != '\\'))
                    in_string = curLine[i];
            }

            if (curLine[i] == '/' && curLine[i + 1] == '/' && level_comment == 0) {
                curLine.resize(i + 1);
                curLine[i] = '\n';
            }
            else if (curLine[i] == '/' && curLine[i + 1] == '*') {
                level_comment++;
                curLine[i] = ' ';
                curLine[i + 1] = ' ';
            }
            else if (curLine[i] == '*' && curLine[i + 1] == '/') {
                level_comment--;
                if (level_comment < 0)
                    level_comment = 0;
                curLine[i] = ' ';
                curLine[i + 1] = ' ';
            }

            if (level_comment > 0) {
                curLine[i] = ' '; //empty line
                continue;
            }
        }

        // trim leading spaces
        auto trimLeading = curLine.find_first_not_of(" \t");
        if (trimLeading == std::string::npos) {//This line is empty besides a couple spaces. Nothing to preprocess here.
            if (keepLineCount) {
                //output.write("\n", 1);
                linesToProcess.emplace_back(std::move(curLine), false, line);

                lineref.file_index.push_back(file_index);
                lineref.line_number.push_back(line);
            }
            continue;
        }

        if (trimLeading != 0)
            curLine = curLine.substr(trimLeading);

        // skip lines inside untrue ifs
        if (level > level_true) {
            if ((curLine.length() < 5 || strncmp(curLine.c_str(), "#else", 5) != 0) &&
                (curLine.length() < 6 || strncmp(curLine.c_str(), "#endif", 6) != 0)) {
                continue;
            }
        }

        // second constant is line number
        // @todo
        // if (constants[1].value == 0)
        //     constants[1].value = (char *)safe_malloc(16);
        // sprintf(constants[1].value, "%i", line - 1);

        if (level_comment == 0 && curLine[0] == '#') {
            processLines();
            const char *ptr = curLine.c_str() + 1;
            while (*ptr == ' ' || *ptr == '\t')
                ptr++;

            std::string_view inp(ptr);
            auto directive = std::string(inp.substr(0, inp.find_first_of(" \t\n")));
            std::string_view argUntrimmed = inp.substr(directive.length() + inp.find_first_not_of(" \t\n") + 1);
            auto directive_args = std::string(argUntrimmed.substr(0, argUntrimmed.find_last_not_of(" \t\n") + 1));

            if (directive == "include") {
                std::replace(directive_args.begin(), directive_args.end(), '<', '"');
                std::replace(directive_args.begin(), directive_args.end(), '>', '"');

                auto firstQuote = directive_args.find_first_of('"');
                if (firstQuote == std::string::npos) { //No quotes around path
                    lerrorf(sourceFileName, line, "Failed to parse #include.\n");
                    return 5;
                }
                auto lastQuote = directive_args.find_last_of('"');

                if (lastQuote == std::string::npos) {
                    lerrorf(sourceFileName, line, "Failed to parse #include.\n");
                    return 6;
                }

                directive_args = directive_args.substr(firstQuote + 1, lastQuote - firstQuote - 1);

                char actualpath[2048];
                if (find_file(directive_args.c_str(), sourceFileName, actualpath)) {
                    lerrorf(sourceFileName, line, "Failed to find %s.\n", directive_args.c_str());
                    return 7;
                }

                std::ifstream includefile(actualpath);

                if (!includefile.is_open() || includefile.fail()) {
                    errorf("Failed to open %s.\n", actualpath);
                    return 1;
                }

                int success = preprocess(actualpath, includefile, output, constants);

                include_stack.pop_back();

                current_target = sourceFileName;

                if (success)
                    return success;
            } else if (directive == "define") { //#TODO directive string to enum
                if (!constants_parse(constants, directive_args, line)) {
                    lerrorf(sourceFileName, line, "Failed to parse macro definition.\n");
                    return 3;
                }
            } else if (directive == "undef") {

                auto found = std::find_if(constants.begin(), constants.end(), [directive_args](const constant& cnst) {
                    return cnst.name == directive_args;
                });
                if (found != constants.end())
                    constants.erase(found);

            } else if (directive == "ifdef") {
                level++;

                auto found = std::find_if(constants.begin(), constants.end(), [directive_args](const constant& cnst) {
                    return cnst.name == directive_args;
                });
                if (found != constants.end())
                    level_true++;
            } else if (directive == "ifndef") {
                level++;
                auto found = std::find_if(constants.begin(), constants.end(), [directive_args](const constant& cnst) {
                    return cnst.name == directive_args;
                });
                if (found == constants.end())
                    level_true++;
            } else if (directive == "else") {
                if (level == level_true)
                    level_true--;
                else
                    level_true = level;
            } else if (directive == "endif") {
                if (level == 0) {
                    lerrorf(sourceFileName, line, "Unexpected #endif.\n");
                    return 4;
                }
                if (level == level_true)
                    level_true--;
                level--;
            } else {
                lerrorf(sourceFileName, line, "Unknown preprocessor directive \"%s\".\n", directive);
                return 5;
            }
        } else if (curLine.length() > 1) {
            linesToProcess.emplace_back(std::move(curLine), true, line);
        }
    }
    processLines();
    return 0;
}
