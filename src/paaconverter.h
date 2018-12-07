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

class Logger;

enum class PAAType : uint16_t {
    default,
    DXT1 = 0xFF01,
    DXT3 = 0xFF03,
    DXT5 = 0xFF05,
    ARGB4444 = 0x4444,
    ARGB1555 = 0x1555,
    AI88 = 0x8080,
    invalid
};

class PAAFile {
public:
    ///input has to be opened with binary flag
    void readHeaders(std::istream& input);
    bool isAlpha;
    bool isTransparent;
    uint16_t width;
    uint16_t height;
    PAAType type;
    uint32_t avgColor{ 0 }; //#TODO packed color type
    uint32_t maxColor{ 0 };
};


class PAAConverter{
    static void img2dxt1(unsigned char *input, unsigned char *output, int width, int height);
    static void img2dxt5(unsigned char *input, unsigned char *output, int width, int height);
    static void dxt12img(unsigned char *input, unsigned char *output, int width, int height);
    static void dxt52img(unsigned char *input, unsigned char *output, int width, int height);
public:

    static constexpr PAAType typeFromString(std::string_view str) {
        if (str.empty())
            return PAAType::default;
        else if (str == "DXT1")
            return PAAType::DXT1;
        else if (str == "DXT3")
            return PAAType::DXT3;
        else if (str == "DXT5")
            return PAAType::DXT5;
        else if (str == "ARGB4444")
            return PAAType::ARGB4444;
        else if (str == "ARGB1555")
            return PAAType::ARGB1555;
        else if (str == "AI88")
            return PAAType::AI88;
        else
            return PAAType::invalid;
    }

    static int img2paa(std::istream &source, std::ostream &target, Logger& logger, PAAType targetType = PAAType::default);
    static int paa2img(std::istream &source, std::ostream &target, Logger& logger);

    static int cmd_img2paa(Logger& logger);
    static int cmd_paa2img(Logger& logger);

};

