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
#include <filesystem>

#include "args.h"
#include "filesystem.h"
#include "utils.h"
#include "preprocess.h"
#include <fstream>
#include <execution>
#include <variant>

__itt_domain* preprocDomain = __itt_domain_create("armake.preproc");

//std::execution::seq std::execution::par_unseq
#define MULTITHREADED_EXC std::execution::par_unseq

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

bool Preprocessor::constants_parse(ConstantMapType &constants, std::string_view definition, int line) {
    
    char *argstr;
    char *tok;
    std::vector<std::string> args;
    int i;
    int len;
    bool quoted;


    //Should be able to replace with by looping substr. Compiler should optimize it down
    const char *ptr = definition.data();
    while (IS_MACRO_CHAR(*ptr))
        ptr++;

    std::string_view name = std::string_view(definition.data(), ptr - definition.data());



    auto found = constants.find(std::string(name)); //#TODO string_view comparions
    if (found != constants.end()) {
        constants.erase(found);
        logger.warning(current_file, line, LoggerMessageType::redefinition_wo_undef,
            "Constant \"%s\" is being redefined without an #undef.\n", name.data());
    }

    auto iter = constants.emplace(name, constant());
    constant& c = iter.first->second;

    //c.name = name; //#TODO remove this. Duplciate memory usage and not required for operation. Just here for debug
    c.num_args = 0;
    if (*ptr == '(') {
        argstr = safe_strdup(ptr + 1);
        if (strchr(argstr, ')') == NULL) {
            logger.error(current_file, line,
                    "Missing ) in argument list of \"%s\".\n", name);
            return false;
        }
        *strchr(argstr, ')') = 0;
        ptr += strlen(argstr) + 2;

        tok = strtok(argstr, ",");
        while (tok) {
            auto& ins = args.emplace_back(trim(std::string_view(tok)));
            c.num_args++;
            tok = strtok(NULL, ",");
        }

        free(argstr);
    }

    while (*ptr == ' ' || *ptr == '\t')
        ptr++;

    c.num_occurences = 0;
    if (c.num_args > 0) {
        c.value = "";
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
                    logger.warning(current_file, line, LoggerMessageType::excessive_concatenation,
                            "Leading token concatenation operators (##) are not necessary.\n");
                    quoted = false;
                    ptr++;
                }

                while (*ptr == '#' && *(ptr + 1) == '#')
                    ptr += 2;

                if (*ptr == '#') {
                    logger.error(current_file, line,
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
                if (tok == args[i])
                    break;
            }

            if (i == c.num_args) {
                if (quoted) {
                    logger.error(current_file, line, "Stringizing is only allowed for arguments.\n");
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
                //#TODO ignore ANYTHING inside double quoted string. Don't count occurences inside there
                c.num_occurences++;
            }

            free(tok);

            // Handle concatenation
            while (*ptr == '#' && *(ptr + 1) == '#') {
                if (quoted) {
                    logger.error(current_file, line,
                            "Token concatenations cannot be stringized.\n");
                    return false;
                }

                ptr += 2;
                if (!IS_MACRO_CHAR(*ptr))
                    logger.warning(current_file, line, LoggerMessageType::excessive_concatenation,
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

    return true;
}

std::optional<std::string> Preprocessor::constants_preprocess(const ConstantMapType &constants, std::string_view source, int line, constant_stack &constant_stack) {
    const char *ptr = source.data();
    const char *start;

    struct constToProcess {
        ConstantMapType::const_iterator constant;
        std::vector<std::string> args;
        std::string processed;
    };

    std::vector<std::variant<std::string_view, constToProcess>> result;
    result.reserve(8);

    auto processConstants = [this, &line, &constant_stack, &result, &constants]() {
        const auto processConst = 
            [&](std::variant<std::string_view, constToProcess>& var) {


            if (var.index() == 0) return std::get<std::string_view>(var).length();

            auto& cnst = std::get<constToProcess>(var);

            auto substack = constant_stack;
            auto value = constant_value(constants, cnst.constant, cnst.args.size(), cnst.args, line, substack);//#TODO remove args.size and just get it in func
            if (!value)
                return static_cast<size_t>(0u); //#TODO exception
            cnst.processed = std::move(*value);
            return cnst.processed.length();
        };

        auto finalSize = (result.size() > 5) ?
            std::transform_reduce(MULTITHREADED_EXC, result.begin(), result.end(), static_cast<size_t>(0u), [](size_t l, size_t r) {return l + r; }, processConst)
            :
            std::transform_reduce(std::execution::seq, result.begin(), result.end(), static_cast<size_t>(0u), [](size_t l, size_t r) {return l + r; }, processConst);
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
        while (*ptr != 0 && !IS_MACRO_CHAR(*ptr)) {
            if (*ptr == '"') {
                ptr++;
                while (*ptr != 0 && *ptr != '"')
                    ptr++;
            }
            ptr++; //also skips ending "
        }

        //#TODO support for #<macroval> needs to add quotes here
        if (ptr - start > 0) {
            auto val = std::string_view(start, ptr - start);
            if (val != "##") //These are supposed to disappear
                result.emplace_back(val);
        }

        if (*ptr == 0)
            break;

        // Potential tokens
        start = ptr;
        while (IS_MACRO_CHAR(*ptr))
            ptr++;

        std::string_view src(start, ptr - start);

        if (src.front() == '_') {

            //#TODO __LINE__ macro
            //#TODO __FILE__ macro

        }

        auto found = constants.find(std::string(src));

        if (found == constants.end() || (found->second.num_args > 0 && *ptr != '(')) {
            result.emplace_back(src);
            continue;
        }

        // prevent infinite loop
        if (std::find(constant_stack.stack.begin(), constant_stack.stack.end(), found) != constant_stack.stack.end())
            continue;

        const constant& c = found->second;


        constToProcess curConst;
        auto& args = curConst.args;
        curConst.constant = found;
        if (*ptr == '(') {
            args.reserve(4);
            ptr++;
            start = ptr;

            char in_string = 0;
            int level = 0;
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
                logger.error(current_file, line,
                        "Incomplete argument list for macro \"%s\".\n", c.name.c_str());
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

std::optional<std::string> Preprocessor::constant_value(const ConstantMapType &constants, ConstantMapType::const_iterator constantIter,
        int num_args, std::vector<std::string>& args, int line, constant_stack & constant_stack) {
    //#TODO num_args and line be unsigned
    auto& constant = constantIter->second;


    if (num_args != constant.num_args) {
        if (num_args)
            logger.error(current_file, line,
                    "Macro \"%s\" expects %i arguments, %i given.\n", constant.name.c_str(), constant.num_args, num_args);
        return {};
    }

    for (uint32_t i = 0; i < num_args; i++) {
        auto ret = constants_preprocess(constants, args[i], line, constant_stack);
        if (!ret)
            return {};
        trimRef(*ret); //trim tabs and spaces from both ends
        args[i] = std::move(*ret);
    }


    std::string result;
    if (num_args == 0) {
        result = constant.value;
    } else {
        result.reserve(constant.num_occurences * 16);
        const char *ptr = constant.value.data();
        for (uint32_t i = 0; i < constant.num_occurences; i++) {
            result += std::string_view(ptr, constant.occurrences[i].second - (ptr - constant.value.data()));
            result += args[constant.occurrences[i].first];
            ptr = constant.value.data() + constant.occurrences[i].second;
        }
        result += ptr;
    }

    constant_stack.stack.push_back(constantIter);

    auto res = constants_preprocess(constants, result, line, constant_stack);

    constant_stack.stack.pop_back();

    if (res && !res->empty()) {
        //trim tabs and spaces from both ends
        trimRef(*res);
        return res;
    }

    return res;
}

bool matches_includepath(std::filesystem::path startPath, std::string_view includepath, std::string_view includefolder) {
    /*
     * Checks if a given file can be matched to an include path by traversing
     * backwards through the filesystem until a $PBOPREFIX$ file is found.
     * If the prefix file, together with the diretory strucure, matches the
     * included path, true is returned.
     */
    auto path = startPath;




    while (path.has_relative_path()) {
        path = path.parent_path();
        if (!std::filesystem::exists(path / "$PBOPREFIX$")) continue;
        if (path.string().length() < includefolder.length()) break; //We had to leave includefolder to find it. Probably not correct.

        std::ifstream prefixFile(path / "$PBOPREFIX$");
        std::string prefix;
        std::getline(prefixFile, prefix);
        prefixFile.close();

        if (prefix.back() == '\r') prefix.pop_back();
        if (prefix.back() == '\\') prefix.pop_back();

        prefix += startPath.string().substr(path.string().length());

        for (char& i : prefix) {
            if (i == '/')
                i = '\\';
        }

        // compensate for missing leading slash in PBOPREFIX
        if (prefix[0] != '\\' && includepath[0] == '\\')
            return (strcmp(prefix.data(), includepath.data() +1) == 0);
        else
            return (strcmp(prefix.data(), includepath.data()) == 0);
    }

    //if the path contains the searchpath plus full prefix. It's probably correct. (unpacked modfolder in include directory, without pboprefix)
    if (includepath.size() > startPath.string().size()) return false;
    return std::equal(includepath.rbegin(), includepath.rend(), startPath.string().rbegin());
}


std::optional<std::filesystem::path> find_file_helper(std::string_view includepath, std::string_view includefolder) {
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

    std::filesystem::path incPath(includepath);
    auto filename = incPath.filename();

    //Check if includefolder is a pdrive
    auto subf = std::filesystem::path(includefolder) / includepath;
    if (std::filesystem::exists(subf) && matches_includepath(subf, includepath, includefolder))
        return subf;

    const std::filesystem::path ignoreGit(".git");
    const std::filesystem::path ignoreSvn(".svn");

    //recrusively search in directory
    for (auto i = std::filesystem::recursive_directory_iterator(includefolder, std::filesystem::directory_options::follow_directory_symlink);
        i != std::filesystem::recursive_directory_iterator();
        ++i) {
        if (i->is_directory() && (i->path().filename() == ignoreGit || i->path().filename() == ignoreSvn)) {
            i.disable_recursion_pending(); //Don't recurse into that directory
            continue;
        }
        if (!i->is_regular_file()) continue;

        if (i->path().filename() == filename && matches_includepath(i->path(), includepath, includefolder)) {
            return *i;
        }
    }

    // check for file without pboprefix

//    char filename2[2048];
//    strncpy(filename2, includefolder.data(), sizeof(filename2));
//    strncat(filename2, includepath.data(), sizeof(filename2) - strlen(filename2) - 1);
//#ifndef _WIN32
//    int i;
//    for (i = 0; i < strlen(filename); i++) {
//        if (filename[i] == '\\')
//            filename[i] = '/';
//    }
//#endif
    //if (std::filesystem::exists(filename)) {
    //    strcpy(actualpath, filename);
    //    return 0;
    //}

    return {};
}

__itt_string_handle* handle_find_file = __itt_string_handle_create("find_file");
std::optional<std::filesystem::path> find_file(std::string_view includepath, std::string_view origin) {
    __itt_task_begin(preprocDomain, __itt_null, __itt_null, handle_find_file);
    static std::map<std::string, std::filesystem::path, std::less<>> cache;

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
        std::filesystem::path originPath(origin);
        auto result = originPath.parent_path() / includepath;
        if (std::filesystem::exists(result)) {
            __itt_task_end(preprocDomain);
            return result;
        }
    }

    if (auto found = cache.find(includepath); found != cache.end()) {
        __itt_task_end(preprocDomain);
        return found->second;
    }


    extern struct arguments args;
    for (int i = 0; i < args.num_includefolders; i++) {
        auto result = find_file_helper(includepath, args.includefolders[i]);

        if (result) {
            __itt_task_end(preprocDomain);
            cache.insert_or_assign(std::string(includepath), *result);
            return result;
        }
    }
    __itt_task_end(preprocDomain);
    return {};
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


int Preprocessor::preprocess(char *source, std::ostream &f_target, ConstantMapType &constants) {
    /*
     * Writes the contents of source into the target file pointer, while
     * recursively resolving constants and includes using the includefolder
     * for finding included files.
     *
     * Returns 0 on success, a positive integer on failure.
     */

    std::ifstream f_source(source, std::ifstream::in | std::ifstream::binary);

    if (!f_source.is_open() || f_source.fail()) {
        logger.error("Failed to open %s.\n", source);
        return 1;
    }

    return preprocess(source, f_source, f_target, constants);
}


int Preprocessor::preprocess(std::string_view sourceFileName, std::istream &input, std::ostream &output, ConstantMapType &constants) {
    __itt_string_handle* handle_preprocess = __itt_string_handle_create(sourceFileName.data());
    __itt_task_begin(preprocDomain, __itt_null, __itt_null, handle_preprocess);

    int line = 0;
    int level = 0;
    int level_true = 0;
    int level_comment = 0;

    current_file = sourceFileName;
    include_stack.reserve(8);
    constants.reserve(64);

    if (std::find(include_stack.begin(), include_stack.end(), std::string(sourceFileName)) != include_stack.end()) {

        logger.error("Circular dependency detected, printing include stack:\n", sourceFileName);
        fprintf(stderr, "    !!! %s\n", sourceFileName.data());
        for (auto& it : reverse(include_stack)) {
            fprintf(stderr, "        %s\n", it.c_str()); //#TODO don't print to stderr. Make a global config thingy that contains the error stream (might be file or even network)
        }
        __itt_task_end(preprocDomain);
        return 1;
    }

    include_stack.emplace_back(sourceFileName);

    // Skip byte order mark if it exists
    if (input.peek() == 0x3f)
        input.seekg(3, std::ifstream::beg);

    //#TODO use fileystem::path to get filename here
    int file_index = lineref.file_names.size();
    if (strchr(sourceFileName.data(), PATHSEP) == NULL)
        lineref.file_names.emplace_back(sourceFileName);
    else
        lineref.file_names.emplace_back(strrchr(sourceFileName.data(), PATHSEP) + 1);



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
        c.stack.reserve(8);
        std::optional<std::string> preprocessedLine = constants_preprocess(constants, curLine, line, c);
        if (!preprocessedLine) {
            logger.error(sourceFileName, line, "Failed to resolve macros.\n");
            //return 1; 
            //#TODO exceptions
        }
        return *preprocessedLine;
    };



    auto processLines = [&]() {
        std::vector<std::string> ret;
        ret.resize(linesToProcess.size());

        std::transform(MULTITHREADED_EXC, linesToProcess.begin(),linesToProcess.end(), ret.begin(),[&](std::tuple<std::string, bool, int>& element) {
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



    std::string curLine;
    while (std::getline(input, curLine)) {// get line and add next lines if line ends with a backslash
        
        // fix windows line endings
        if (!curLine.empty() && curLine.back() == '\r')
            curLine.pop_back();

        line++;
        lineref.file_index.push_back(file_index);
        lineref.line_number.push_back(line);

        if (curLine.empty()) continue;

        while (curLine.back() == '\\') {
            curLine.pop_back(); //remove backslash
            //if (curLine.back() != '\n')
            //    curLine += '\n';

            std::string nextLine;
            std::getline(input, nextLine);
            // fix windows line endings
            if (nextLine.back() == '\r')
                nextLine.pop_back(); //remove the \r so that we hit the backslash in the while condition


            //#TODO lineref should be map<uint32_t, pair<file_index, line_number>>
            //where key is current line. aka number of \n's that were written to output so far


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
                    logger.error(sourceFileName, line, "Failed to parse #include.\n");
                    __itt_task_end(preprocDomain);
                    return 5;
                }
                auto lastQuote = directive_args.find_last_of('"');

                if (lastQuote == std::string::npos) {
                    logger.error(sourceFileName, line, "Failed to parse #include.\n");
                    __itt_task_end(preprocDomain);
                    return 6;
                }

                directive_args = directive_args.substr(firstQuote + 1, lastQuote - firstQuote - 1);

                auto fileFound = find_file(directive_args.c_str(), sourceFileName);
                if (!fileFound) {
                    logger.error(sourceFileName, line, "Failed to find %s.\n", directive_args.c_str());
                    __itt_task_end(preprocDomain);
                    return 7;
                }

                std::ifstream includefile(fileFound->string());

                if (!includefile.is_open() || includefile.fail()) {
                    logger.error("Failed to open %s.\n", fileFound->string().c_str());
                    __itt_task_end(preprocDomain);
                    return 1;
                }

                int success = preprocess(fileFound->string(), includefile, output, constants);

                include_stack.pop_back();

                current_file = sourceFileName;

                if (success) {
                    __itt_task_end(preprocDomain);
                    return success;
                }
                   
            } else if (directive == "define") { //#TODO directive string to enum
                if (!constants_parse(constants, directive_args, line)) {
                    logger.error(sourceFileName, line, "Failed to parse macro definition.\n");
                    __itt_task_end(preprocDomain);
                    return 3;
                }
            } else if (directive == "undef") {
                constants.erase(directive_args);
            } else if (directive == "ifdef") {
                level++;

                auto found = constants.find(directive_args);
                if (found != constants.end())
                    level_true++;
            } else if (directive == "ifndef") {
                level++;
                auto found = constants.find(directive_args);
                if (found == constants.end())
                    level_true++;
            } else if (directive == "else") {
                if (level == level_true)
                    level_true--;
                else
                    level_true = level;
            } else if (directive == "endif") {
                if (level == 0) {
                    logger.error(sourceFileName, line, "Unexpected #endif.\n");
                    __itt_task_end(preprocDomain);
                    return 4;
                }
                if (level == level_true)
                    level_true--;
                level--;
            } else {
                logger.error(sourceFileName, line, "Unknown preprocessor directive \"%s\".\n", directive.c_str());
                __itt_task_end(preprocDomain);
                return 5;
            }
        } else if (curLine.length() > 1) {
            linesToProcess.emplace_back(std::move(curLine), true, line);
        }
    }
    processLines();
    __itt_task_end(preprocDomain);
    return 0;
}
