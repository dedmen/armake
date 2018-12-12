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


#ifndef _WIN32
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"
#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"
#include "minilzo.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "args.h"
#include "utils.h"
#include "paaconverter.h"
#include "logger.h"

#define COMP_NONE 0
#define COMP_LZSS 1
#define COMP_LZO  2

void PAAFile::readHeaders(std::istream& input) {

    input.read(reinterpret_cast<char*>(&type), 2); //palette size

    while (true) {
        char taggsig[5];
        input.read(taggsig, 4);
        taggsig[4] = 0x00;
        if (std::string_view(taggsig) != "GGAT") {
            input.seekg(-4, std::istream::cur); //Go back the 4 bytes of the invalid tag we tried to read
            break; //This is not a tag.
        }

        char taggname[5];
        input.read(taggname, 4);
        taggname[4] = 0x00;
        std::string_view tagName(taggname);

        uint32_t tagglen;
        input.read(reinterpret_cast<char*>(&tagglen), 4);
        if (tagName == "SFFO") {

            auto offsCount = tagglen/sizeof(uint32_t);

            std::vector<uint32_t> offs;
            offs.reserve(offsCount);

            for (int i = 0; i < offsCount; ++i) {
                uint32_t val;
                input.read(reinterpret_cast<char*>(&val), 4);
                if (val != 0) //There are 0's in here? There are always 16 elements even though only some of them are filled.
                offs.emplace_back(val); //#TODO these are mipmap offsets, we should store these
            }
            mipmap = offs[0]; //#TODO there can be 0 mipmaps here theoretically
            continue;
        }

        if (tagName == "CGVA") { //avg color
            input.read(reinterpret_cast<char*>(&avgColor.value), 4);
            continue;
        }

        if (tagName == "CXAM") { //max color
            input.read(reinterpret_cast<char*>(&maxColor.value), 4);
            continue;
        }

        if (tagName == "ZIWS") { //??? SWIZ ?? WAT?
            //source.ignore(tagglen);
            //Seems to be 32bit. Dunno what this is.
            //0x08 0x01 0x02 0x03 ??
            //source.read(reinterpret_cast<char*>(&xxx), 4);
            //continue;
        }

        if (tagName == "GALF") { //Flags. Is alpha/istransparent
            uint32_t flags;
            input.read(reinterpret_cast<char*>(&flags), 4);
            if (flags & 1) isAlpha = true;
            if (flags & 2) isTransparent = true;
            continue;
        }

        input.ignore(tagglen); //unknown tag
    }

    //#TODO Read 16bit size of palette and palette
    uint16_t paletteSize;
    input.read(reinterpret_cast<char*>(&paletteSize), sizeof(paletteSize));

    auto pos = input.tellg();
    input.seekg(mipmap, std::ifstream::beg);

    input.read(reinterpret_cast<char*>(&width), sizeof(width));
    width &= 0x7fff;//compression flag
    input.read(reinterpret_cast<char*>(&height), sizeof(height));
    //mipmap will read this again, we just want this to fill the width/height vars
    input.seekg(pos);

}

bool PAAFile::skipMipmap(std::istream& input) {
    uint16_t width;
    uint16_t height;
    input.read(reinterpret_cast<char*>(&width), sizeof(width));
    input.read(reinterpret_cast<char*>(&height), sizeof(height));
    if (width == 0 || height == 0) return false;

    uint32_t datalen = 0;
    //compressed Size
    input.read(reinterpret_cast<char*>(&datalen), 3);
    input.ignore(datalen); //skip mipmap
    return true;
}

