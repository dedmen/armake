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
//#include <unistd.h>
#include <math.h>
#include <fstream>
#include <sstream>

#include "args.h"
#include "filesystem.h"
#include "rapify.h"
#include "preprocess.h"
#include "utils.h"
#include "derapify.h"
#include "matrix.h"
#include "material.h"

//#TODO make these enums and use in p3d.cpp finishLod on shadowMaterial
static constexpr std::array<shader_ref, 153> pixelshaders{
    shader_ref{ 0, "Normal" },
    { 1, "NormalDXTA" },
    { 2, "NormalMap" },
    { 3, "NormalMapThrough" },
    { 4, "NormalMapGrass" },
    { 5, "NormalMapDiffuse" },
    { 6, "Detail" },
    { 7, "Interpolation" },
    { 8, "Water" },
    { 9, "WaterSimple" },
    { 10, "White" },
    { 11, "WhiteAlpha" },
    { 12, "AlphaShadow" },
    { 13, "AlphaNoShadow" },
    { 14, "Dummy0" },
    { 15, "DetailMacroAS" },
    { 16, "NormalMapMacroAS" },
    { 17, "NormalMapDiffuseMacroAS" },
    { 18, "NormalMapSpecularMap" },
    { 19, "NormalMapDetailSpecularMap" },
    { 20, "NormalMapMacroASSpecularMap" },
    { 21, "NormalMapDetailMacroASSpecularMap" },
    { 22, "NormalMapSpecularDIMap" },
    { 23, "NormalMapDetailSpecularDIMap" },
    { 24, "NormalMapMacroASSpecularDIMap" },
    { 25, "NormalMapDetailMacroASSpecularDIMap" },
    { 26, "Terrain1" },
    { 27, "Terrain2" },
    { 28, "Terrain3" },
    { 29, "Terrain4" },
    { 30, "Terrain5" },
    { 31, "Terrain6" },
    { 32, "Terrain7" },
    { 33, "Terrain8" },
    { 34, "Terrain9" },
    { 35, "Terrain10" },
    { 36, "Terrain11" },
    { 37, "Terrain12" },
    { 38, "Terrain13" },
    { 39, "Terrain14" },
    { 40, "Terrain15" },
    { 41, "TerrainSimple1" },
    { 42, "TerrainSimple2" },
    { 43, "TerrainSimple3" },
    { 44, "TerrainSimple4" },
    { 45, "TerrainSimple5" },
    { 46, "TerrainSimple6" },
    { 47, "TerrainSimple7" },
    { 48, "TerrainSimple8" },
    { 49, "TerrainSimple9" },
    { 50, "TerrainSimple10" },
    { 51, "TerrainSimple11" },
    { 52, "TerrainSimple12" },
    { 53, "TerrainSimple13" },
    { 54, "TerrainSimple14" },
    { 55, "TerrainSimple15" },
    { 56, "Glass" },
    { 57, "NonTL" },
    { 58, "NormalMapSpecularThrough" },
    { 59, "Grass" },
    { 60, "NormalMapThroughSimple" },
    { 61, "NormalMapSpecularThroughSimple" },
    { 62, "Road" },
    { 63, "Shore" },
    { 64, "ShoreWet" },
    { 65, "Road2Pass" },
    { 66, "ShoreFoam" },
    { 67, "NonTLFlare" },
    { 68, "NormalMapThroughLowEnd" },
    { 69, "TerrainGrass1" },
    { 70, "TerrainGrass2" },
    { 71, "TerrainGrass3" },
    { 72, "TerrainGrass4" },
    { 73, "TerrainGrass5" },
    { 74, "TerrainGrass6" },
    { 75, "TerrainGrass7" },
    { 76, "TerrainGrass8" },
    { 77, "TerrainGrass9" },
    { 78, "TerrainGrass10" },
    { 79, "TerrainGrass11" },
    { 80, "TerrainGrass12" },
    { 81, "TerrainGrass13" },
    { 82, "TerrainGrass14" },
    { 83, "TerrainGrass15" },
    { 84, "Crater1" },
    { 85, "Crater2" },
    { 86, "Crater3" },
    { 87, "Crater4" },
    { 88, "Crater5" },
    { 89, "Crater6" },
    { 90, "Crater7" },
    { 91, "Crater8" },
    { 92, "Crater9" },
    { 93, "Crater10" },
    { 94, "Crater11" },
    { 95, "Crater12" },
    { 96, "Crater13" },
    { 97, "Crater14" },
    { 98, "Sprite" },
    { 99, "SpriteSimple" },
    { 100, "Cloud" },
    { 101, "Horizon" },
    { 102, "Super" },
    { 103, "Multi" },
    { 104, "TerrainX" },
    { 105, "TerrainSimpleX" },
    { 106, "TerrainGrassX" },
    { 107, "Tree" },
    { 108, "TreePRT" },
    { 109, "TreeSimple" },
    { 110, "Skin" },
    { 111, "CalmWater" },
    { 112, "TreeAToC" },
    { 113, "GrassAToC" },
    { 114, "TreeAdv" },
    { 115, "TreeAdvSimple" },
    { 116, "TreeAdvTrunk" },
    { 117, "TreeAdvTrunkSimple" },
    { 118, "TreeAdvAToC" },
    { 119, "TreeAdvSimpleAToC" },
    { 120, "TreeSN" },
    { 121, "SpriteExtTi" },
    { 122, "TerrainSNX" },
    { 123, "InterpolationAlpha" },
    { 124, "VolCloud" },
    { 125, "VolCloudSimple" },
    { 126, "UnderwaterOcclusion" },
    { 127, "SimulWeatherClouds" },
    { 128, "SimulWeatherCloudsWithLightning" },
    { 129, "SimulWeatherCloudsCPU" },
    { 130, "SimulWeatherCloudsWithLightningCPU" },
    { 131, "SuperExt" },
    { 132, "SuperHair" },
    { 133, "SuperHairAtoC" },
    { 134, "Caustics" },
    { 135, "Refract" },
    { 136, "SpriteRefract" },
    { 137, "SpriteRefractSimple" },
    { 138, "SuperAToC" },
    { 139, "NonTLFlareNew" },
    { 140, "NonTLFlareLight" },
    { 141, "TerrainNoDetailX" },
    { 142, "TerrainNoDetailSNX" },
    { 143, "TerrainSimpleSNX" },
    { 144, "NormalPiP" },
    { 145, "NonTLFlareNewNoOcclusion" },
    { 146, "Empty" },
    { 147, "Point" },
    { 148, "TreeAdvTrans"  },
    { 149, "TreeAdvTransAToC" },
    { 150, "Collimator" },
    { 151, "LODDiag" },
    { 152, "DepthOnly" }
};

