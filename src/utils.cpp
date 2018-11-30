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
#include <stdarg.h>
#include <string.h>
//#include <unistd.h>
#include <math.h>

#include "args.h"
#include "filesystem.h"
#include "utils.h"
#include <vector>
std::string current_target;

#ifdef _WIN32

char *strndup(const char *s, size_t n) {
    char *result = (char *)safe_malloc(n + 1);
    strncpy(result, s, n);
    result[n] = 0;
    return result;
}

char *strchrnul(char *s, int c) {
    char *result = strchr(s, c);
    if (result != NULL)
        return result;
    return (char *)s + strlen(s);
}

#else

int stricmp(char *a, char *b) {
    int d;
    char a_lower;
    char b_lower;

    for (;; a++, b++) {
        a_lower = *a;
        b_lower = *b;

        if (a_lower >= 'A' && a_lower <= 'Z')
            a_lower -= 'A' - 'a';

        if (b_lower >= 'A' && b_lower <= 'Z')
            b_lower -= 'A' - 'a';

        d = a_lower - b_lower;
        if (d != 0 || !*a)
            return d;
    }
}

#endif

void *safe_malloc(size_t size) {
    void *result = malloc(size);

    if (result == NULL) {
        __debugbreak();
        //errorf("Failed to allocate %i bytes.\n", size);
        exit(127);
    }

    return result;
}


void *safe_realloc(void *ptr, size_t size) {
    void *result = realloc(ptr, size);

    if (result == NULL) {
        __debugbreak();
        //errorf("Failed to reallocate %i bytes.\n", size);
        exit(127);
    }

    return result;
}


char *safe_strdup(const char *s) {
    char *result = strdup(s);

    if (result == NULL) {
        __debugbreak();
        //errorf("Failed to reallocate %i bytes.\n", strlen(s) + 1);
        exit(127);
    }

    return result;
}


char *safe_strndup(const char *s, size_t n) {
    char *result = strndup(s, n);

    if (result == NULL) {
        __debugbreak();
        //errorf("Failed to reallocate %i bytes.\n", n + 1);
        exit(127);
    }

    return result;
}


int get_line_number(FILE *f_source) {
    int line;
    long fp_start;

    fp_start = ftell(f_source);
    fseek(f_source, 0, SEEK_SET);

    line = 0;
    while (ftell(f_source) < fp_start) {
        if (fgetc(f_source) == '\n')
            line++;
    }

    return line;
}


void reverse_endianness(void *ptr, size_t buffsize) {
    char *buffer;
    char *temp;
    int i;

    buffer = (char *)ptr;
    temp = (char*)safe_malloc(buffsize);

    for (i = 0; i < buffsize; i++) {
        temp[(buffsize - 1) - i] = buffer[i];
    }

    memcpy(buffer, temp, buffsize);

    free(temp);
}


bool matches_glob(std::string_view string, std::string_view pattern) {
    auto ptr1 = string.begin();
    auto ptr2 = pattern.begin();

    while (ptr1 != string.end() && ptr2 != pattern.end()) {
        if (*ptr2 == '*') {
            ++ptr2;
            while (true) {
                if (matches_glob(string.substr(ptr1 - string.begin()), pattern.substr(ptr2 - pattern.begin())))
                    return true;
                if (*ptr1 == 0)
                    return false;
                ++ptr1;
            }
        }

        if (*ptr2 != '?' && *ptr1 != *ptr2)
            return false;

        ++ptr1;
        ++ptr2;
    }

    return (*ptr1 == *ptr2);
}


int fsign(float f) {
    return (0 < f) - (f < 0);
}


void lower_case(char *string) {
    /*
     * Converts a null-terminated string to lower case.
     */

    int i;

    for (i = 0; i < strlen(string); i++) {
        if (string[i] >= 'A' && string[i] <= 'Z')
            string[i] -= 'A' - 'a';
    }
}


void trim_leading(char *string, size_t buffsize) {
    /*
     * Trims leading tabs and spaces on the string.
     */

    std::vector<char> tmp;
    tmp.resize(buffsize);
    char *ptr = tmp.data();
    strncpy(tmp.data(), string, buffsize);
    while (*ptr == ' ' || *ptr == '\t')
        ptr++;
    strncpy(string, ptr, buffsize - (ptr - tmp.data()));
}


void trim(char *string, size_t buffsize) {
    /*
     * Trims tabs and spaces on either side of the string.
     */

    char *ptr;

    trim_leading(string, buffsize);

    ptr = string + (strlen(string) - 1);
    while (ptr >= string && (*ptr == ' ' || *ptr == '\t'))
        ptr--;

    *(ptr + 1) = 0;
}


std::string_view trim(std::string_view string) {
    /*
     * Trims tabs and spaces on either side of the string.
     */
    if (string.empty()) return "";

    auto begin = string.find_first_not_of("\t ");
    auto end = string.find_last_not_of("\t ");
    return string.substr(begin, end - begin + 1);
}

void trimRef(std::string& string) {
    
    auto begin = string.find_first_not_of("\t ");
    auto end = string.find_last_not_of("\t ");
    string = string.substr(begin, end - begin + 1);
}