bool PAAFile::convertMipmap(std::istream& input, std::ostream& target, Logger& logger) {
    

    uint16_t width;
    uint16_t height;
    input.read(reinterpret_cast<char*>(&width), sizeof(width));
    input.read(reinterpret_cast<char*>(&height), sizeof(height));
    if (width == 0 || height == 0) return false;

    uint32_t datalen = 0;
    //compressed Size
    input.read(reinterpret_cast<char*>(&datalen), 3);


    std::vector<unsigned char> compressedData;
    compressedData.resize(datalen);
    input.read(reinterpret_cast<char*>(compressedData.data()), datalen);

    int compression = COMP_NONE;
    if (width % 32768 != width && (type == PAAType::DXT1 || type == PAAType::DXT3 || type == PAAType::DXT5)) {
        width -= 32768;
        compression = COMP_LZO;
    }
    else if (type == PAAType::ARGB4444 || type == PAAType::ARGB1555 || type == PAAType::AI88) {
        compression = COMP_LZSS;
    }

    int imgdatalen = width * height;
    if (type == PAAType::DXT1)
        imgdatalen /= 2;
    std::vector<unsigned char> imgdata;
    imgdata.resize(imgdatalen);

    if (compression == COMP_LZO) {
        lzo_uint out_len = imgdatalen;
        if (lzo_init() != LZO_E_OK) {
            logger.error("Failed to initialize LZO for decompression.\n"); //#TODO throw exceptions instead
            return false;
        }
        if (lzo1x_decompress(compressedData.data(), datalen, imgdata.data(), &out_len, NULL) != LZO_E_OK) {
            logger.error("Failed to decompress LZO data.\n");
            return false;
        }
    }
    else if (compression == COMP_LZSS) {
        logger.error("LZSS compression support is not implemented.\n");
        return false;
    }
    else {
        memcpy(imgdata.data(), compressedData.data(), imgdata.size());
    }

    std::vector<unsigned char> outputdata;
    outputdata.resize(width * height * 4);

    switch (type) {
    case PAAType::DXT1:
        PAAConverter::dxt12img(imgdata.data(), outputdata.data(), width, height);
        break;
    case PAAType::DXT3:
        logger.error("DXT3 support is not implemented.\n");
        return false;
    case PAAType::DXT5:
        PAAConverter::dxt52img(imgdata.data(), outputdata.data(), width, height);
        break;
    case PAAType::ARGB4444:
        logger.error("ARGB4444 support is not implemented.\n");
        return false;
    case PAAType::ARGB1555:
        logger.error("ARGB1555 support is not implemented.\n");
        return false;
    case PAAType::AI88:
        logger.error("GRAY / AI88 support is not implemented.\n");
        return false;
    default:
        logger.error("Unrecognized PAA type.\n");
        return false;
    }

    if (!stbi_write_png_to_func([](void *context, void *data, int size)
        {
            static_cast<std::ostream*>(context)->write(static_cast<const char*>(data), size);
        }, &target, width, height, 4, outputdata.data(), width * 4)) {
        logger.error("Failed to write image to output.\n");
        return false;
    }

    return true;


}

void PAAConverter::img2dxt1(unsigned char *input, unsigned char *output, int width, int height) {
    /*
     * Converts image data to DXT1 data.
     */

    unsigned char img_block[64];
    unsigned char dxt_block[8];

    for (int i = 0; i < height; i += 4) {
        for (int j = 0; j < width; j += 4) {
            memcpy(img_block +  0, input + (i + 0) * width * 4 + j * 4, 16);
            memcpy(img_block + 16, input + (i + 1) * width * 4 + j * 4, 16);
            memcpy(img_block + 32, input + (i + 2) * width * 4 + j * 4, 16);
            memcpy(img_block + 48, input + (i + 3) * width * 4 + j * 4, 16);

            stb_compress_dxt_block(dxt_block, (const unsigned char *)img_block, 0, STB_DXT_HIGHQUAL);

            memcpy(output + (i / 4) * (width / 4) * sizeof(dxt_block) + (j / 4) * sizeof(dxt_block), dxt_block, sizeof(dxt_block));
        }
    }
}

void PAAConverter::img2dxt5(unsigned char *input, unsigned char *output, int width, int height) {
    /*
     * Converts image data to DXT5 data.
     */

    unsigned char img_block[64];
    unsigned char dxt_block[16];

    for (int i = 0; i < height; i += 4) {
        for (int j = 0; j < width; j += 4) {
            memcpy(img_block +  0, input + (i + 0) * width * 4 + j * 4, 16);
            memcpy(img_block + 16, input + (i + 1) * width * 4 + j * 4, 16);
            memcpy(img_block + 32, input + (i + 2) * width * 4 + j * 4, 16);
            memcpy(img_block + 48, input + (i + 3) * width * 4 + j * 4, 16);

            stb_compress_dxt_block(dxt_block, (const unsigned char *)img_block, 1, STB_DXT_HIGHQUAL);

            memcpy(output + (i / 4) * (width / 4) * sizeof(dxt_block) + (j / 4) * sizeof(dxt_block), dxt_block, sizeof(dxt_block));
        }
    }
}