static constexpr std::array<shader_ref, 45> vertexshaders{
    shader_ref{ 0, "Basic" },
    { 1, "NormalMap" },
    { 2, "NormalMapDiffuse" },
    { 3, "Grass" },
    { 4, "Dummy2" },
    { 5, "Dummy3" },
    { 6, "ShadowVolume" },
    { 7, "Water" },
    { 8, "WaterSimple" },
    { 9, "Sprite" },
    { 10, "Point" },
    { 11, "NormalMapThrough" },
    { 12, "Dummy3" },
    { 13, "Terrain" },
    { 14, "BasicAS" },
    { 15, "NormalMapAS" },
    { 16, "NormalMapDiffuseAS" },
    { 17, "Glass" },
    { 18, "NormalMapSpecularThrough" },
    { 19, "NormalMapThroughNoFade" },
    { 20, "NormalMapSpecularThroughNoFade" },
    { 21, "Shore" },
    { 22, "TerrainGrass" },
    { 23, "Super" },
    { 24, "Multi" },
    { 25, "Tree" },
    { 26, "TreeNoFade" },
    { 27, "TreePRT" },
    { 28, "TreePRTNoFade" },
    { 29, "Skin" },
    { 30, "CalmWater" },
    { 31, "TreeAdv" },
    { 32, "TreeAdvTrunk" },
    { 33, "VolCloud" },
    { 34, "Road" },
    { 35, "UnderwaterOcclusion" },
    { 36, "SimulWeatherClouds" },
    { 37, "SimulWeatherCloudsCPU" },
    { 38, "SpriteOnSurface" },
    { 39, "TreeAdvModNormals" },
    { 40, "Refract" },
    { 41, "SimulWeatherCloudsGS" },
    { 42, "BasicFade" },
    { 43, "Star" },
    { 44, "TreeAdvNoFade" },
};