void replace_string(char *string, size_t buffsize, char *search, char *replace, int max, bool macro) {
    /*
     * Replaces the search string with the given replacement in string.
     * max is the maximum number of occurrences to be replaced. 0 means
     * unlimited.
     */

    if (strstr(string, search) == NULL)
        return;

    char *tmp;
    char *ptr_old;
    char *ptr_next;
    char *ptr_tmp;
    int i;
    bool quote;

    tmp = (char*)safe_malloc(buffsize);
    strncpy(tmp, string, buffsize);

    ptr_old = string;
    ptr_tmp = tmp;

    for (i = 0;; i++) {
        ptr_next = strstr(ptr_old, search);
        ptr_tmp += (ptr_next - ptr_old);
        if (ptr_next == NULL || (i >= max && max != 0))
            break;

        quote = false;
        if (macro && ptr_next > string + 1 && *(ptr_next - 2) == '#' && *(ptr_next - 1) == '#')
            ptr_next -= 2;
        else if ((quote = macro && (ptr_next > string && *(ptr_next - 1) == '#')))
            *(ptr_next - 1) = '"';

        strncpy(ptr_next, replace, buffsize - (ptr_next - string));
        ptr_next += strlen(replace);

        if (quote)
            *(ptr_next++) = '"';

        ptr_tmp += strlen(search);

        if (macro && *ptr_tmp == '#' && *(ptr_tmp + 1) == '#')
            ptr_tmp += 2;

        strncpy(ptr_next, ptr_tmp, buffsize - (ptr_next - string));

        ptr_old = ptr_next;
    }

    free(tmp);
}


void quote(char *string) {
    char tmp[1024] = "\"";
    strncat(tmp, string, 1022);
    strcat(tmp, "\"");
    strncpy(string, tmp, 1024);
}

void escape_string(char *buffer, size_t buffsize) {
    char *tmp;
    char *ptr;
    char tmp_array[3];

    tmp = (char*)safe_malloc(buffsize * 2);
    tmp[0] = 0;
    tmp_array[2] = 0;

    for (ptr = buffer; *ptr != 0; ptr++) {
        tmp_array[0] = '\\';
        if (*ptr == '\r') {
            tmp_array[1] = 'r';
        } else if (*ptr == '\n') {
            tmp_array[1] = 'n';
        } else if (*ptr == '"') {
            tmp_array[0] = '"';
            tmp_array[1] = '"';
        } else {
            tmp_array[0] = *ptr;
            tmp_array[1] = 0;
        }
        strcat(tmp, tmp_array);
    }

    strncpy(buffer, tmp, buffsize);

    free(tmp);
}

std::string escape_string(std::string_view input) {
    char tmp_array[3];

    std::string tmp;
    tmp.resize(input.size());
    tmp_array[2] = 0;

    for (const char* ptr = input.data(); *ptr != 0; ptr++) {
        tmp_array[0] = '\\';
        if (*ptr == '\r') {
            tmp_array[1] = 'r';
        }
        else if (*ptr == '\n') {
            tmp_array[1] = 'n';
        }
        else if (*ptr == '"') {
            tmp_array[0] = '"';
            tmp_array[1] = '"';
        }
        else {
            tmp_array[0] = *ptr;
            tmp_array[1] = 0;
        }
        tmp += tmp_array;
    }
    return tmp;
}

std::string unescape_string(std::string_view buffer) {
    char tmp_array[2];

    tmp_array[1] = 0;

    std::string tmp;
    tmp.reserve(buffer.size());

    const char quote = buffer[0];
    //#TODO get rid of the copy here. Also use std::copy_if maybe?
    std::string forIterate = std::string(buffer.substr(1, buffer.length() - 2 )); //Cut off end and start

    for (char *ptr = forIterate.data(); *ptr != 0; ptr++) {
        char current = *ptr;

        if (*ptr == '\\' && *(ptr + 1) == '\\') {
            ptr++;
        }
        else if (*ptr == '\\' && *(ptr + 1) == '"') {
            current = '"';
            ptr++;
        }
        else if (*ptr == '\\' && *(ptr + 1) == '\'') {
            current = '\'';
            ptr++;
        }
        else if (*ptr == quote && *(ptr + 1) == quote) {
            ptr++;
        }

        if (*ptr == 0)
            break;

        tmp_array[0] = current;
        tmp += tmp_array;
    }

    return tmp;
}

void write_compressed_int(uint32_t integer, std::ostream &f) {
    uint64_t temp;
    char c;

    temp = (uint64_t)integer;

    if (temp == 0) {
        f.put(0);
    }

    while (temp > 0) {
        if (temp > 0x7f) {
            // there are going to be more entries
            c = 0x80 | (temp & 0x7f);
            f.put(c);
            temp = temp >> 7;
        }
        else {
            // last entry
            c = temp;
            f.put(c);
            temp = 0;
        }
    }
}

uint32_t read_compressed_int(std::istream & f) {
    uint64_t result = 0;

    for (int i = 0; i <= 4; i++) {
        
        uint8_t temp = f.get();
        result = result | ((temp & 0x7f) << (i * 7));

        if (temp < 0x80)
            break;
    }

    return (uint32_t)result;
}
