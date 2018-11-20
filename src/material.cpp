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


const struct shader_ref pixelshaders[30] = {
    { 0, "Normal" },
    { 1, "NormalDXTA" },
    { 2, "NormalMap" },
    { 3, "NormalMapThrough" },
    { 4, "NormalMapSpecularDIMap" },
    { 5, "NormalMapDiffuse" },
    { 6, "Detail" },
    { 8, "Water" },
    { 12, "AlphaShadow" },
    { 13, "AlphaNoShadow" },
    { 15, "DetailMacroAS" },
    { 18, "NormalMapSpecularMap" },
    { 19, "NormalMapDetailSpecularMap" },
    { 20, "NormalMapMacroASSpecularMap" },
    { 21, "NormalMapDetailMacroASSpecularMap" },
    { 22, "NormalMapSpecularDIMap" },
    { 23, "NormalMapDetailSpecularDIMap" },
    { 24, "NormalMapMacroASSpecularDIMap" },
    { 25, "NormalMapDetailMacroASSpecularDIMap" },
    { 56, "Glass" },
    { 58, "NormalMapSpecularThrough" },
    { 56, "Grass" },
    { 60, "NormalMapThroughSimple" },
    { 102, "Super" },
    { 103, "Multi" },
    { 107, "Tree" },
    { 110, "Skin" },
    { 111, "CalmWater" },
    { 114, "TreeAdv" },
    { 116, "TreeAdvTrunk" }
};

const struct shader_ref vertexshaders[20] = {
    { 0, "Basic" },
    { 1, "NormalMap" },
    { 2, "NormalMapDiffuse" },
    { 3, "Grass" },
    { 8, "Water" },
    { 11, "NormalMapThrough" },
    { 14, "BasicAS" },
    { 15, "NormalMapAS" },
    { 17, "Glass" },
    { 18, "NormalMapSpecularThrough" },
    { 19, "NormalMapThroughNoFade" },
    { 20, "NormalMapSpecularThroughNoFade" },
    { 23, "Super" },
    { 24, "Multi" },
    { 25, "Tree" },
    { 26, "TreeNoFade" },
    { 29, "Skin" },
    { 30, "CalmWater" },
    { 31, "TreeAdv" },
    { 32, "TreeAdvTrunk" }
};