void PAAConverter::dxt12img(unsigned char *input, unsigned char *output, int width, int height) {
    /* Convert DXT1 data into a PNG image array. */

    uint8_t c[4][3];
    unsigned int clookup[16];
    unsigned int x, y, index;

    struct dxt1block {
        uint16_t c0 : 16;
        uint16_t c1 : 16;
        uint8_t cl3 : 2;
        uint8_t cl2 : 2;
        uint8_t cl1 : 2;
        uint8_t cl0 : 2;
        uint8_t cl7 : 2;
        uint8_t cl6 : 2;
        uint8_t cl5 : 2;
        uint8_t cl4 : 2;
        uint8_t cl11 : 2;
        uint8_t cl10 : 2;
        uint8_t cl9 : 2;
        uint8_t cl8 : 2;
        uint8_t cl15 : 2;
        uint8_t cl14 : 2;
        uint8_t cl13 : 2;
        uint8_t cl12 : 2;
    } block;

    for (int i = 0; i < (width * height) / 2; i += 8) {
        memcpy(&block, input + i, 8);

        c[0][0] = 255 * ((63488 & block.c0) >> 11) / 31;
        c[0][1] = 255 * ((2016 & block.c0) >> 5) / 63;
        c[0][2] = 255 * (31 & block.c0) / 31;
        c[1][0] = 255 * ((63488 & block.c1) >> 11) / 31;
        c[1][1] = 255 * ((2016 & block.c1) >> 5) / 63;
        c[1][2] = 255 * (31 & block.c1) / 31;
        c[2][0] = (2 * c[0][0] + 1 * c[1][0]) / 3;
        c[2][1] = (2 * c[0][1] + 1 * c[1][1]) / 3;
        c[2][2] = (2 * c[0][2] + 1 * c[1][2]) / 3;
        c[3][0] = (1 * c[0][0] + 2 * c[1][0]) / 3;
        c[3][1] = (1 * c[0][1] + 2 * c[1][1]) / 3;
        c[3][2] = (1 * c[0][2] + 2 * c[1][2]) / 3;

        clookup[0] = block.cl0;
        clookup[1] = block.cl1;
        clookup[2] = block.cl2;
        clookup[3] = block.cl3;
        clookup[4] = block.cl4;
        clookup[5] = block.cl5;
        clookup[6] = block.cl6;
        clookup[7] = block.cl7;
        clookup[8] = block.cl8;
        clookup[9] = block.cl9;
        clookup[10] = block.cl10;
        clookup[11] = block.cl11;
        clookup[12] = block.cl12;
        clookup[13] = block.cl13;
        clookup[14] = block.cl14;
        clookup[15] = block.cl15;

        for (int j = 0; j < 16; j++) {
            x = ((i / 8) % (width / 4)) * 4 + 3 - (j % 4);
            y = ((i / 8) / (width / 4)) * 4 + (j / 4);
            index = (y * width + x) * 4;
            *(output + index + 0) = c[clookup[j]][0];
            *(output + index + 1) = c[clookup[j]][1];
            *(output + index + 2) = c[clookup[j]][2];
            *(output + index + 3) = 255;
        }
    }
}

