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


#define MAXBONES 255 //Arma 3 engine limit
#define MAXSECTIONS 1024
#define MAXANIMS 1024

#include "vector.h"
#include <string>
#include <vector>
#include "logger.h"

struct bone {
    bone(std::string n, std::string p) : name(std::move(n)), parent(std::move(p)) {}
    std::string name;
    std::string parent;
};

enum class AnimationType {
    ROTATION,
    ROTATION_X,
    ROTATION_Y,
    ROTATION_Z,
    TRANSLATION,
    TRANSLATION_X,
    TRANSLATION_Y,
    TRANSLATION_Z,
    DIRECT,
    HIDE,
};

enum class AnimationSourceAddress {
    clamp,
    loop,
    mirror
};

struct animation {
    animation(std::string n): name(std::move(n)) {}
    AnimationType type;
    std::string name;
    std::string selection;
    std::string source;
    std::string axis;
    std::string begin;
    std::string end;
    float min_value;
    float max_value;
    float min_phase;
    float max_phase;
    uint32_t junk;
    uint32_t always_0; // this might be centerfirstvertex? @todo
    AnimationSourceAddress source_address;
    float angle0;
    float angle1;
    float offset0;
    float offset1;
    vector3 axis_pos;
    vector3 axis_dir;
    float angle;
    float axis_offset;
    float hide_value;
    float unhide_value;
};

class ConfigClass;
class ModelConfig {

    std::optional<Config> cfg;
    std::shared_ptr<ConfigClass> modelConfig;
    std::string modelName;
    std::filesystem::path sourcePath;
public:
    int load(std::filesystem::path source, Logger& logger);


    const std::filesystem::path& getSourcePath() const noexcept {
        return sourcePath;
    }
    bool isLoaded() const noexcept {
        return cfg.has_value();
    }
    auto getConfig() const {
        return cfg->getConfig();
    }
    std::shared_ptr<ConfigClass> getModelConfig() const {
        return modelConfig;
    }
    auto getCfgSkeletons() const {
        return cfg->getConfig()->getClass({ "CfgSkeletons" });
    }
    auto getCfgModels() const {
        return cfg->getConfig()->getClass({ "CfgModels" });
    }

};


struct skeleton_ { //using std::vector and std::string reduced the size of this from 4,3MB to 192B

    void writeTo(std::ostream& output);
    int read(const ModelConfig& source, Logger& logger);



    std::string name; //skeleton name
    uint32_t num_bones{ 0 };
    std::vector<struct bone> bones;
    uint32_t num_sections{ 0 };
    std::vector<std::string> sections;
    uint32_t num_animations{ 0 };
    std::vector<animation> animations;
    bool is_discrete { false };
};