int Material::read() {
    /*
     * Reads the material information for the given material struct.
     * Returns 0 on success and a positive integer on failure.
     */

    extern std::string current_target; //#TODO take current_target as parameter from p3d
    const ColorFloat default_color = { 0.0f, 0.0f, 0.0f, 1.0f };

    // Write default values
    emissive = default_color;
    ambient = default_color;
    diffuse = default_color;
    forced_diffuse = default_color;
    specular = default_color;
    specular_power = 1.0f;
    pixelshader_id = 0;
    vertexshader_id = 0;
    num_textures = 1;
    num_transforms = 1;

    textures.resize(num_textures);
    transforms.resize(num_transforms);

    std::string readPath = path; //need to copy cuz `path` is used to check if a material already exists, we cannot change that
    if (readPath[0] != '\\') {
        readPath.insert(readPath.begin(), '\\');
    }
    auto foundFile = find_file(readPath, "");
    if (!foundFile) {
        logger.warning(current_target, 0u, "Failed to find material \"%s\".\n", path.c_str());
        return 1;
    }

    std::ifstream rvmatInput(foundFile->string(), std::ifstream::in | std::ifstream::binary);
    if (!rvmatInput.is_open()) {
        logger.warning(current_target, 0u, "Failed to open material \"%s\".\n", path.c_str());
        return 1;
    }

    Config cfg;
    if (Rapifier::isRapified(rvmatInput)) {
        cfg = Config::fromBinarized(rvmatInput, logger);
    } else {
        Preprocessor p(logger);
        std::stringstream buf;
        p.preprocess(foundFile->string(), rvmatInput, buf, Preprocessor::ConstantMapType());
        buf.seekg(0);
        cfg = Config::fromPreprocessedText(buf, p.getLineref(), logger);
    }

#define TRY_READ_ARRAY(tgt, src) {auto x = cfg->getArrayOfFloats({ #src }); if (!x.empty()) tgt = x;}

    // Read colors
    TRY_READ_ARRAY(emissive, emmisive);// "Did you mean: emissive?"
    TRY_READ_ARRAY(ambient, ambient);
    TRY_READ_ARRAY(diffuse, diffuse);
    TRY_READ_ARRAY(forced_diffuse, forcedDiffuse);
    TRY_READ_ARRAY(specular, specular);

    {auto x = cfg->getFloat({ "specularPower" }); if (x) specular_power = *x; }


    auto cfg_renderFlagsArray = cfg->getArrayOfStringViews({ "renderFlags" });
    if (cfg_renderFlagsArray) {
        for (auto& flag : *cfg_renderFlagsArray) {
            auto found = std::find_if(IndexToRenderFlag.begin(), IndexToRenderFlag.end(), [&flag](const auto& it) {
                return iequals(it.second, flag);
            });
            if (found == IndexToRenderFlag.end()) {
                logger.warning(path, -1, "Unrecognized render flag: \"%.*s\".\n", static_cast<int>(flag.size()), flag.data());
                continue;
            }
            render_flags.set(found->first, true);
        }
    }

    auto cfg_surfaceInfo = cfg->getString({ "surfaceInfo" });
    if (cfg_surfaceInfo) {
        surface = *cfg_surfaceInfo;
    }
    //#TODO move enum resolution to template or macro
    auto cfg_mainLight = cfg->getString({ "mainLight" });
    if (cfg_mainLight) {

        auto found = std::find_if(LightModeToName.begin(), LightModeToName.end(), [&searchName = *cfg_mainLight](const auto& it) {
            return iequals(it.second, searchName);
        });
        if (found == LightModeToName.end()) {
            logger.warning(path, -1, "Unrecognized LightMode: \"%s\".\n", cfg_mainLight->c_str());
        } else
            mainLight = found->first;
    }

    auto cfg_fogMode = cfg->getString({ "fogMode" });
    if (cfg_fogMode) {

        auto found = std::find_if(FogModeToName.begin(), FogModeToName.end(), [&searchName = *cfg_fogMode](const auto& it) {
            return iequals(it.second, searchName);
        });
        if (found == FogModeToName.end()) {
            logger.warning(path, -1, "Unrecognized FogMode: \"%s\".\n", cfg_fogMode->c_str());
        } else
            fogMode = found->first;
    }


    // Read shaders
    auto pixelShaderID = cfg->getString({ "PixelShaderID" });
    if (pixelShaderID) {

        auto found = std::find_if(pixelshaders.begin(), pixelshaders.end(),[&searchName = *pixelShaderID](const auto& sha) {
            return iequals(sha.name, searchName);
        });
        if (found == pixelshaders.end()) {
            logger.warning(path, -1, "Unrecognized pixel shader: \"%s\", assuming \"Normal\".\n", pixelShaderID->c_str());
            found = pixelshaders.begin(); //normal is always at front
        }

        pixelshader_id = found->id;
    }

    auto VertexShaderID = cfg->getString({ "VertexShaderID" });
    if (VertexShaderID) {
        auto found = std::find_if(vertexshaders.begin(), vertexshaders.end(), [&searchName = *VertexShaderID](const auto& sha) {
            return iequals(sha.name, searchName);
        });

        if (found == vertexshaders.end()) {
            logger.warning(path, -1, "Unrecognized vertex shader: \"%s\", assuming \"Basic\".\n", VertexShaderID->c_str());
            found = vertexshaders.begin(); //normal is always at front
        }

        vertexshader_id = found->id;
    }

    // Read stages
    for (int i = 1; i < MAXSTAGES; i++) {
        if (!cfg->getString({ "Stage" + std::to_string(i), "texture" }))
            break;
        num_textures++;
        num_transforms++;
    }
    textures.resize(num_textures);
    transforms.resize(0);

    for (uint32_t i = 0u; i < num_textures; i++) { //#TODO ranged for
        if (i == 0) {
            textures[i].path[0] = 0;
            textures[i].transform_index = i;
            continue;
        }

        auto stageCfg = cfg->getClass({ "Stage" + std::to_string(i) });

        auto texture = stageCfg->getString({"texture"});
        textures[i].path = *texture;
        textures[i].transform_index = i;

        auto cfg_Filter = stageCfg->getString({ "Filter" });
        if (cfg_Filter) {

            auto found = std::find_if(TextureFilterToName.begin(), TextureFilterToName.end(), [&searchName = *cfg_Filter](const auto& it) {
                return iequals(it.second, searchName);
            });
            if (found == TextureFilterToName.end()) {
                logger.warning(path, -1, "Unrecognized TextureFilter: \"%s\" in Stage%u.\n", cfg_Filter->c_str(), i);
            } else
                textures[i].texture_filter = found->first;
        }


        uint8_t texGen = 0xFF;
        auto cfg_texGen = stageCfg->getFloat({ "TexGen" });
        if (cfg_texGen) {
            texGen = *cfg_texGen;
        } else {
            auto cfg_texGenString = stageCfg->getString({ "texGen" });
            if (cfg_texGenString) {
                logger.warning(path, 0, "TexGen is String, should be Number! Stage%u.\n", i);
                texGen = std::stoi(*cfg_texGenString);
            }
        }
        auto transformCfg = stageCfg;

        if (texGen != 0xFF) {
            if (texGen > 8) {
                logger.warning(path, 0, "TexGen too big! Stage%u tried to get texGen%u.\n", i, texGen);
                return 1;
            }
            auto cfg_texGenClass = cfg->getClass({ "TexGen" + std::to_string(texGen) });
            if (!cfg_texGenClass) {
                logger.warning(path, 0, "TexGen not found! Stage%u tried to get texGen%u.\n", i, texGen);
                return 1;
            }
            transformCfg = cfg_texGenClass;
        }

        stage_transform stageTrans;


        auto cfg_uvSourceNumber = transformCfg->getFloat({ "uvSource" });

        if (cfg_uvSourceNumber) {
            stageTrans.uv_source = static_cast<uv_source>(static_cast<uint32_t>(*cfg_uvSourceNumber));
        } else {
            auto cfg_uvSourceString = transformCfg->getString({ "uvSource" });

            if (cfg_uvSourceString) {
                auto found = std::find_if(uvSourceToName.begin(), uvSourceToName.end(), [&name = *cfg_uvSourceString](const auto& it){
                    return iequals(it.second, name);
                });

                if (found == uvSourceToName.end()) {
                    logger.warning(path, 0, "Invalid uvSource in Stage%u\n", i);
                } else {
                    stageTrans.uv_source = found->first;
                }
            }
        }

        auto uvTransform = transformCfg->getClass({ "uvTransform" });
        if (uvTransform) {
            auto aside = uvTransform->getArrayOfFloats({ "aside" });
            if (!aside.empty()) { //#TODO verify that arrays are min size 3
                stageTrans.transform.m00 = aside[0]; //#TODO make this easier use a actual matrix. And then read the parts as vectors using a vector<float> constructor
                stageTrans.transform.m01 = aside[1];
                stageTrans.transform.m02 = aside[2];
            }
            auto up = uvTransform->getArrayOfFloats({ "up" });
            if (!up.empty()) {
                stageTrans.transform.m10 = up[0];
                stageTrans.transform.m11 = up[1];
                stageTrans.transform.m12 = up[2];
            }
            auto dir = uvTransform->getArrayOfFloats({ "dir" });
            if (!dir.empty()) {
                stageTrans.transform.m20 = dir[0];
                stageTrans.transform.m21 = dir[1];
                stageTrans.transform.m22 = dir[2];
            }
            auto pos = uvTransform->getArrayOfFloats({ "pos" });
            if (!pos.empty()) {
                stageTrans.transform.m30 = pos[0];
                stageTrans.transform.m31 = pos[1];
                stageTrans.transform.m32 = pos[2];
            }
        }

        auto found = std::find(transforms.begin(), transforms.end(), stageTrans);
        if (found == transforms.end()) {
            transforms.emplace_back(stageTrans);
            textures[i].transform_index = transforms.size() - 1;
        } else
            textures[i].transform_index = found - transforms.begin();

    }
    num_transforms = transforms.size();

    if (num_transforms > 8) {
        logger.error(path, 0, "Too many texGen's! Trying to use %u out of maximum 8.\n", num_transforms);
        return 1;
    }

    auto texture = cfg->getString({ "StageTi", "texture" });
    if (texture)
        dummy_texture.path = *texture;

    return 0;
}