void PAAConverter::dxt52img(unsigned char *input, unsigned char *output, int width, int height) {
    /* Convert DXT5 data into a PNG image array. */

    uint8_t a[8];
    unsigned int alookup[16];
    uint8_t c[4][3];
    unsigned int clookup[16];
    unsigned int x, y, index;

    /* For some reason, directly unpacking the alpha lookup table into the 16
     * 3-bit arrays didn't work, so i'm reading it into one 64bit integer and
     * unpacking it manually later. @todo */
    struct dxt5block {
        uint8_t a0 : 8;
        uint8_t a1 : 8;
        uint64_t al : 48;
        uint16_t c0 : 16;
        uint16_t c1 : 16;
        uint8_t cl3 : 2;
        uint8_t cl2 : 2;
        uint8_t cl1 : 2;
        uint8_t cl0 : 2;
        uint8_t cl7 : 2;
        uint8_t cl6 : 2;
        uint8_t cl5 : 2;
        uint8_t cl4 : 2;
        uint8_t cl11 : 2;
        uint8_t cl10 : 2;
        uint8_t cl9 : 2;
        uint8_t cl8 : 2;
        uint8_t cl15 : 2;
        uint8_t cl14 : 2;
        uint8_t cl13 : 2;
        uint8_t cl12 : 2;
    } block;

    for (int i = 0; i < width * height; i += 16) {
        memcpy(&block, input + i, 16);

        a[0] = block.a0;
        a[1] = block.a1;
        if (block.a0 > block.a1) {
            a[2] = (6 * block.a0 + 1 * block.a1) / 7;
            a[3] = (5 * block.a0 + 2 * block.a1) / 7;
            a[4] = (4 * block.a0 + 3 * block.a1) / 7;
            a[5] = (3 * block.a0 + 4 * block.a1) / 7;
            a[6] = (2 * block.a0 + 5 * block.a1) / 7;
            a[7] = (1 * block.a0 + 6 * block.a1) / 7;
        }
        else {
            a[2] = (4 * block.a0 + 1 * block.a1) / 5;
            a[3] = (3 * block.a0 + 2 * block.a1) / 5;
            a[4] = (2 * block.a0 + 3 * block.a1) / 5;
            a[5] = (1 * block.a0 + 4 * block.a1) / 5;
            a[6] = 0;
            a[7] = 255;
        }

        // This is ugly, retarded and shouldn't be necessary. See above.
        alookup[0] = (block.al & 3584) >> 9;
        alookup[1] = (block.al & 448) >> 6;
        alookup[2] = (block.al & 56) >> 3;
        alookup[3] = (block.al & 7) >> 0;
        alookup[4] = (block.al & 14680064) >> 21;
        alookup[5] = (block.al & 1835008) >> 18;
        alookup[6] = (block.al & 229376) >> 15;
        alookup[7] = (block.al & 28672) >> 12;
        alookup[8] = (block.al & 60129542144) >> 33;
        alookup[9] = (block.al & 7516192768) >> 30;
        alookup[10] = (block.al & 939524096) >> 27;
        alookup[11] = (block.al & 117440512) >> 24;
        alookup[12] = (block.al & 246290604621824) >> 45;
        alookup[13] = (block.al & 30786325577728) >> 42;
        alookup[14] = (block.al & 3848290697216) >> 39;
        alookup[15] = (block.al & 481036337152) >> 36;

        c[0][0] = 255 * ((63488 & block.c0) >> 11) / 31;
        c[0][1] = 255 * ((2016 & block.c0) >> 5) / 63;
        c[0][2] = 255 * (31 & block.c0) / 31;
        c[1][0] = 255 * ((63488 & block.c1) >> 11) / 31;
        c[1][1] = 255 * ((2016 & block.c1) >> 5) / 63;
        c[1][2] = 255 * (31 & block.c1) / 31;
        c[2][0] = (2 * c[0][0] + 1 * c[1][0]) / 3;
        c[2][1] = (2 * c[0][1] + 1 * c[1][1]) / 3;
        c[2][2] = (2 * c[0][2] + 1 * c[1][2]) / 3;
        c[3][0] = (1 * c[0][0] + 2 * c[1][0]) / 3;
        c[3][1] = (1 * c[0][1] + 2 * c[1][1]) / 3;
        c[3][2] = (1 * c[0][2] + 2 * c[1][2]) / 3;

        clookup[0] = block.cl0;
        clookup[1] = block.cl1;
        clookup[2] = block.cl2;
        clookup[3] = block.cl3;
        clookup[4] = block.cl4;
        clookup[5] = block.cl5;
        clookup[6] = block.cl6;
        clookup[7] = block.cl7;
        clookup[8] = block.cl8;
        clookup[9] = block.cl9;
        clookup[10] = block.cl10;
        clookup[11] = block.cl11;
        clookup[12] = block.cl12;
        clookup[13] = block.cl13;
        clookup[14] = block.cl14;
        clookup[15] = block.cl15;

        for (int j = 0; j < 16; j++) {
            x = ((i / 16) % (width / 4)) * 4 + 3 - (j % 4);
            y = ((i / 16) / (width / 4)) * 4 + (j / 4);
            index = (y * width + x) * 4;
            *(output + index + 0) = c[clookup[j]][0];
            *(output + index + 1) = c[clookup[j]][1];
            *(output + index + 2) = c[clookup[j]][2];
            *(output + index + 3) = a[alookup[j]];
        }
    }
}

