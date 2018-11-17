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


#define MAXBONES 512
#define MAXSECTIONS 1024
#define MAXANIMS 1024

#include "vector.h"
#include <string>
#include <vector>

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

struct skeleton_ { //using std::vector and std::string reduced the size of this from 4,3MB to 192B
    std::string name;
    uint32_t num_bones;
    std::vector<struct bone> bones;
    uint32_t num_sections;
    std::vector<std::string> sections;
    uint32_t num_animations;
    std::vector<animation> animations;
    bool is_discrete;
    float ht_min;
    float ht_max;
    float af_max;
    float mf_max;
    float mf_act;
    float t_body;
};


int read_model_config(const char *path, struct skeleton_ *skeleton);