int read_material(struct material *material) {
    /*
     * Reads the material information for the given material struct.
     * Returns 0 on success and a positive integer on failure.
     */

    extern const char *current_target;
    char temp[2048];
    int i;
    const struct color default_color = { 0.0f, 0.0f, 0.0f, 1.0f };

    if (material->path[0] != '\\') {
        strcpy(temp, "\\");//#TODO use std::filesystem::path
        strcat(temp, material->path.c_str());
    } else {
        strcpy(temp, material->path.c_str());
    }

    // Write default values
    material->type = MATERIALTYPE;
    material->depr_1 = 1;
    material->depr_2 = 1;
    material->depr_3 = 1;
    material->emissive = default_color;
    material->ambient = default_color;
    material->diffuse = default_color;
    material->forced_diffuse = default_color;
    material->specular = default_color;
    material->specular2 = default_color;
    material->specular_power = 1.0f;
    material->pixelshader_id = 0;
    material->vertexshader_id = 0;
    material->num_textures = 1;
    material->num_transforms = 1;

    material->textures.resize(material->num_textures);
    material->transforms.resize(material->num_transforms);

    material->textures[0].path[0] = 0;
    material->textures[0].texture_filter = 3;
    material->textures[0].transform_index = 0;
    material->textures[0].type11_bool = 0;

    material->transforms[0].uv_source = 1;
    memset(material->transforms[0].transform, 0, 12 * sizeof(float));
    memcpy(material->transforms[0].transform, &identity_matrix, sizeof(identity_matrix));

    material->dummy_texture.path[0] = 0;
    material->dummy_texture.texture_filter = 3;
    material->dummy_texture.transform_index = 0;
    material->dummy_texture.type11_bool = 0;

    auto foundFile = find_file(temp, "");
    if (!foundFile) {
        lwarningf(current_target, -1, "Failed to find material \"%s\".\n", temp);
        return 1;
    }

    current_target = temp;

    ;

    Preprocessor p;
    std::stringstream buf;
    p.preprocess(foundFile->string(), std::ifstream(foundFile->string(), std::ifstream::in | std::ifstream::binary), buf, Preprocessor::ConstantMapType());
    buf.seekg(0);
    auto cfg = Config::fromPreprocessedText(buf, p.getLineref());

#define TRY_READ_ARRAY(tgt, src) {auto x = cfg->getArrayOfFloats({ #src }); if (!x.empty()) material->tgt = x;}

    current_target = material->path.c_str();
    // Read colors
    TRY_READ_ARRAY(emissive, emmisive);// "Did you mean: emissive?"
    TRY_READ_ARRAY(ambient, ambient);
    TRY_READ_ARRAY(diffuse, diffuse);
    TRY_READ_ARRAY(forced_diffuse, forcedDiffuse);
    TRY_READ_ARRAY(specular, specular);

    material->specular2 = material->specular;
    {auto x = cfg->getFloat({ "specularPower" }); if (x) material->specular_power = *x; }

    // Read shaders
    auto pixelShaderID = cfg->getString({ "PixelShaderID" });
    if (pixelShaderID) {
        for (i = 0; i < sizeof(pixelshaders) / sizeof(struct shader_ref); i++) {
            if (pixelshaders[i].name == *pixelShaderID)
                break;
        }
        if (i == sizeof(pixelshaders) / sizeof(struct shader_ref)) {
            lwarningf(current_target, -1, "Unrecognized pixel shader: \"%s\", assuming \"Normal\".\n", *pixelShaderID);
            i = 0;
        }
        material->pixelshader_id = pixelshaders[i].id;
    }

    auto VertexShaderID = cfg->getString({ "VertexShaderID" });
    if (VertexShaderID) {
        for (i = 0; i < sizeof(vertexshaders) / sizeof(struct shader_ref); i++) {
            if (vertexshaders[i].name == *VertexShaderID)
                break;
        }
        if (i == sizeof(vertexshaders) / sizeof(struct shader_ref)) {
            lwarningf(current_target, -1, "Unrecognized vertex shader: \"%s\", assuming \"Basic\".\n", *VertexShaderID);
            i = 0;
        }
        material->vertexshader_id = vertexshaders[i].id;
    }

    // Read stages
    for (i = 1; i < MAXSTAGES; i++) {
        if (!cfg->getString({ "Stage" + std::to_string(i), "texture" }))
            break;
        material->num_textures++;
        material->num_transforms++;
    }
    material->textures.resize(material->num_textures);
    material->transforms.resize(material->num_transforms);

    for (i = 0; i < material->num_textures; i++) {
        if (i == 0) {
            material->textures[i].path[0] = 0;
        } else {
            auto texture = cfg->getString({ "Stage" + std::to_string(i), "texture" });
            material->textures[i].path = *texture;
        }

        material->textures[i].texture_filter = 3;
        material->textures[i].transform_index = i;
        material->textures[i].type11_bool = 0;

        material->transforms[i].uv_source = 1;
        memset(material->transforms[i].transform, 0, 12 * sizeof(float));
        memcpy(material->transforms[i].transform, &identity_matrix, sizeof(identity_matrix));

        if (i != 0) {
            //#TODO retrieve uvTransform entry. So we don't re-resolve the whole path everytime

            auto aside = cfg->getArrayOfFloats({ "Stage" + std::to_string(i), "uvTransform", "aside" });
            if (!aside.empty()) {
                material->transforms[i].transform[0][0] = aside[0]; //#TODO make this easier use a actual matrix. And then read the parts as vectors using a vector<float> constructor
                material->transforms[i].transform[0][1] = aside[1];
                material->transforms[i].transform[0][2] = aside[2];
            }
            auto up = cfg->getArrayOfFloats({ "Stage" + std::to_string(i), "uvTransform", "up" });
            if (!up.empty()) {
                material->transforms[i].transform[1][0] = up[0];
                material->transforms[i].transform[1][1] = up[1];
                material->transforms[i].transform[1][2] = up[2];
            }
            auto dir = cfg->getArrayOfFloats({ "Stage" + std::to_string(i), "uvTransform", "dir" });
            if (!dir.empty()) {
                material->transforms[i].transform[2][0] = dir[0];
                material->transforms[i].transform[2][1] = dir[1];
                material->transforms[i].transform[2][2] = dir[2];
            }
            auto pos = cfg->getArrayOfFloats({ "Stage" + std::to_string(i), "uvTransform", "pos" });
            if (!pos.empty()) {
                material->transforms[i].transform[3][0] = pos[0];
                material->transforms[i].transform[3][1] = pos[1];
                material->transforms[i].transform[3][2] = pos[2];
            }
        }
    }

    //read_string(f, "StageTI >> texture", material->dummy_texture.path, sizeof(material->dummy_texture.path));

    return 0;
}