int calculate_average_color(unsigned char *imgdata, int num_pixels, unsigned char color[4]) {
    uint32_t total_color[4];
    int i;
    int j;

    memset(total_color, 0, 16);

    for (i = 0; i < num_pixels; i++) {
        for (j = 0; j < 4; j++)
            total_color[j] += (uint32_t)imgdata[i * 4 + j];
    }

    for (i = 0; i < 4; i++)
        color[i] = (unsigned char)(total_color[i ^ 2] / num_pixels);

    return 0;
}

int calculate_maximum_color(unsigned char *imgdata, int num_pixels, unsigned char color[4]) {
    int i;
    int j;

    memset(color, 0, 4);

    for (i = 0; i < num_pixels; i++) {
        for (j = 0; j < 4; j++) {
            if (imgdata[(i ^ 2) * 4 + j] > color[j])
                color[j] = imgdata[(i ^ 2) * 4 + j];
        }
    }

    return 0;
}

int PAAConverter::img2paa(std::istream &source, std::ostream &target, Logger& logger, PAAType paatype) {
    /*
     * Converts source image to target PAA.
     *
     * Returns 0 on success and a positive integer on failure.
     */

    extern struct arguments args;

    uint32_t offsets[16];
    uint16_t width;
    int num_channels;
    int w;
    int h;
    int i;
    lzo_uint out_len;
    unsigned char color[4];

    switch (paatype) {
        case PAAType::DXT1:
        case PAAType::DXT5:
            break;
        case PAAType::DXT3:
            logger.error("DXT3 support is not implemented.\n");
            return 4;
        case PAAType::ARGB4444:
            logger.error("ARGB4444 support is not implemented.\n");
            return 4;
        case PAAType::ARGB1555:
            logger.error("ARGB1555 support is not implemented.\n");
            return 4;
        case PAAType::AI88:
            logger.error("AI88 support is not implemented.\n");
            return 4;
        default:
            logger.error("Unrecognized PAA type \"%s\".\n", args.paatype);
            return 4;
    }

    stbi_io_callbacks cbacks = {
    [](void *user, char *data, int size) -> int {//read
        auto& stream = *static_cast<std::istream*>(user);
        return stream.readsome(data, size);
    },
    [](void *user, int n) {//skip
          auto& stream = *static_cast<std::istream*>(user);
          stream.ignore(n);
    },
    [](void *user) -> int {//eof
          auto& stream = *static_cast<std::istream*>(user);
          return stream.eof() ? 1 : 0;
    }
    };


    unsigned char* tmp = stbi_load_from_callbacks(&cbacks, &source, &w, &h, &num_channels, 4);
    if (!tmp) {
        logger.error("Failed to load image.\n");
        return 1;
    }

    width = w;
    uint16_t height = h;

    // Check if alpha channel is necessary
    if (num_channels == 4) {
        num_channels--;
        for (i = 3; i < width * height * 4; i += 4) {
            if (tmp[i] < 0xff) {
                num_channels++;
                break;
            }
        }
    }

    // Unless told otherwise, use DXT5 for alpha stuff and DXT1 for everything else
    if (paatype == PAAType::default) {
        paatype = (num_channels == 4) ? PAAType::DXT5 : PAAType::DXT1;
    }

    if (width % 4 != 0 || height % 4 != 0) {
        logger.error("Dimensions are no multiple of 4.\n");
        stbi_image_free(tmp);
        return 2;
    }

    std::vector<unsigned char> imgdata;
    imgdata.resize(width * height * 4);
    memcpy(imgdata.data(), tmp, width * height * 4);
    stbi_image_free(tmp);


    // Type
    target.write(reinterpret_cast<char*>(&paatype), 2);

    // TAGGs
    target.write("GGATCGVA", 8); //#TODO implement this properly
    target.write("\x04\x00\x00\x00", 4);
    calculate_average_color(imgdata.data(), width * height, color);
    target.write(reinterpret_cast<const char*>(color), sizeof(color));

    target.write("GGATCXAM", 8); //#TODO implement this properly
    target.write("\x04\x00\x00\x00", 4);
    calculate_maximum_color(imgdata.data(), width * height, color);
    target.write(reinterpret_cast<const char*>(color), sizeof(color));

    target.write("GGATSFFO", 8);
    target.write("\x40\x00\x00\x00", 4);
    long fp_offsets = target.tellp();
    memset(offsets, 0, sizeof(offsets));
    target.write(reinterpret_cast<const char*>(offsets), sizeof(offsets));

    // Palette
    target.write("\x00\x00", 2);

    // MipMaps
    for (i = 0; i < 15; i++) {
        uint32_t datalen = width * height;
        if (paatype == PAAType::DXT1)
            datalen /= 2;

        std::vector<unsigned char> outputdata;
        outputdata.resize(datalen);

        // Convert to output format
        switch (paatype) {
            case PAAType::DXT1:
                img2dxt1(imgdata.data(), outputdata.data(), width, height);
                break;
            case PAAType::DXT5:
                img2dxt5(imgdata.data(), outputdata.data(), width, height);
                break;
            default:
                return 5;
        }

        // LZO compression
        bool compressed = args.compress && datalen > LZO1X_MEM_COMPRESS;

        if (compressed) {
            auto tmpCopy = outputdata;

            std::vector<unsigned char> LZOWorkMem;
            LZOWorkMem.resize(LZO1X_MEM_COMPRESS);

            lzo_uint in_len = datalen;

            if (lzo_init() != LZO_E_OK) {
                logger.error("Failed to initialize LZO for compression.\n");
                return 6;
	    }
            if (lzo1x_1_compress(tmpCopy.data(), in_len, outputdata.data(), &out_len, LZOWorkMem.data()) != LZO_E_OK) {
                logger.error("Failed to compress image data.\n");
                return 6;
            }

            datalen = out_len;
        }

        // Write to file
        offsets[i] = target.tellp();
        if (compressed)
            width += 32768;
        target.write(reinterpret_cast<char*>(&width), sizeof(width));
        if (compressed)
            width -= 32768;
        target.write(reinterpret_cast<char*>(height), sizeof(height));
        target.write(reinterpret_cast<char*>(datalen), 3);
        target.write(reinterpret_cast<char*>(outputdata.data()), datalen);

        // Resize image for next MipMap
        width /= 2;
        height /= 2;

        if (width < 4 || height < 4) { break; }

        auto tmpCpy = imgdata;
        if (!stbir_resize_uint8(imgdata.data(), width * 2, height * 2, 0, tmpCpy.data(), width, height, 0, 4)) {
            logger.error("Failed to resize image.\n");
            return 7;
        }
        imgdata = tmpCpy;
    }

    offsets[i] = target.tellp();
    target.write("\x00\x00\x00\x00", 4);

    target.write("\x00\x00", 2);

    // Update offsets
    target.seekp(fp_offsets);
    target.write(reinterpret_cast<const char*>(offsets), sizeof(offsets));

    return 0;
}

