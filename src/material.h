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


#define MATERIALTYPE 11
#define MAXSTAGES 16


#include "utils.h"
#include <string>
#include "logger.h"

struct shader_ref {
    uint32_t id;
    std::string_view name;
};

struct color {
    color() {}
    color(std::vector<float> vec): r(vec[0]), g(vec[1]), b(vec[2]), a(vec[3]) {}
    color(float r, float g, float b, float a): r(r), g(g), b(b), a(a) {}
    float r;
    float g;
    float b;
    float a;
};

struct stage_texture {
    uint32_t texture_filter;
    std::string path;
    uint32_t transform_index;
    bool type11_bool;
};

struct stage_transform {
    uint32_t uv_source;
    float transform[4][3];
};

class Material {
    Logger& logger;
public:
    Material(Logger& logger) : logger(logger) {}
    Material(Logger& logger, std::string p) : logger(logger), path(std::move(p)) {}


    std::string path;
    uint32_t type{0};
    struct color emissive;
    struct color ambient;
    struct color diffuse;
    struct color forced_diffuse;
    struct color specular;
    struct color specular2;
    float specular_power;
    uint32_t pixelshader_id{0};
    uint32_t vertexshader_id{0};
    uint32_t depr_1{0};
    uint32_t depr_2{0};
    std::string surface;
    uint32_t depr_3{0};
    uint32_t render_flags{0};
    uint32_t num_textures{0};
    uint32_t num_transforms{0};
    std::vector<stage_texture> textures;
    std::vector<stage_transform> transforms;
    struct stage_texture dummy_texture;

    int read();
    void writeTo(std::ostream& output);
};
