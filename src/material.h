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

#include <string>
#include "logger.h"
#include "matrix.h"

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

enum class uv_source : uint32_t {
    None,
    Tex,
    TexWaterAnim,
    Pos,
    Norm,
    Tex1,
    WorldPos,
    WorldNorm,
    TexShoreAnim
};

inline constexpr std::array<std::pair<uv_source, std::string_view>, 8> uvSourceToName{
    std::pair<uv_source, std::string_view>
    
    {uv_source::None, "None"},
    {uv_source::Tex, "Tex"},
    {uv_source::TexWaterAnim, "TexWaterAnim"},
    {uv_source::Pos, "Pos"},
    {uv_source::Tex1, "Tex1"},
    {uv_source::WorldPos, "WorldPos"},
    {uv_source::WorldNorm, "WorldNorm"},
    {uv_source::TexShoreAnim, "TexShoreAnim"}
};



struct stage_transform {
    uv_source uv_source{ uv_source::Tex };
    matrix4 transform{ identity_matrix4 };

    bool operator==(const stage_transform& o) const {
        return uv_source == o.uv_source &&
            ComparableFloat<std::micro>(transform.m00) == o.transform.m00 &&
            ComparableFloat<std::micro>(transform.m01) == o.transform.m01 &&
            ComparableFloat<std::micro>(transform.m02) == o.transform.m02 &&
            ComparableFloat<std::micro>(transform.m10) == o.transform.m10 &&
            ComparableFloat<std::micro>(transform.m11) == o.transform.m11 &&
            ComparableFloat<std::micro>(transform.m12) == o.transform.m12 &&
            ComparableFloat<std::micro>(transform.m20) == o.transform.m20 &&
            ComparableFloat<std::micro>(transform.m21) == o.transform.m21 &&
            ComparableFloat<std::micro>(transform.m22) == o.transform.m22 &&
            ComparableFloat<std::micro>(transform.m30) == o.transform.m30 &&
            ComparableFloat<std::micro>(transform.m31) == o.transform.m31 &&
            ComparableFloat<std::micro>(transform.m32) == o.transform.m32;
    }

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
