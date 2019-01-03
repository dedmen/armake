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
#include "utils.h"
#include <bitset>

struct shader_ref {
    uint32_t id;
    std::string_view name;
};

enum class TextureFilter : uint32_t {
    Point,
    Linear,
    Trilinear,
    Anizotropic,
    Anizotropic2,
    Anizotropic4,
    Anizotropic8,
    Anizotropic16,
};

inline constexpr std::array<std::pair<TextureFilter, std::string_view>, 8> TextureFilterToName {
    std::pair<TextureFilter, std::string_view>

    {TextureFilter::Point, "Point"},
    {TextureFilter::Linear, "Linear"},
    {TextureFilter::Trilinear, "Trilinear"},
    {TextureFilter::Anizotropic, "Anizotropic"},
    {TextureFilter::Anizotropic2, "Anizotropic2"},
    {TextureFilter::Anizotropic4, "Anizotropic4"},
    {TextureFilter::Anizotropic8, "Anizotropic8"},
    {TextureFilter::Anizotropic16, "Anizotropic16"}
};


struct stage_texture {
    TextureFilter texture_filter { TextureFilter::Anizotropic };
    std::string path;
    uint32_t transform_index { 0 };
    bool useWorldEnvMap { false };
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

inline constexpr std::array<std::pair<uint8_t,std::string_view>,13> IndexToRenderFlag{
    std::pair<uint8_t, std::string_view>
    {0, "AlwaysInShadow"},
    {1, "NoZWrite"},
    {2, "LandShadow"},
    {3, "Dummy0"},
    {4, "NoColorWrite"},
    {5, "NoAlphaWrite"},
    {6, "AddBlend"},
    {7, "AlphaTest32"},
    {8, "AlphaTest64"},
    {9, "AlphaTest128"},
    {10, "Road"},
    {11, "NoTiWrite"},
    {12, "NoReceiveShadow"}
};

enum class FogMode : uint32_t {
    None,
    Fog,        /// Ordinary fog 
    Alpha,      /// Fog as alpha
    FogAlpha,   /// Fog as both alpha and fog
    FogSky      /// Fog for sky objects (moon, stars)
};

inline constexpr std::array<std::pair<FogMode, std::string_view>, 8> FogModeToName{
    std::pair<FogMode, std::string_view>

    {FogMode::None, "None"},
    {FogMode::Fog, "Fog"},
    {FogMode::Alpha, "Alpha"},
    {FogMode::FogAlpha, "FogAlpha"},
    {FogMode::FogSky, "FogSky"},
};

enum class LightMode : uint32_t {//https://community.bistudio.com/wiki/RVMAT_basics#Light_Mode
    None,
    Sun,
    Sky,
    Horizon,
    Stars,
    SunObject,
    SunHaloObject,
    MoonObject,
    MoonHaloObject,
};

inline constexpr std::array<std::pair<LightMode, std::string_view>, 9> LightModeToName{
    std::pair<LightMode, std::string_view>

    {LightMode::None, "None"},
    {LightMode::Sun, "Sun"},
    {LightMode::Sky, "Sky"},
    {LightMode::Horizon, "Horizon"},
    {LightMode::Stars, "Stars"},
    {LightMode::SunObject, "SunObject"},
    {LightMode::SunHaloObject, "SunHaloObject"},
    {LightMode::MoonObject, "MoonObject"},
    {LightMode::MoonHaloObject, "MoonHaloObject"}
};


class Material {
    Logger& logger;
public:
    Material(Logger& logger) : logger(logger) {}
    Material(Logger& logger, std::string p) : logger(logger), path(std::move(p)) {}


    std::string path;
    uint32_t type { MATERIALTYPE }; //This is actually version
    ColorFloat emissive{0,0,0,1};
    ColorFloat ambient{1,1,1,1};
    ColorFloat diffuse{ 1,1,1,1 };
    ColorFloat forced_diffuse{ 0,0,0,1 };
    ColorFloat specular{ 0,0,0,1 };;
    //ColorFloat specular2;
    float specular_power { 0 };
    uint32_t pixelshader_id{0};
    uint32_t vertexshader_id{0};
    LightMode mainLight{ LightMode::Sun };
    FogMode fogMode { FogMode::Fog};
    std::string surface;
    uint32_t depr_3{0};
    std::bitset<32> render_flags;
    uint32_t num_textures{0};
    uint32_t num_transforms{0};
    std::vector<stage_texture> textures;
    std::vector<stage_transform> transforms;
    struct stage_texture dummy_texture;

    int read(); //#TODO support for async reading
    void writeTo(std::ostream& output);
};