int PAAConverter::paa2img(std::istream &source, std::ostream &target, Logger& logger) {
    /*
     * Converts PAA to PNG.
     *
     * Returns 0 on success and a positive integer on failure.
     */


    PAAFile file;
    file.readHeaders(source);
    if (file.convertMipmap(source, target, logger)) { //Convert top-most mipmap
        return 0;
    } else {
        return 1;
    }



}

int PAAConverter::cmd_img2paa(Logger& logger) {
    extern struct arguments args;

    if (args.num_positionals != 3)
        return 128;

    // check if target already exists
    if (std::filesystem::exists(args.positionals[2]) && !args.force) {
        logger.error("File %s already exists and --force was not set.\n", args.positionals[2]);
        return 1;
    }

    std::ifstream input(args.positionals[1], std::ifstream::in | std::ifstream::binary);
    std::ofstream output(args.positionals[2], std::ifstream::out | std::ifstream::binary);

    auto paatype = typeFromString(args.paatype);
    if (paatype == PAAType::invalid) {
        logger.error("Unrecognized PAA type \"%s\".\n", args.paatype);
        return 4;
    }
        

    return img2paa(input, output, logger);
}


int PAAConverter::cmd_paa2img(Logger& logger) {
    extern struct arguments args;

    if (args.num_positionals != 3)
        return 128;

    // check if target already exists
    if (std::filesystem::exists(args.positionals[2]) && !args.force) {
        logger.error("File %s already exists and --force was not set.\n", args.positionals[2]);
        return 1;
    }

    std::ifstream input(args.positionals[1], std::ifstream::in | std::ifstream::binary);
    std::ofstream output(args.positionals[2], std::ifstream::out | std::ifstream::binary);

    return paa2img(input, output, logger);
}
