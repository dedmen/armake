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

#include "vector.h"
#include <ratio>
#include <string>
#include <functional>


#include "lib/ittnotify.h"



#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define COLOR_RED "\e[1;31m"
#define COLOR_GREEN "\e[1;32m"
#define COLOR_YELLOW "\e[1;33m"
#define COLOR_BLUE "\e[1;34m"
#define COLOR_MAGENTA "\e[1;35m"
#define COLOR_CYAN "\e[1;36m"
#define COLOR_RESET "\e[0m"

#define OP_BUILD 1
#define OP_PREPROCESS 2
#define OP_RAPIFY 3
#define OP_P3D 4
#define OP_MODELCONFIG 5
#define OP_MATERIAL 6
#define OP_UNPACK 7
#define OP_DERAPIFY 8
#define OP_IMAGE 9


struct point {
    float x;
    float y;
    float z;
    uint32_t point_flags;
    vector3 getPosition() const {
        return { x,y,z };
    }
};

inline bool float_equal(float f1, float f2, float precision) {
    /*
     * Performs a fuzzy float comparison.
     */

    return fabs(1.0 - (f1 / f2)) < precision;
}

template <class Ratio>
class ComparableFloat {
public:
    float value;
    static constexpr float precision = static_cast<float>(Ratio::num)/static_cast<float>(Ratio::den);

    ComparableFloat() : value(0.f) {}
    ComparableFloat(const float& other) : value(other) {}
    ComparableFloat& operator=(float newVal) {
        value = newVal;
    }
    operator float() const {
        return value;
    }
    bool operator==(const float& other) const {
        if (value == other) return true;
        return float_equal(value, other, precision);
    }
    bool operator!=(const float& other) const {
        return !(*this == other);
    }
    bool operator>=(const float& other) const {
        if (value == other) return true;
        return value > other;
    }
    bool operator<=(const float& other) const {
        if (value == other) return true;
        return value < other;
    }
    bool operator==(const ComparableFloat& other) const {
        return *this == static_cast<float>(other);
    }
    bool operator!=(const ComparableFloat& other) const {
        return !(*this == other);
    }
    bool operator>=(const ComparableFloat& other) const {
        return *this >= static_cast<float>(other);
    }
    bool operator<=(const ComparableFloat& other) const {
        return !(*this <= static_cast<float>(other));
    }

};


class ScopeGuard {
public:
    explicit ScopeGuard(std::function<void()> func) :function(std::move(func)) {}

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

    void dismiss() { dismissed = true; }
    ~ScopeGuard() { if (!dismissed) function(); }
private:
    bool dismissed = false;
    std::function<void()> function;
};


extern std::string current_target;

#ifdef _WIN32
char *strndup(const char *s, size_t n);

char *strchrnul(char *s, int c);
#else
int stricmp(char *a, char *b);
#endif

void *safe_malloc(size_t size);
void *safe_realloc(void *ptr, size_t size);
char *safe_strdup(const char *s);
char *safe_strndup(const char *s, size_t n);

int get_line_number(FILE *f_source);

void reverse_endianness(void *ptr, size_t buffsize);

bool matches_glob(const char *string, const char *pattern);

int fsign(float f);

void lower_case(char *string);

void get_word(char *target, char *source);

void trim_leading(char *string, size_t buffsize);

void trim(char *string, size_t buffsize);

std::string trim(std::string_view string);
void trimRef(std::string &string);

void replace_string(char *string, size_t buffsize, char *search, char *replace, int max, bool macro);

void quote(char *string);

char lookahead_c(FILE *f);

int lookahead_word(FILE *f, char *buffer, size_t buffsize);

int skip_whitespace(FILE *f);

void escape_string(char *buffer, size_t buffsize);
std::string escape_string(std::string_view input);

std::string unescape_string(std::string_view buffer);

void write_compressed_int(uint32_t integer, FILE *f);
void write_compressed_int(uint32_t integer, std::ostream &f);

uint32_t read_compressed_int(FILE *f);
uint32_t read_compressed_int(std::istream& f);