void Material::writeTo(std::ostream& output) {

#define WRITE_CASTED(elem, size)  output.write(reinterpret_cast<char*>(&elem), size);

    output.write(path.c_str(), path.length() + 1);
    WRITE_CASTED(type, sizeof(uint32_t));
    WRITE_CASTED(emissive, sizeof(ColorFloat));
    WRITE_CASTED(ambient, sizeof(ColorFloat));
    WRITE_CASTED(diffuse, sizeof(ColorFloat));
    WRITE_CASTED(forced_diffuse, sizeof(ColorFloat));
    WRITE_CASTED(specular, sizeof(ColorFloat));
    WRITE_CASTED(specular, sizeof(ColorFloat));//Yes.. This is literally correct. No idea WTF Arma is doing, seems to be a bug.
    
    
    //WRITE_CASTED(specular2, sizeof(ColorFloat));
    WRITE_CASTED(specular_power, sizeof(float));
    WRITE_CASTED(pixelshader_id, sizeof(uint32_t));
    WRITE_CASTED(vertexshader_id, sizeof(uint32_t));
    WRITE_CASTED(mainLight, sizeof(uint32_t));
    WRITE_CASTED(fogMode, sizeof(uint32_t));


    output.write(surface.c_str(), surface.length() + 1);

    if (render_flags.none()) { //We can just omit render flags if they are all false
        uint32_t renderFlagsSize = 0;
        WRITE_CASTED(renderFlagsSize, sizeof(uint32_t)); //render flags size. number of int's to store render flags
    } else {
        auto renderFlagsSize = render_flags.size()/32;
        WRITE_CASTED(renderFlagsSize, sizeof(uint32_t)); //render flags size. number of int's to store render flags
        uint32_t renderFlags = render_flags.to_ulong();
        WRITE_CASTED(renderFlags, sizeof(uint32_t));
    }


    if (num_transforms > 8) {
        logger.error("Too many texGen's being used in RVMAT! Armake will clip excess off and cause weird behaviour! %s\n", path.c_str());

        num_transforms = 8;
        transforms.resize(8);

        for (auto& it : textures) {
            if (it.transform_index > 7) it.transform_index = 7;
        }
    }


    WRITE_CASTED(num_textures, sizeof(uint32_t));
    WRITE_CASTED(num_transforms, sizeof(uint32_t));

    for (int i = 0; i < num_textures; i++) {
        WRITE_CASTED(textures[i].texture_filter, sizeof(uint32_t));
        output.write(textures[i].path.c_str(), textures[i].path.length() + 1);
        WRITE_CASTED(textures[i].transform_index, sizeof(uint32_t));
        WRITE_CASTED(textures[i].useWorldEnvMap, sizeof(bool)); //world env map. Added in version 11.
    }



    //#TODO properly save them by iterating?
    output.write(reinterpret_cast<char*>(transforms.data()), sizeof(struct stage_transform) * num_transforms);

    WRITE_CASTED(dummy_texture.texture_filter, sizeof(uint32_t));

    //TI stage. Added in version 11
    output.write(dummy_texture.path.c_str(), dummy_texture.path.length() + 1);
    WRITE_CASTED(dummy_texture.transform_index, sizeof(uint32_t)); //tex gen. doesn't matter for ti
    WRITE_CASTED(dummy_texture.useWorldEnvMap, sizeof(bool)); //useWorldEnvMap grab from config entry "useWorldEnvMap"

    //#MinorTask we can save as version 10 instead and omit TI stage and the worldEnvMap to save a couple bytes filesize

}
