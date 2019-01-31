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
#include <algorithm>
#include "ittnotify.h"

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
    vector3 pos;
    uint32_t point_flags;

    /*
    //#TODO point flags
    
    // special flags
    POINT_ONLAND    0x1
    POINT_UNDERLAND 0x2
    POINT_ABOVELAND 0x4
    POINT_KEEPLAND  0x8
    POINT_LAND_MASK 0xf

    POINT_DECAL      0x100
    POINT_VDECAL     0x200
    POINT_DECAL_MASK 0x300

    POINT_NOLIGHT    0x10 // active colors
    POINT_AMBIENT    0x20
    POINT_FULLLIGHT  0x40
    POINT_HALFLIGHT  0x80
    POINT_LIGHT_MASK 0xf0

    POINT_NOFOG     0x1000 // active colors
    POINT_SKYFOG    0x2000
    POINT_FOG_MASK  0x3000

    POINT_USER_MASK  0xff0000
    POINT_USER_STEP  0x010000

    POINT_SPECIAL_MASK   0xf000000
    POINT_SPECIAL_HIDDEN 0x1000000


     */
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
        return *this;
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

bool matches_glob(std::string_view string, std::string_view pattern);

int fsign(float f);

void lower_case(char *string);

void get_word(char *target, char *source);

void trim_leading(char *string, size_t buffsize);

void trim(char *string, size_t buffsize);

std::string_view trim(std::string_view string);
void trimRef(std::string &string);

void replace_string(char *string, size_t buffsize, char *search, char *replace, int max, bool macro);

void quote(char *string);

std::string escape_string(std::string_view input);

std::string unescape_string(std::string_view buffer);

void write_compressed_int(uint32_t integer, std::ostream &f);

uint32_t read_compressed_int(std::istream& f);


inline bool iequals(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(),
        b.begin(), b.end(),
        [](char a, char b) {
        return tolower(a) == tolower(b);
    });
}




class ColorFloat {
public:
    float r, g, b, a;
    ColorFloat() noexcept : r(0), g(0), b(0), a(0) {}
    ColorFloat(std::vector<float> vec) : r(vec[0]), g(vec[1]), b(vec[2]), a(vec[3]) {}
    ColorFloat(float r, float g, float b, float a) noexcept : r(r), g(g), b(b), a(a) {}
    ColorFloat(float r, float g, float b) noexcept : r(r), g(g), b(b), a(1) {}




    ColorFloat operator *(ColorFloat other) const noexcept {
        return ColorFloat(r*other.r, g*other.g, b*other.b, a*other.a);
    }
    ColorFloat operator *(float c) const noexcept {
        return ColorFloat(r*c, g*c, b*c, a*c);
    }
    ColorFloat operator +(ColorFloat other) const noexcept {
        return ColorFloat(r + other.r, g + other.g, b + other.b, a + other.a);
    }
    ColorFloat operator -(ColorFloat other) const noexcept {
        return ColorFloat(r - other.r, g - other.g, b - other.b, a - other.a);
    }

    ColorFloat& operator +=(ColorFloat other) noexcept {
        r += other.r;
        g += other.g;
        b += other.b;
        a += other.a;
        return *this;
    }
    ColorFloat& operator -=(ColorFloat other) noexcept {
        r -= other.r;
        g -= other.g;
        b -= other.b;
        a -= other.a;
        return *this;
    }
};


class ColorInt {
public:


    //https://stackoverflow.com/a/119538
    //https://stackoverflow.com/questions/78619/what-is-the-fastest-way-to-convert-float-to-int-on-x86
    /** by Vlad Kaipetsky
    portable assuming FP24 set to nearest rounding mode
    efficient on x86 platform
    */
    union UFloatInt {
        int i;
        float f;
    };
    static constexpr float Snapper = 3 << 22;
    static inline int toInt(float fval)
    {
        //Assert(fabs(fval) <= 0x003fffff); // only 23 bit values handled
        UFloatInt &fi = *(UFloatInt *)&fval;
        fi.f += Snapper;
        return ((fi.i) & 0x007fffff) - 0x00400000;
    }

    uint32_t value; //ARGB
    ColorInt() : value(0) {}
    ColorInt(uint32_t v) : value(v) {}
    ColorInt(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : value(a << 24 | r << 16 | g << 8 | b) {}
    ColorInt(ColorFloat f) {
        uint8_t _r = toInt(std::clamp(f.r, 0.f, 1.f) * 255);
        uint8_t _g = toInt(std::clamp(f.g, 0.f, 1.f) * 255);
        uint8_t _b = toInt(std::clamp(f.b, 0.f, 1.f) * 255);
        uint8_t _a = toInt(std::clamp(f.a, 0.f, 1.f) * 255);
        value = (_a << 24 | _r << 16 | _g << 8 | _b);
    }




    uint8_t getA() const { return (value >> 24) & 0xff; }
    uint8_t getR() const { return (value >> 16) & 0xff; }
    uint8_t getG() const { return (value >> 8) & 0xff; }
    uint8_t getB() const { return (value >> 0) & 0xff; }

    operator ColorFloat() const {
        constexpr float mult = 1.0f / 255;
        return ColorFloat(getR()*mult, getG()*mult, getB()*mult, getA()*mult);
    }
};
