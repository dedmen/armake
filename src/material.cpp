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


const struct shader_ref pixelshaders[153] = {
    { 0, "Normal" },
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

const struct shader_ref vertexshaders[45] = {
    { 0, "Basic" },
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

    extern std::string current_target;
    char temp[2048];
    int i;
    const struct color default_color = { 0.0f, 0.0f, 0.0f, 1.0f };

    if (path[0] != '\\') {
        strcpy(temp, "\\");//#TODO use std::filesystem::path
        strcat(temp, path.c_str());
    } else {
        strcpy(temp, path.c_str());
    }

    // Write default values
    type = MATERIALTYPE;
    depr_1 = 1;
    depr_2 = 1;
    depr_3 = 1;
    emissive = default_color;
    ambient = default_color;
    diffuse = default_color;
    forced_diffuse = default_color;
    specular = default_color;
    specular2 = default_color;
    specular_power = 1.0f;
    pixelshader_id = 0;
    vertexshader_id = 0;
    num_textures = 1;
    num_transforms = 1;

    textures.resize(num_textures);
    transforms.resize(num_transforms);
    
    textures[0].path[0] = 0;
    textures[0].texture_filter = 3;
    textures[0].transform_index = 0;
    textures[0].type11_bool = 0;
    
    transforms[0].uv_source = 1;
    memset(transforms[0].transform, 0, 12 * sizeof(float));
    memcpy(transforms[0].transform, &identity_matrix, sizeof(identity_matrix));

    dummy_texture.path[0] = 0;
    dummy_texture.texture_filter = 3;
    dummy_texture.transform_index = 0;
    dummy_texture.type11_bool = 0;

    auto foundFile = find_file(temp, "");
    if (!foundFile) {
        logger.warning(current_target, 0u, "Failed to find material \"%s\".\n", temp);
        return 1;
    }

    current_target = temp;

    ;

    Preprocessor p(logger);
    std::stringstream buf;
    p.preprocess(foundFile->string(), std::ifstream(foundFile->string(), std::ifstream::in | std::ifstream::binary), buf, Preprocessor::ConstantMapType());
    buf.seekg(0);
    auto cfg = Config::fromPreprocessedText(buf, p.getLineref(), logger);

#define TRY_READ_ARRAY(tgt, src) {auto x = cfg->getArrayOfFloats({ #src }); if (!x.empty()) tgt = x;}

    current_target = path.c_str();
    // Read colors
    TRY_READ_ARRAY(emissive, emmisive);// "Did you mean: emissive?"
    TRY_READ_ARRAY(ambient, ambient);
    TRY_READ_ARRAY(diffuse, diffuse);
    TRY_READ_ARRAY(forced_diffuse, forcedDiffuse);
    TRY_READ_ARRAY(specular2 = specular, specular);

    {auto x = cfg->getFloat({ "specularPower" }); if (x) specular_power = *x; }

    // Read shaders
    auto pixelShaderID = cfg->getString({ "PixelShaderID" });
    if (pixelShaderID) {
        for (i = 0; i < sizeof(pixelshaders) / sizeof(struct shader_ref); i++) {
            if (pixelshaders[i].name == *pixelShaderID)
                break;
        }
        if (i == sizeof(pixelshaders) / sizeof(struct shader_ref)) {
            logger.warning(current_target, -1, "Unrecognized pixel shader: \"%s\", assuming \"Normal\".\n", *pixelShaderID);
            i = 0;
        }
        pixelshader_id = pixelshaders[i].id;
    }

    auto VertexShaderID = cfg->getString({ "VertexShaderID" });
    if (VertexShaderID) {
        for (i = 0; i < sizeof(vertexshaders) / sizeof(struct shader_ref); i++) {
            if (vertexshaders[i].name == *VertexShaderID)
                break;
        }
        if (i == sizeof(vertexshaders) / sizeof(struct shader_ref)) {
            logger.warning(current_target, -1, "Unrecognized vertex shader: \"%s\", assuming \"Basic\".\n", *VertexShaderID);
            i = 0;
        }
        vertexshader_id = vertexshaders[i].id;
    }

    // Read stages
    for (i = 1; i < MAXSTAGES; i++) {
        if (!cfg->getString({ "Stage" + std::to_string(i), "texture" }))
            break;
        num_textures++;
        num_transforms++;
    }
    textures.resize(num_textures);
    transforms.resize(num_transforms);

    for (i = 0; i < num_textures; i++) { //#TODO ranged for
        if (i == 0) {
            textures[i].path[0] = 0;
        } else {
            auto texture = cfg->getString({ "Stage" + std::to_string(i), "texture" });
            textures[i].path = *texture;
        }

        textures[i].texture_filter = 3;
        textures[i].transform_index = i;
        textures[i].type11_bool = 0;

        transforms[i].uv_source = 1;
        //#TODO transforms uv source enum
//    XX(type, prefix, None) \
    //    XX(type, prefix, Tex) \
    //    XX(type, prefix, TexWaterAnim) \
    //    XX(type, prefix, Pos) \
    //    XX(type, prefix, Norm) \
    //    XX(type, prefix, Tex1) \
    //    XX(type, prefix, WorldPos) \
    //    XX(type, prefix, WorldNorm) \
    //    XX(type, prefix, TexShoreAnim) \


        memset(transforms[i].transform, 0, 12 * sizeof(float));
        memcpy(transforms[i].transform, &identity_matrix, sizeof(identity_matrix));

        if (i != 0) {
            //#TODO retrieve uvTransform entry. So we don't re-resolve the whole path everytime

            auto aside = cfg->getArrayOfFloats({ "Stage" + std::to_string(i), "uvTransform", "aside" });
            if (!aside.empty()) {
                transforms[i].transform[0][0] = aside[0]; //#TODO make this easier use a actual matrix. And then read the parts as vectors using a vector<float> constructor
                transforms[i].transform[0][1] = aside[1];
                transforms[i].transform[0][2] = aside[2];
            }
            auto up = cfg->getArrayOfFloats({ "Stage" + std::to_string(i), "uvTransform", "up" });
            if (!up.empty()) {
                transforms[i].transform[1][0] = up[0];
                transforms[i].transform[1][1] = up[1];
                transforms[i].transform[1][2] = up[2];
            }
            auto dir = cfg->getArrayOfFloats({ "Stage" + std::to_string(i), "uvTransform", "dir" });
            if (!dir.empty()) {
                transforms[i].transform[2][0] = dir[0];
                transforms[i].transform[2][1] = dir[1];
                transforms[i].transform[2][2] = dir[2];
            }
            auto pos = cfg->getArrayOfFloats({ "Stage" + std::to_string(i), "uvTransform", "pos" });
            if (!pos.empty()) {
                transforms[i].transform[3][0] = pos[0];
                transforms[i].transform[3][1] = pos[1];
                transforms[i].transform[3][2] = pos[2];
            }
        }
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
    WRITE_CASTED(emissive, sizeof(struct color));
    WRITE_CASTED(ambient, sizeof(struct color));
    WRITE_CASTED(diffuse, sizeof(struct color));
    WRITE_CASTED(forced_diffuse, sizeof(struct color));
    WRITE_CASTED(specular, sizeof(struct color));
    WRITE_CASTED(specular2, sizeof(struct color));
    WRITE_CASTED(specular_power, sizeof(float));
    WRITE_CASTED(pixelshader_id, sizeof(uint32_t));
    WRITE_CASTED(vertexshader_id, sizeof(uint32_t));
    WRITE_CASTED(depr_1, sizeof(uint32_t)); //mainlight
    /**
     *enum
      None,          
      Sun,           
      Sky,           
      Horizon,       
      Stars,         
      SunObject,     
      SunHaloObject, 
      MoonObject,    
      MoonHaloObject,
     */
    //https://community.bistudio.com/wiki/RVMAT_basics#Light_Mode

    WRITE_CASTED(depr_2, sizeof(uint32_t)); //fogmode
    //#TODO this and mainLight come from rvmat entry
    /*
     enum
        /// No fog is being used 
        //None
        /// Ordinary fog 
        //Fog
        /// Fog as alpha
        //Alpha
        /// Fog as both alpha and fog
        //FogAlpha
        /// Fog for sky objects (moon, stars)
        //FogSky
     */


    output.write(surface.c_str(), surface.length() + 1);
    WRITE_CASTED(depr_3, sizeof(uint32_t)); //render flags size. number of int's to store render flags
    WRITE_CASTED(render_flags, sizeof(uint32_t));
    //1 render flag size means 1 32bit int. Meaning 32 different flags. Currently only 13 exist.
    //#TODO from rvmat
    /*
      AlwaysInShadow
      NoZWrite
      LandShadow
      Dummy0
      NoColorWrite
      NoAlphaWrite
      AddBlend
      AlphaTest32
      AlphaTest64
      AlphaTest128
      Road
      NoTiWrite
      NoReceiveShadow
     *
     */



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
        WRITE_CASTED(textures[i].type11_bool, sizeof(bool)); //world env map. Added in version 11.
    }



    //#TODO properly save them by iterating?
    output.write(reinterpret_cast<char*>(transforms.data()), sizeof(struct stage_transform) * num_transforms);
    //#TODO get uv source from rvmat
    /*
     uv source

      None
      Tex
      TexWaterAnim
      Pos
      Norm
      Tex1
      WorldPos
      WorldNorm
      TexShoreAnim
     */

    WRITE_CASTED(dummy_texture.texture_filter, sizeof(uint32_t));
    //FILTER
   //    Point = 0
   //    Linear = 1
   //    Trilinear
   //    Anizotropic << this is default
   //    Anizotropic2
   //    Anizotropic4
   //    Anizotropic8
   //    Anizotropic16

    //TI stage. Added in version 11
    output.write(dummy_texture.path.c_str(), dummy_texture.path.length() + 1);
    WRITE_CASTED(dummy_texture.transform_index, sizeof(uint32_t)); //tex gen. doesn't matter for ti
    WRITE_CASTED(dummy_texture.type11_bool, sizeof(bool)); //useWorldEnvMap grab from config entry "useWorldEnvMap"

    //#MinorTask we can save as version 10 instead and omit TI stage and the worldEnvMap to save a couple bytes filesize

}
