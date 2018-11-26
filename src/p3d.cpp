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
#define NOMINMAX

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <algorithm>
//#include <unistd.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "args.h"
#include "filesystem.h"
#include "utils.h"
#include "model_config.h"
#include "material.h"
#include "vector.h"
#include "matrix.h"
#include "p3d.h"
#include <numeric>
#include <fstream>

__itt_domain* p3dDomain = __itt_domain_create("armake.p3d");
__itt_string_handle* handle_mlod2odol = __itt_string_handle_create("mlod2odol");
__itt_string_handle* handle_bs1 = __itt_string_handle_create("bs1");
__itt_string_handle* handle_bs2 = __itt_string_handle_create("bs2");


#define WRITE_CASTED(elem, size)  output.write(reinterpret_cast<char*>(&elem), size);

float mlod_lod::getBoundingSphere(const vector3& center) {
    /*
     * Calculate and return the bounding sphere for the given LOD.
     */

    float sphere = 0.f;

    __itt_task_begin(p3dDomain, __itt_null, __itt_null, handle_bs1);

    float sphere1 = std::transform_reduce(points.begin(), points.end(), 0.f,
        [](const auto& lhs, const auto& rhs) { return std::max(lhs, rhs); }, [&center](const auto& p) {
        auto& point = p.getPosition();
        return (point - center).magnitude_squared();
    });
    __itt_task_end(p3dDomain);

    __itt_task_begin(p3dDomain, __itt_null, __itt_null, handle_bs2);

    for (auto i = 0u; i < num_points; i++) {
        auto& point = points[i].getPosition();
        auto dist = (point - center).magnitude_squared();
        if (dist > sphere)
            sphere = dist;
    }
    __itt_task_end(p3dDomain);

    return sqrt(sphere);
}

bool mlod_lod::read(std::istream& source) {
    char buffer[512]; //texture_name needs 512
    uint32_t tagg_len;


    source.seekg(8, std::istream::cur); //#TODO what is it skipping here?
    source.read(reinterpret_cast<char*>(&num_points), 4);
    source.read(reinterpret_cast<char*>(&num_facenormals), 4);
    source.read(reinterpret_cast<char*>(&num_faces), 4);
    source.seekg(4, std::istream::cur);

    bool empty = num_points == 0;

    if (empty) {
        num_points = 1;
        points.resize(1);
        points[0].x = 0.0f;
        points[0].y = 0.0f;
        points[0].z = 0.0f;
        points[0].point_flags = 0;
    } else {
        points.resize(num_points);
        for (int j = 0; j < num_points; j++)
            source.read(reinterpret_cast<char*>(&points[j]), sizeof(struct point));
    }

    facenormals.resize(num_facenormals);
    for (int j = 0; j < num_facenormals; j++)
        source.read(reinterpret_cast<char*>(&facenormals[j]), sizeof(vector3));

    faces.resize(num_faces);
    for (int j = 0; j < num_faces; j++) {
        source.read(reinterpret_cast<char*>(&faces[j]), 72);

        size_t fp_tmp = source.tellg();
        std::getline(source, faces[j].texture_name, '\0');

        std::getline(source, faces[j].material_name, '\0');

        faces[j].section_names = "";
    }

    source.read(buffer, 4);
    if (strncmp(buffer, "TAGG", 4) != 0)
        return false;

    num_sharp_edges = 0;
    properties.clear();

    while (true) {
        source.seekg(1, std::istream::cur);
        std::string entry;


        std::getline(source, entry, '\0');
        source.read(reinterpret_cast<char*>(&tagg_len), 4);
        size_t fp_tmp = static_cast<size_t>(source.tellg()) + tagg_len;

        if (entry[0] != '#') {
            mlod_selection newSelection;

            newSelection.name = entry;

            if (empty) {
                newSelection.points.resize(1);
                newSelection.points[0] = 0;
            }
            else {
                newSelection.points.resize(num_points);
                source.read(reinterpret_cast<char*>(newSelection.points.data()), num_points);
            }

            newSelection.faces.resize(num_faces);
            source.read(reinterpret_cast<char*>(newSelection.faces.data()), num_faces);

            selections.emplace_back(std::move(newSelection));
        }

        if (entry == "#Mass#") {
            if (empty) {
                mass.resize(1);
                mass[0] = 0.0f;
            }
            else {
                mass.resize(num_points);
                source.read(reinterpret_cast<char*>(mass.data()), sizeof(float) * num_points);
            }
        }

        if (entry == "#SharpEdges#") {
            num_sharp_edges = tagg_len / (2 * sizeof(uint32_t));
            sharp_edges.resize(num_sharp_edges * 2);//one edge is 2 points
            //#TODO make it a vec of a pair of uints. 2 points for an edge
            source.read(reinterpret_cast<char*>(sharp_edges.data()), tagg_len);
        }

        if (entry == "#Property#") {
            char buffer[64];
            source.read(buffer, 64);
            std::string name = buffer;
            source.read(buffer, 64);
            std::string value = buffer;
            properties.emplace_back(property{ std::move(name), std::move(value) });
        }

        source.seekg(fp_tmp);

        if (entry == "#EndOfFile#")
            break;
    }
    num_selections = selections.size();

    source.read(reinterpret_cast<char*>(&resolution), 4);
    return true;
}

void odol_section::writeTo(std::ostream& output) {
    WRITE_CASTED(face_index_start, sizeof(uint32_t));
    WRITE_CASTED(face_index_end, sizeof(uint32_t));
    WRITE_CASTED(min_bone_index, sizeof(uint32_t));
    WRITE_CASTED(bones_count, sizeof(uint32_t));
    WRITE_CASTED(mat_dummy, sizeof(uint32_t));
    WRITE_CASTED(common_texture_index, sizeof(uint16_t));
    WRITE_CASTED(common_face_flags, sizeof(uint32_t));
    WRITE_CASTED(material_index, sizeof(int32_t));
    if (material_index == -1)
        output.put(0);
    WRITE_CASTED(num_stages, sizeof(uint32_t));
    if (num_stages > 2)
        __debugbreak(); //This doesn't work. I can't print this
    output.write(reinterpret_cast<char*>(area_over_tex), sizeof(float) * num_stages);
    WRITE_CASTED(unknown_long, sizeof(uint32_t));
}

void odol_selection::writeTo(std::ostream& output) {
    output.write(name.c_str(), name.length() + 1);

    WRITE_CASTED(num_faces, sizeof(uint32_t), 1, f_target);
    if (num_faces > 0) {
        output.put(0);
        output.write(reinterpret_cast<char*>(faces.data()), sizeof(uint32_t) * num_faces);
    }

    WRITE_CASTED(always_0, sizeof(uint32_t));

    WRITE_CASTED(is_sectional, 1);
    WRITE_CASTED(num_sections, sizeof(uint32_t));
    if (num_sections > 0) {
        output.put(0);
        output.write(reinterpret_cast<char*>(sections.data()), sizeof(uint32_t) * num_sections);
    }

    WRITE_CASTED(num_vertices, sizeof(uint32_t));
    if (num_vertices > 0) {
        output.put(0);
        output.write(reinterpret_cast<char*>(vertices.data()), sizeof(uint32_t) * num_vertices);
    }

    WRITE_CASTED(num_vertex_weights, sizeof(uint32_t));
    if (num_vertex_weights > 0) {
        output.put(0);
        output.write(reinterpret_cast<char*>(vertex_weights.data()), sizeof(uint8_t) * num_vertex_weights);
    }
}


uint32_t odol_lod::add_point(const mlod_lod &mlod_lod, const model_info &model_info,
        uint32_t point_index_mlod, vector3 normal, struct uv_pair *uv_coords_input, Logger& logger) {
    uint32_t i;
    uint32_t j;
    uint32_t weight_index;

    // Check if there already is a vertex that satisfies the requirements
    for (i = 0; i < num_points; i++) { //#TODO make vertex_to_point a unordered_map?
        if (vertex_to_point[i] != point_index_mlod)
            continue;

        // normals and uvs don't matter for non-visual lods
        if (mlod_lod.resolution < LOD_GEOMETRY) {
            if (!float_equal(normals[i].x, normal.x, 0.0001) ||
                !float_equal(normals[i].y, normal.y, 0.0001) ||
                !float_equal(normals[i].z, normal.z, 0.0001))
                continue;

            if (!float_equal(uv_coords[i].u, uv_coords_input->u, 0.0001) ||
                !float_equal(uv_coords[i].v, uv_coords_input->v, 0.0001))
                continue;
        }

        return i;
    }

    // Add vertex
    points[num_points] = mlod_lod.points[point_index_mlod].getPosition();
    normals[num_points] = normal;
    memcpy(&uv_coords[num_points], uv_coords_input, sizeof(struct uv_pair));

    if (!vertexboneref.empty() && model_info.skeleton->num_bones > 0) {
        memset(&vertexboneref[num_points], 0, sizeof(struct odol_vertexboneref));

        for (i = model_info.skeleton->num_bones - 1; (int32_t)i >= 0; i--) {
            for (j = 0; j < mlod_lod.num_selections; j++) {
                if (stricmp(model_info.skeleton->bones[i].name.c_str(),
                        mlod_lod.selections[j].name.c_str()) == 0)
                    break;
            }

            if (j == mlod_lod.num_selections)
                continue;

            if (mlod_lod.selections[j].points[point_index_mlod] == 0)
                continue;

            if (vertexboneref[num_points].num_bones == 4) {
                logger.warning(current_target, -1, "Vertex %u of LOD %f is part of more than 4 bones.\n", point_index_mlod, mlod_lod.resolution);
                continue;
            }

            if (vertexboneref[num_points].num_bones == 1 && model_info.skeleton->is_discrete) {
                logger.warning(current_target, -1, "Vertex %u of LOD %f is part of more than 1 bone in a discrete skeleton.\n", point_index_mlod, mlod_lod.resolution);
                continue;
            }

            weight_index = vertexboneref[num_points].num_bones;
            vertexboneref[num_points].num_bones++;

            vertexboneref[num_points].weights[weight_index][0] = skeleton_to_subskeleton[i].links[0];
            vertexboneref[num_points].weights[weight_index][1] = mlod_lod.selections[j].points[point_index_mlod];

            // convert weight
            if (vertexboneref[num_points].weights[weight_index][1] == 0x01)
                vertexboneref[num_points].weights[weight_index][1] = 0xff;
            else
                vertexboneref[num_points].weights[weight_index][1]--;

            if (model_info.skeleton->is_discrete)
                break;
        }
    }

    vertex_to_point[num_points] = point_index_mlod;
    point_to_vertex[point_index_mlod] = num_points;

    num_points++;

    return (num_points - 1);
}


void odol_lod::writeTo(std::ostream& output) {
    short u, v;
    int x, y, z;
    long i;
    uint32_t temp;
    char *ptr;

    WRITE_CASTED(num_proxies, sizeof(uint32_t));
    for (i = 0; i < num_proxies; i++) {
        output.write(proxies[i].name.c_str(), proxies[i].name.length() + 1);
        WRITE_CASTED(proxies[i].transform_x, sizeof(vector3));
        WRITE_CASTED(proxies[i].transform_y, sizeof(vector3));
        WRITE_CASTED(proxies[i].transform_z, sizeof(vector3));
        WRITE_CASTED(proxies[i].transform_n, sizeof(vector3));
        WRITE_CASTED(proxies[i].proxy_id, sizeof(uint32_t));
        WRITE_CASTED(proxies[i].selection_index, sizeof(uint32_t));
        WRITE_CASTED(proxies[i].bone_index, sizeof(int32_t));
        WRITE_CASTED(proxies[i].section_index, sizeof(uint32_t));
    }

    WRITE_CASTED(num_bones_subskeleton, sizeof(uint32_t));
    //#TODO use size() on array instead of num var
    output.write(reinterpret_cast<const char*>(subskeleton_to_skeleton.data()), sizeof(uint32_t) * num_bones_subskeleton);


    WRITE_CASTED(num_bones_skeleton, sizeof(uint32_t));
    for (i = 0; i <num_bones_skeleton; i++) {
        WRITE_CASTED(skeleton_to_subskeleton[i].num_links, sizeof(uint32_t));
        output.write(reinterpret_cast<const char*>(skeleton_to_subskeleton[i].links), sizeof(uint32_t) * skeleton_to_subskeleton[i].num_links);
    }

    WRITE_CASTED(num_points, sizeof(uint32_t));
    WRITE_CASTED(face_area, sizeof(float));
    output.write(reinterpret_cast<const char*>(clip_flags), sizeof(uint32_t)*2);
    WRITE_CASTED(min_pos, sizeof(vector3));
    WRITE_CASTED(max_pos, sizeof(vector3));
    WRITE_CASTED(autocenter_pos, sizeof(vector3));
    WRITE_CASTED(sphere, sizeof(float));

    WRITE_CASTED(num_textures, sizeof(uint32_t));
    ptr = textures;
    for (i = 0; i < num_textures; i++)
        ptr += strlen(ptr) + 1;
    output.write(textures, ptr - textures);

    WRITE_CASTED(num_materials, sizeof(uint32_t));
    for (uint32_t i = 0; i < num_materials; i++) //#TODO ranged for
        materials[i].writeTo(output);

    // the point-to-vertex and vertex-to-point arrays are just left out
    output.write("\0\0\0\0\0\0\0\0", sizeof(uint32_t) * 2);

    WRITE_CASTED(num_faces, sizeof(uint32_t));
    WRITE_CASTED(face_allocation_size, sizeof(uint32_t));
    WRITE_CASTED(always_0, sizeof(uint16_t));

    for (i = 0; i < num_faces; i++) {
        WRITE_CASTED(faces[i].face_type, sizeof(uint8_t));
        output.write(reinterpret_cast<char*>(faces[i].table), sizeof(uint32_t) * faces[i].face_type);
    }

    WRITE_CASTED(num_sections, sizeof(uint32_t));
    for (i = 0; i < num_sections; i++) {
        sections[i].writeTo(output);
    }

    //#TODO may want a array writer for this. It's always number followed by elements
    WRITE_CASTED(num_selections, sizeof(uint32_t));
    for (i = 0; i < num_selections; i++) {
        selections[i].writeTo(output);
    }

    WRITE_CASTED(num_properties, sizeof(uint32_t));
    for (auto& prop : properties) {
        output.write(prop.name.c_str(), prop.name.length() + 1);
        output.write(prop.value.c_str(), prop.value.length() + 1);
    }

    WRITE_CASTED(num_frames, sizeof(uint32_t));
    // @todo frames

    WRITE_CASTED(icon_color, sizeof(uint32_t));
    WRITE_CASTED(selected_color, sizeof(uint32_t));
    WRITE_CASTED(flags, sizeof(uint32_t));
    WRITE_CASTED(vertexboneref_is_simple, sizeof(bool));

    size_t fp_vertextable_size = output.tellp();
    output.write("\0\0\0\0", 4);

    //#TODO? This? stuff?
    // pointflags
    WRITE_CASTED(num_points, 4);
    output.put(1);
    if (num_points > 0)
        output.write("\0\0\0\0", 4);

    // uvs
    output.write(reinterpret_cast<char*>(uv_scale), sizeof(struct uv_pair) * 2);
    WRITE_CASTED(num_points, sizeof(uint32_t));
    output.put(0);
    if (num_points > 0) {
        output.put(0);
        for (i = 0; i < num_points; i++) {
            // write compressed pair
            float u_relative = (uv_coords[i].u - uv_scale[0].u) / (uv_scale[1].u - uv_scale[0].u);
            float v_relative = (uv_coords[i].v - uv_scale[0].v) / (uv_scale[1].v - uv_scale[0].v);
            u = (short)(u_relative * 2 * INT16_MAX - INT16_MAX);
            v = (short)(v_relative * 2 * INT16_MAX - INT16_MAX);

            output.write(reinterpret_cast<char*>(&u), sizeof(int16_t));
            output.write(reinterpret_cast<char*>(&v), sizeof(int16_t));
        }
    }
    output.write("\x01\0\0\0", 4);

    // points
    WRITE_CASTED(num_points, sizeof(uint32_t));
    if (num_points > 0) {
        output.put(0);
        output.write(reinterpret_cast<char*>(points.data()), sizeof(vector3) * num_points);
    }

    // normals
    WRITE_CASTED(num_points, sizeof(uint32_t));
    output.put(0);
    if (num_points > 0) {
        output.put(0);
        for (i = 0; i < num_points; i++) {
            // write compressed triplet
            x = (int)(-511.0f * normals[i].x + 0.5);
            y = (int)(-511.0f * normals[i].y + 0.5);
            z = (int)(-511.0f * normals[i].z + 0.5);

            x = MAX(MIN(x, 511), -511);
            y = MAX(MIN(y, 511), -511);
            z = MAX(MIN(z, 511), -511);

            temp = (((uint32_t)z & 0x3FF) << 20) | (((uint32_t)y & 0x3FF) << 10) | ((uint32_t)x & 0x3FF);
            output.write(reinterpret_cast<char*>(&temp), sizeof(uint32_t));
        }
    }

    // ST coordinates
    output.write("\0\0\0\0", 4);

    // vertex bone ref
    if (vertexboneref.empty() || num_points == 0) {
        output.write("\0\0\0\0", 4);
    }
    else {
        WRITE_CASTED(num_points, sizeof(uint32_t));
        output.put(0);
        output.write(reinterpret_cast<char*>(vertexboneref.data()), sizeof(struct odol_vertexboneref) * num_points);
    }

    // neighbor bone ref
    output.write("\0\0\0\0", 4);

    temp = static_cast<size_t>(output.tellp()) - fp_vertextable_size;
    output.seekp(fp_vertextable_size);
    WRITE_CASTED(temp, 4);
    output.seekp(0, std::ostream::end);


    // has Collimator info?
    output.write("\0\0\0\0", sizeof(uint32_t)); //If 1 then need to write CollimatorInfo structure

    // unknown byte
    output.write("\0", 1);
}


int flags_to_pass(uint32_t flags) {
    if (flags & FLAG_DSTBLENDZERO)
        return 0;
    if (flags & FLAG_NOALPHAWRITE)
        return 2;
    if (flags & FLAG_NOCOLORWRITE)
        return 3;
    if (flags & FLAG_NOZWRITE)
        return 4;
    return 1;
}


int compare_face_lookup(std::vector<mlod_face> &faces, uint32_t a, uint32_t b) {

    uint32_t a_index;
    uint32_t b_index;
    int compare;

    a_index = a;
    b_index = b;

    compare = faces[a_index].material_index - faces[b_index].material_index;
    if (compare != 0)
        return compare;

    compare = faces[a_index].face_flags - faces[b_index].face_flags;
    if (compare != 0)
        return compare;

    compare = faces[a_index].texture_index - faces[b_index].texture_index;
    if (compare != 0)
        return compare;

    return faces[a_index].section_names.compare(faces[b_index].section_names);
}


bool is_alpha(struct mlod_face *face) {
    // @todo check actual texture maybe?
    if (strstr(face->texture_name.c_str(), "_ca.paa") != NULL)
        return true;
    if (strstr(face->texture_name.c_str(), "ca)") != NULL)
        return true;
    return false;
}


void P3DFile::convert_lod(mlod_lod &mlod_lod, odol_lod &odol_lod) {
    extern std::string current_target;
    unsigned long i;
    unsigned long j;
    unsigned long k;
    unsigned long face;
    unsigned long face_start;
    unsigned long face_end;
    size_t size;
    char *ptr;
    std::vector<std::string> textures;
    vector3 normal;
    struct uv_pair uv_coords;

    // Set sub skeleton references
    odol_lod.num_bones_skeleton = model_info.skeleton->num_bones;
    odol_lod.num_bones_subskeleton = model_info.skeleton->num_bones;
    odol_lod.subskeleton_to_skeleton.resize(odol_lod.num_bones_skeleton);
    odol_lod.skeleton_to_subskeleton.resize(odol_lod.num_bones_skeleton);

    for (i = 0; i < model_info.skeleton->num_bones; i++) {
        odol_lod.subskeleton_to_skeleton[i] = i;
        odol_lod.skeleton_to_subskeleton[i].num_links = 1;
        odol_lod.skeleton_to_subskeleton[i].links[0] = i;
    }

    odol_lod.num_points_mlod = mlod_lod.num_points;

    odol_lod.face_area = 0;
    odol_lod.clip_flags[0] = 0;
    odol_lod.clip_flags[1] = 0;

    odol_lod.min_pos = empty_vector;
    odol_lod.max_pos = empty_vector;

    for (i = 0; i < mlod_lod.num_points; i++) {
        if (i == 0 || mlod_lod.points[i].x < odol_lod.min_pos.x)
            odol_lod.min_pos.x = mlod_lod.points[i].x;
        if (i == 0 || mlod_lod.points[i].y < odol_lod.min_pos.y)
            odol_lod.min_pos.y = mlod_lod.points[i].y;
        if (i == 0 || mlod_lod.points[i].z < odol_lod.min_pos.z)
            odol_lod.min_pos.z = mlod_lod.points[i].z;
        if (i == 0 || mlod_lod.points[i].x > odol_lod.max_pos.x)
            odol_lod.max_pos.x = mlod_lod.points[i].x;
        if (i == 0 || mlod_lod.points[i].y > odol_lod.max_pos.y)
            odol_lod.max_pos.y = mlod_lod.points[i].y;
        if (i == 0 || mlod_lod.points[i].z > odol_lod.max_pos.z)
            odol_lod.max_pos.z = mlod_lod.points[i].z;
    }

    odol_lod.autocenter_pos = (odol_lod.min_pos + odol_lod.max_pos) * 0.5f;

    odol_lod.sphere = mlod_lod.getBoundingSphere(odol_lod.autocenter_pos);

    // Textures & Materials
    odol_lod.num_textures = 0;
    odol_lod.num_materials = 0;
    odol_lod.materials.clear();

    size = 0;
    for (i = 0; i < mlod_lod.num_faces; i++) {
        for (j = 0; j < odol_lod.num_textures; j++) {
            if (mlod_lod.faces[i].texture_name == textures[j])
                break;
        }

        mlod_lod.faces[i].texture_index = j;

        if (j >= MAXTEXTURES) {
            logger.warning(current_target, -1, "Maximum amount of textures per LOD (%i) exceeded.", MAXTEXTURES);
            break;
        }

        if (j >= odol_lod.num_textures) {
            textures.emplace_back(mlod_lod.faces[i].texture_name);
            size += mlod_lod.faces[i].texture_name.length() + 1;
            odol_lod.num_textures++;
        }

        bool matExists = false;
        for (j = 0; j < MAXMATERIALS && j < odol_lod.materials.size() && !odol_lod.materials[j].path.empty(); j++) {
            if (mlod_lod.faces[i].material_name == odol_lod.materials[j].path) {
                matExists = true;
                break;
            }
        }
        //#TODO use findIf here
        //#CHECK if material doesn't exist yet. j should be materials size+1
        //#TODO use materials.count() to get the index of the to be inserted element
        mlod_lod.faces[i].material_index = (mlod_lod.faces[i].material_name.length() > 0) ? j : -1;

        if (j >= MAXMATERIALS) {
            logger.warning(current_target, -1, "Maximum amount of materials per LOD (%i) exceeded.", MAXMATERIALS);
            break;
        }

        if (mlod_lod.faces[i].material_name.empty() || matExists) //Only create material if we have a material. And if it doesn't exist already
            continue;

        auto temp = current_target;

        Material mat(logger, mlod_lod.faces[i].material_name);
       
        odol_lod.num_materials++;
        mat.read();//#TODO exceptions Though we don't seem to check for errors there? Maybe we should?
        odol_lod.materials.emplace_back(std::move(mat)); //#CHECK new element should be at index j now.

        current_target = temp;
    }

    odol_lod.textures = (char *)safe_malloc(size);
    ptr = odol_lod.textures;
    for (i = 0; i < odol_lod.num_textures; i++) {
        strncpy(ptr, textures[i].c_str(), textures[i].length() + 1);
        ptr += textures[i].length() + 1;
    }

    odol_lod.num_faces = mlod_lod.num_faces;

    odol_lod.always_0 = 0;

    odol_lod.faces.resize(odol_lod.num_faces);

    odol_lod.num_points = 0;

    odol_lod.point_to_vertex.resize(odol_lod.num_points_mlod);
    odol_lod.vertex_to_point.resize(odol_lod.num_faces * 4 + odol_lod.num_points_mlod);
    odol_lod.face_lookup.resize(mlod_lod.num_faces);

    for (i = 0; i < mlod_lod.num_faces; i++)
        odol_lod.face_lookup[i] = i;

    for (i = 0; i < odol_lod.num_points_mlod; i++)
        odol_lod.point_to_vertex[i] = NOPOINT;

    odol_lod.uv_coords.resize(odol_lod.num_faces * 4 + odol_lod.num_points_mlod);
    odol_lod.points.resize(odol_lod.num_faces * 4 + odol_lod.num_points_mlod);
    odol_lod.normals.resize(odol_lod.num_faces * 4 + odol_lod.num_points_mlod);

    if (model_info.skeleton->num_bones > 0)
        odol_lod.vertexboneref.resize(odol_lod.num_faces * 4 + odol_lod.num_points_mlod);

    // Set face flags
    std::vector<bool> tileU, tileV;
    tileU.resize(odol_lod.num_textures); tileV.resize(odol_lod.num_textures);

    for (i = 0; i < mlod_lod.num_faces; i++) {
        if (mlod_lod.faces[i].texture_name.empty())
            continue;
        if (tileU[mlod_lod.faces[i].texture_index] && tileV[mlod_lod.faces[i].texture_index])
            continue;
        for (j = 0; j < mlod_lod.faces[i].face_type; j++) {
            if (mlod_lod.faces[i].table[j].u < -CLAMPLIMIT || mlod_lod.faces[i].table[j].u > 1 + CLAMPLIMIT)
                tileU[mlod_lod.faces[i].texture_index] = true;
            if (mlod_lod.faces[i].table[j].v < -CLAMPLIMIT || mlod_lod.faces[i].table[j].v > 1 + CLAMPLIMIT)
                tileV[mlod_lod.faces[i].texture_index] = true;
        }
    }
    for (i = 0; i < mlod_lod.num_faces; i++) {
        if (mlod_lod.faces[i].face_flags & (FLAG_NOCLAMP | FLAG_CLAMPU | FLAG_CLAMPV))
            continue;
        if (mlod_lod.faces[i].texture_name.empty()) {
            mlod_lod.faces[i].face_flags |= FLAG_NOCLAMP;
            continue;
        }

        if (!tileU[mlod_lod.faces[i].texture_index])
            mlod_lod.faces[i].face_flags |= FLAG_CLAMPU;
        if (!tileV[mlod_lod.faces[i].texture_index])
            mlod_lod.faces[i].face_flags |= FLAG_CLAMPV;
        if (tileU[mlod_lod.faces[i].texture_index] && tileU[mlod_lod.faces[i].texture_index])
            mlod_lod.faces[i].face_flags |= FLAG_NOCLAMP;

        if (is_alpha(&mlod_lod.faces[i]))
            mlod_lod.faces[i].face_flags |= FLAG_ISALPHA;
    }

    for (i = 0; i < mlod_lod.num_selections; i++) {
        for (j = 0; j < model_info.skeleton->num_sections; j++) {
            if (mlod_lod.selections[i].name == model_info.skeleton->sections[j])
                break;
        }
        if (j < model_info.skeleton->num_sections) {
            for (k = 0; k < mlod_lod.num_faces; k++) {
                if (mlod_lod.selections[i].faces[k] > 0) {
                    mlod_lod.faces[k].section_names += ":";
                    mlod_lod.faces[k].section_names += mlod_lod.selections[i].name;
                }
            }
        }

        if (strncmp(mlod_lod.selections[i].name.c_str(), "proxy:", 6) != 0)
            continue;

        for (k = 0; k < mlod_lod.num_faces; k++) {
            if (mlod_lod.selections[i].faces[k] > 0) {
                mlod_lod.faces[k].face_flags |= FLAG_ISHIDDENPROXY;
                mlod_lod.faces[k].texture_index = -1;
                mlod_lod.faces[k].material_index = -1;
            }
        }
    }

    // Sort faces
    if (mlod_lod.num_faces > 1) {
        std::sort(mlod_lod.faces.begin(), mlod_lod.faces.end());
    }

    // Write face vertices
    face_end = 0;
    memset(odol_lod.uv_scale, 0, sizeof(struct uv_pair) * 2);
    for (i = 0; i < mlod_lod.num_faces; i++) {
        odol_lod.faces[i].face_type = mlod_lod.faces[odol_lod.face_lookup[i]].face_type;
        for (j = 0; j < odol_lod.faces[i].face_type; j++) {
            normal = mlod_lod.facenormals[mlod_lod.faces[odol_lod.face_lookup[i]].table[j].normals_index];
            uv_coords.u = mlod_lod.faces[odol_lod.face_lookup[i]].table[j].u;
            uv_coords.v = mlod_lod.faces[odol_lod.face_lookup[i]].table[j].v;

            uv_coords.u = fsign(uv_coords.u) * (fmod(fabs(uv_coords.u), 1.0));
            uv_coords.v = fsign(uv_coords.v) * (fmod(fabs(uv_coords.v), 1.0));

            odol_lod.uv_scale[0].u = fminf(uv_coords.u, odol_lod.uv_scale[0].u);
            odol_lod.uv_scale[0].v = fminf(uv_coords.v, odol_lod.uv_scale[0].v);
            odol_lod.uv_scale[1].u = fmaxf(uv_coords.u, odol_lod.uv_scale[1].u);
            odol_lod.uv_scale[1].v = fmaxf(uv_coords.v, odol_lod.uv_scale[1].v);

            // Change vertex order for ODOL
            // Tris:  0 1 2   -> 1 0 2
            // Quads: 0 1 2 3 -> 1 0 3 2
            if (odol_lod.faces[i].face_type == 4)
                k = j ^ 1;
            else
                k = j ^ (1 ^ (j >> 1));

            odol_lod.faces[i].table[k] = odol_lod.add_point(mlod_lod, model_info,
                mlod_lod.faces[odol_lod.face_lookup[i]].table[j].points_index, normal, &uv_coords, logger);
        }
        face_end += (odol_lod.faces[i].face_type == 4) ? 20 : 16;
    }

    odol_lod.face_allocation_size = face_end;

    // Write remaining vertices
    for (i = 0; i < odol_lod.num_points_mlod; i++) {
        if (odol_lod.point_to_vertex[i] < NOPOINT)
            continue;

        normal = empty_vector;

        uv_coords.u = 0.0f;
        uv_coords.v = 0.0f;

        odol_lod.point_to_vertex[i] = odol_lod.add_point(mlod_lod, model_info,
            i, normal, &uv_coords, logger);
    }

    // Normalize vertex bone ref
    odol_lod.vertexboneref_is_simple = 1;
    float weight_sum;
    if (!odol_lod.vertexboneref.empty()) {
        for (i = 0; i < odol_lod.num_points; i++) {
            if (odol_lod.vertexboneref[i].num_bones == 0)
                continue;

            if (odol_lod.vertexboneref[i].num_bones > 1)
                odol_lod.vertexboneref_is_simple = 0;

            weight_sum = 0;
            for (j = 0; j < odol_lod.vertexboneref[i].num_bones; j++) {
                weight_sum += odol_lod.vertexboneref[i].weights[j][1] / 255.0f;
            }

            for (j = 0; j < odol_lod.vertexboneref[i].num_bones; j++) {
                odol_lod.vertexboneref[i].weights[j][1] *= (1.0 / weight_sum);
            }
        }
    }

    // Sections
    if (odol_lod.num_faces > 0) {
        odol_lod.num_sections = 1;
        for (i = 1; i < odol_lod.num_faces; i++) {
            if (compare_face_lookup(mlod_lod.faces, odol_lod.face_lookup[i], odol_lod.face_lookup[i - 1])) {
                odol_lod.num_sections++;
                continue;
            }
        }

        odol_lod.sections.resize(odol_lod.num_sections);

        face_start = 0;
        face_end = 0;
        k = 0;
        for (i = 0; i < odol_lod.num_faces;) {
            odol_lod.sections[k].face_start = i;
            odol_lod.sections[k].face_end = i;
            odol_lod.sections[k].face_index_start = face_start;
            odol_lod.sections[k].face_index_end = face_start;
            odol_lod.sections[k].min_bone_index = 0;
            odol_lod.sections[k].bones_count = odol_lod.num_bones_subskeleton;
            odol_lod.sections[k].mat_dummy = 0;
            odol_lod.sections[k].common_texture_index = mlod_lod.faces[odol_lod.face_lookup[i]].texture_index;
            odol_lod.sections[k].common_face_flags = mlod_lod.faces[odol_lod.face_lookup[i]].face_flags;
            odol_lod.sections[k].material_index = mlod_lod.faces[odol_lod.face_lookup[i]].material_index;
            odol_lod.sections[k].num_stages = 2; // num_stages defines number of entries in area_over_tex
            odol_lod.sections[k].area_over_tex[0] = 1.0f; // @todo
            odol_lod.sections[k].area_over_tex[1] = -1000.0f;
            odol_lod.sections[k].unknown_long = 0;

            for (j = i; j < odol_lod.num_faces; j++) {
                if (compare_face_lookup(mlod_lod.faces, odol_lod.face_lookup[j], odol_lod.face_lookup[i]))
                    break;

                odol_lod.sections[k].face_end++;
                odol_lod.sections[k].face_index_end += (odol_lod.faces[j].face_type == 4) ? 20 : 16;
            }

            face_start = odol_lod.sections[k].face_index_end;
            i = j;
            k++;
        }
    } else {
        odol_lod.num_sections = 0;
        odol_lod.sections.clear();
    }

    // Selections
    odol_lod.num_selections = mlod_lod.num_selections;
    odol_lod.selections.resize(odol_lod.num_selections);
    for (i = 0; i < odol_lod.num_selections; i++) {
        auto& odolSection = odol_lod.selections[i];
        auto& mlodSection = mlod_lod.selections[i];
        odolSection.name = mlodSection.name;
        std::transform(odolSection.name.begin(), odolSection.name.end(), odolSection.name.begin(), tolower);

        for (j = 0; j < model_info.skeleton->num_sections; j++) {
            if (model_info.skeleton->sections[j] == mlodSection.name.c_str())
                break;
        }
        odolSection.is_sectional = j < model_info.skeleton->num_sections;

        if (odolSection.is_sectional) {
            odolSection.num_faces = 0;
            odolSection.faces.clear();
            odolSection.always_0 = 0;
            odolSection.num_vertices = 0;
            odolSection.vertices.clear();
            odolSection.num_vertex_weights = 0;
            odolSection.vertex_weights.clear();

            odolSection.num_sections = 0;
            for (j = 0; j < odol_lod.num_sections; j++) {
                if (mlodSection.faces[odol_lod.face_lookup[odol_lod.sections[j].face_start]] > 0)
                    odolSection.num_sections++;
            }
            odolSection.sections.resize(odolSection.num_sections);
            k = 0;
            for (j = 0; j < odol_lod.num_sections; j++) {
                if (mlodSection.faces[odol_lod.face_lookup[odol_lod.sections[j].face_start]] > 0) {
                    odolSection.sections[k] = j;
                    k++;
                }
            }

            continue;
        } else {
            odolSection.num_sections = 0;
            odolSection.sections.clear();
        }

        odolSection.num_faces = 0;
        for (j = 0; j < odol_lod.num_faces; j++) {
            if (mlodSection.faces[j] > 0)
                odolSection.num_faces++;
        }

        odolSection.faces.resize(odolSection.num_faces);
        for (j = 0; j < odolSection.num_faces; j++)
            odolSection.faces[j] = NOPOINT;

        for (j = 0; j < odol_lod.num_faces; j++) {
            if (mlodSection.faces[j] == 0)
                continue;

            for (k = 0; k < odolSection.num_faces; k++) {
                if (odolSection.faces[k] == NOPOINT)
                    break;
            }
            odolSection.faces[k] = odol_lod.face_lookup[j];
        }

        odolSection.always_0 = 0;

        odolSection.num_vertices = 0;
        for (j = 0; j < odol_lod.num_points; j++) {
            if (mlodSection.points[odol_lod.vertex_to_point[j]] > 0)
                odolSection.num_vertices++;
        }

        odolSection.num_vertex_weights = odolSection.num_vertices;

        odolSection.vertices.resize(odolSection.num_vertices);
        for (j = 0; j < odolSection.num_vertices; j++)
            odolSection.vertices[j] = NOPOINT;

        odolSection.vertex_weights.resize(odolSection.num_vertex_weights);

        for (j = 0; j < odol_lod.num_points; j++) {
            if (mlodSection.points[odol_lod.vertex_to_point[j]] == 0)
                continue;

            for (k = 0; k < odolSection.num_vertices; k++) {
                if (odolSection.vertices[k] == NOPOINT)
                    break;
            }
            odolSection.vertices[k] = j;
            odolSection.vertex_weights[k] = mlodSection.points[odol_lod.vertex_to_point[j]];
        }
    }

    // Proxies
    odol_lod.num_proxies = 0;
    for (i = 0; i < mlod_lod.num_selections; i++) {
        if (strncmp(mlod_lod.selections[i].name.c_str(), "proxy:", 6) == 0)
            odol_lod.num_proxies++;
    }
    odol_lod.proxies.resize(odol_lod.num_proxies);
    k = 0;
    for (i = 0; i < mlod_lod.num_selections; i++) {
        if (strncmp(mlod_lod.selections[i].name.c_str(), "proxy:", 6) != 0)
            continue;

        for (j = 0; j < odol_lod.num_faces; j++) {
            if (mlod_lod.selections[i].faces[odol_lod.face_lookup[j]] > 0) {
                face = j;
                break;
            }
        }

        if (j >= mlod_lod.num_faces) {
            logger.warning(current_target, -1, "no-proxy-face", "No face found for proxy \"%s\".\n", mlod_lod.selections[i].name.c_str() + 6);
            odol_lod.num_proxies--;
            continue;
        }

        odol_lod.proxies[k].name = mlod_lod.selections[i].name.substr(6);
        odol_lod.proxies[k].proxy_id = strtol(strrchr(odol_lod.proxies[k].name.c_str(), '.') + 1, &ptr, 10);
        auto newLength = strrchr(odol_lod.proxies[k].name.c_str(), '.') - odol_lod.proxies[k].name.data();
        odol_lod.proxies[k].name.resize(newLength);
        std::transform(odol_lod.proxies[k].name.begin(), odol_lod.proxies[k].name.end(), odol_lod.proxies[k].name.begin(), tolower);

        odol_lod.proxies[k].selection_index = i;
        odol_lod.proxies[k].bone_index = -1;

        if (!odol_lod.vertexboneref.empty() &&
                odol_lod.vertexboneref[odol_lod.faces[face].table[0]].num_bones > 0) {
            odol_lod.proxies[k].bone_index = odol_lod.vertexboneref[odol_lod.faces[face].table[0]].weights[0][0];
        }

        for (j = 0; j < odol_lod.num_sections; j++) {
            if (face > odol_lod.sections[j].face_start)
                continue;
            odol_lod.proxies[k].section_index = j;
            break;
        }

        odol_lod.proxies[k].transform_y = (
            mlod_lod.points[mlod_lod.faces[odol_lod.face_lookup[face]].table[1].points_index].getPosition()
            -
            mlod_lod.points[mlod_lod.faces[odol_lod.face_lookup[face]].table[0].points_index].getPosition());
        odol_lod.proxies[k].transform_y = odol_lod.proxies[k].transform_y.normalize();

        odol_lod.proxies[k].transform_z = (
            mlod_lod.points[mlod_lod.faces[odol_lod.face_lookup[face]].table[2].points_index].getPosition()
            -
            mlod_lod.points[mlod_lod.faces[odol_lod.face_lookup[face]].table[0].points_index].getPosition());
        odol_lod.proxies[k].transform_z = odol_lod.proxies[k].transform_z.normalize();

        odol_lod.proxies[k].transform_x =
            odol_lod.proxies[k].transform_y.cross(odol_lod.proxies[k].transform_z);

        odol_lod.proxies[k].transform_n =
            mlod_lod.points[mlod_lod.faces[odol_lod.face_lookup[face]].table[0].points_index].getPosition();

        k++;
    }

    // Properties
    odol_lod.num_properties = mlod_lod.properties.size();
    odol_lod.properties = mlod_lod.properties;

    odol_lod.num_frames = 0; // @todo
    odol_lod.frames = 0;

    odol_lod.icon_color = 0xff9d8254;
    odol_lod.selected_color = 0xff9d8254;

    odol_lod.flags = 0;
}


void model_info::writeTo(std::ostream& output) {
    auto num_lods = lod_resolutions.size();

    output.write(reinterpret_cast<char*>(lod_resolutions.data()), sizeof(float) * num_lods);
    WRITE_CASTED(index, sizeof(uint32_t));
    WRITE_CASTED(bounding_sphere, sizeof(float));
    WRITE_CASTED(geo_lod_sphere, sizeof(float));
    output.write(reinterpret_cast<char*>(point_flags), sizeof(uint32_t) * 3);
    WRITE_CASTED(aiming_center, sizeof(vector3));
    WRITE_CASTED(map_icon_color, sizeof(uint32_t));
    WRITE_CASTED(map_selected_color, sizeof(uint32_t));
    WRITE_CASTED(view_density, sizeof(float));
    WRITE_CASTED(bbox_min, sizeof(vector3));
    WRITE_CASTED(bbox_max, sizeof(vector3));
    WRITE_CASTED(lod_density_coef, sizeof(float));
    WRITE_CASTED(draw_importance, sizeof(float));
    WRITE_CASTED(bbox_visual_min, sizeof(vector3));
    WRITE_CASTED(bbox_visual_max, sizeof(vector3));
    WRITE_CASTED(bounding_center, sizeof(vector3));
    WRITE_CASTED(geometry_center, sizeof(vector3));
    WRITE_CASTED(centre_of_mass, sizeof(vector3));
    WRITE_CASTED(inv_inertia, sizeof(matrix));
    WRITE_CASTED(autocenter, sizeof(bool));
    WRITE_CASTED(lock_autocenter, sizeof(bool));
    WRITE_CASTED(can_occlude, sizeof(bool));
    WRITE_CASTED(can_be_occluded, sizeof(bool));
#if P3DVERSION > 71
    WRITE_CASTED(ai_cover,            sizeof(bool));  // v73
#endif
    WRITE_CASTED(skeleton->ht_min, sizeof(float));
    WRITE_CASTED(skeleton->ht_max, sizeof(float));
    WRITE_CASTED(skeleton->af_max, sizeof(float));
    WRITE_CASTED(skeleton->mf_max, sizeof(float));
    WRITE_CASTED(skeleton->mf_act, sizeof(float));
    WRITE_CASTED(skeleton->t_body, sizeof(float));
    WRITE_CASTED(force_not_alpha, sizeof(bool));
    WRITE_CASTED(sb_source, sizeof(int32_t));
    WRITE_CASTED(prefer_shadow_volume, sizeof(bool));
    WRITE_CASTED(shadow_offset, sizeof(float));
    WRITE_CASTED(animated, sizeof(bool));
    skeleton->writeTo(output);
    WRITE_CASTED(map_type, sizeof(char));
    WRITE_CASTED(n_floats, sizeof(uint32_t)); //_massArray size
    //! array of mass assigned to all points of geometry level
    //! this is used to calculate angular inertia tensor (_invInertia)


    //output.write("\0\0\0\0\0", 4); // compression header for empty array
    WRITE_CASTED(mass, sizeof(float));
    WRITE_CASTED(mass_reciprocal, sizeof(float));
    WRITE_CASTED(armor, sizeof(float));
    WRITE_CASTED(inv_armor, sizeof(float));
    WRITE_CASTED(special_lod_indices, sizeof(struct lod_indices));
    WRITE_CASTED(min_shadow, sizeof(uint32_t));
    WRITE_CASTED(can_blend, sizeof(bool));
    WRITE_CASTED(class_type, sizeof(char));
    WRITE_CASTED(destruct_type, sizeof(char));
    WRITE_CASTED(property_frequent, sizeof(bool));
    WRITE_CASTED(always_0, sizeof(uint32_t)); //@todo Array of unused Selection Names

#if P3DVERSION > 71
    // v73 adds another 4 bytes here
    output.write("\0\0\0\0", 4); // v73 data
#endif

    //first lodNumber*ShadowVolume then  lodNumber*ShadowBuffer then  lodNumber*ShadowBufferLodVis
    //for each it's a 32bit int. -1 signifies unknown
    //sets preferredShadowVolumeLod, preferredShadowBufferLod, and preferredShadowBufferLodVis for each LOD
    for (int i = 0; i < num_lods; i++)
        output.write("\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", 12);
}

void calculate_axis(struct animation *anim, uint32_t num_lods, std::vector<mlod_lod> &mlod_lods) {
    /*
     * Gets the absolute axis position and direction for the given rotation
     * or translation animations.
     *
     * At the moment, all axis selections are expected to be in the memory LOD.
     */

    int i;
    int j;
    int k;

    anim->axis_pos = empty_vector;
    anim->axis_dir = empty_vector;

    if (anim->axis.empty() && anim->begin.empty() && anim->end.empty())
        return;

    for (i = 0; i < num_lods; i++) {
        if (mlod_lods[i].resolution == LOD_MEMORY)
            break;
    }
    if (i == num_lods)
        return;

    for (j = 0; j < mlod_lods[i].num_selections; j++) {
        if (anim->axis.empty()) {
            if (stricmp(mlod_lods[i].selections[j].name.c_str(), anim->begin.c_str()) == 0) {
                for (k = 0; k < mlod_lods[i].num_points; k++) {
                    if (mlod_lods[i].selections[j].points[k] > 0) {
                        anim->axis_pos = mlod_lods[i].points[k].getPosition();
                        break;
                    }
                }
            }
            if (stricmp(mlod_lods[i].selections[j].name.c_str(), anim->end.c_str()) == 0) {
                for (k = 0; k < mlod_lods[i].num_points; k++) {
                    if (mlod_lods[i].selections[j].points[k] > 0) {
                        anim->axis_dir = mlod_lods[i].points[k].getPosition();
                        break;
                    }
                }
            }
        } else if (stricmp(mlod_lods[i].selections[j].name.c_str(), anim->axis.c_str()) == 0) {
            for (k = 0; k < mlod_lods[i].num_points; k++) {
                if (mlod_lods[i].selections[j].points[k] > 0) {
                    anim->axis_pos = mlod_lods[i].points[k].getPosition();
                    break;
                }
            }
            for (k = k + 1; k < mlod_lods[i].num_points; k++) {
                if (mlod_lods[i].selections[j].points[k] > 0) {
                    anim->axis_dir = mlod_lods[i].points[k].getPosition();
                    break;
                }
            }
        }
    }

    anim->axis_dir = anim->axis_dir - anim->axis_pos;

    if (anim->type == AnimationType::ROTATION_X || anim->type == AnimationType::ROTATION_Y || anim->type == AnimationType::ROTATION_Z) {
        anim->axis_pos = anim->axis_pos + anim->axis_dir*0.5;
        anim->axis_dir = empty_vector;
        anim->axis_dir.x = 1.0f;
    } else if (anim->type == AnimationType::ROTATION) {
        anim->axis_dir = anim->axis_dir.normalize();
    } else {
        anim->axis_pos = empty_vector;
    }
}


void P3DFile::write_animations(std::ostream& output) {
    int i;
    int j;
    int k;
    uint32_t num;
    int32_t index;
    struct animation *anim;

    // Write animation classes
    WRITE_CASTED(model_info.skeleton->num_animations, sizeof(uint32_t));
    for (i = 0; i < model_info.skeleton->num_animations; i++) {
        anim = &model_info.skeleton->animations[i];
        WRITE_CASTED(anim->type, sizeof(uint32_t), 1, f_target);
        output.write(anim->name.c_str(), anim->name.length() + 1);
        output.write(anim->source.c_str(), anim->source.length() + 1);
        WRITE_CASTED(anim->min_value, sizeof(float), 1, f_target);
        WRITE_CASTED(anim->max_value, sizeof(float), 1, f_target);
        WRITE_CASTED(anim->min_value, sizeof(float), 1, f_target);
        WRITE_CASTED(anim->max_value, sizeof(float), 1, f_target);
        //WRITE_CASTED(anim->min_phase, sizeof(float), 1, f_target);
        //WRITE_CASTED(anim->max_phase, sizeof(float), 1, f_target);
        WRITE_CASTED(anim->junk, sizeof(uint32_t), 1, f_target);
        WRITE_CASTED(anim->always_0, sizeof(uint32_t), 1, f_target);
        WRITE_CASTED(anim->source_address, sizeof(uint32_t), 1, f_target);

        switch (anim->type) {
            case AnimationType::ROTATION:
            case AnimationType::ROTATION_X:
            case AnimationType::ROTATION_Y:
            case AnimationType::ROTATION_Z:
                WRITE_CASTED(anim->angle0, sizeof(float));
                WRITE_CASTED(anim->angle1, sizeof(float));
                break;
            case AnimationType::TRANSLATION:
            case AnimationType::TRANSLATION_X:
            case AnimationType::TRANSLATION_Y:
            case AnimationType::TRANSLATION_Z:
                WRITE_CASTED(anim->offset0, sizeof(float));
                WRITE_CASTED(anim->offset1, sizeof(float));
                break;
            case AnimationType::DIRECT:
                WRITE_CASTED(anim->axis_pos, sizeof(vector3));
                WRITE_CASTED(anim->axis_dir, sizeof(vector3));
                WRITE_CASTED(anim->angle, sizeof(float));
                WRITE_CASTED(anim->axis_offset, sizeof(float));
                break;
            case AnimationType::HIDE:
                WRITE_CASTED(anim->hide_value, sizeof(float));
                WRITE_CASTED(anim->unhide_value, sizeof(float));
                break;
        }
    }

    // Write bone2anim and anim2bone lookup tables
    WRITE_CASTED(num_lods, sizeof(uint32_t));

    // bone2anim
    for (i = 0; i < num_lods; i++) {
        WRITE_CASTED(model_info.skeleton->num_bones, sizeof(uint32_t));
        for (j = 0; j < model_info.skeleton->num_bones; j++) {
            num = 0;
            for (k = 0; k < model_info.skeleton->num_animations; k++) {
                anim = &model_info.skeleton->animations[k];
                if (stricmp(anim->selection.c_str(), model_info.skeleton->bones[j].name.c_str()) == 0)
                    num++;
            }

            WRITE_CASTED(num, sizeof(uint32_t));

            for (k = 0; k < model_info.skeleton->num_animations; k++) {
                anim = &model_info.skeleton->animations[k];
                if (stricmp(anim->selection.c_str(), model_info.skeleton->bones[j].name.c_str()) == 0) {
                    num = (uint32_t)k;
                    WRITE_CASTED(num, sizeof(uint32_t));
                }
            }
        }
    }

    // anim2bone
    for (i = 0; i < num_lods; i++) {
        for (j = 0; j < model_info.skeleton->num_animations; j++) {
            anim = &model_info.skeleton->animations[j];

            index = -1;
            for (k = 0; k < model_info.skeleton->num_bones; k++) {
                if (stricmp(anim->selection.c_str(), model_info.skeleton->bones[k].name.c_str()) == 0) {
                    index = (int32_t)k;
                    break;
                }
            }

            WRITE_CASTED(index, sizeof(int32_t));

            if (index == -1) {
                if (i == 0) { // we only report errors for the first LOD
                    logger.warning(current_target, -1, LoggerMessageType::unknown_bone, "Failed to find bone \"%s\" for animation \"%s\".\n",
                            model_info.skeleton->bones[k].name.c_str(), anim->name.c_str());
                }
                continue;
            }

            if (anim->type == AnimationType::DIRECT || anim->type == AnimationType::HIDE)
                continue;

            calculate_axis(anim, num_lods, mlod_lods);

            if (model_info.autocenter)
                anim->axis_pos = anim->axis_pos - model_info.centre_of_mass;

            WRITE_CASTED(anim->axis_pos, sizeof(vector3));
            WRITE_CASTED(anim->axis_dir, sizeof(vector3));
        }
    }
}


void P3DFile::get_mass_data() {
    int i;
    float mass;
    vector3 sum;
    vector3 pos;
    matrix inertia;
    matrix r_tilda;
    struct mlod_lod *mass_lod;

    // mass is primarily stored in geometry
    for (i = 0; i < num_lods; i++) {
        if (mlod_lods[i].resolution == LOD_GEOMETRY)
            break;
    }

    // alternatively use the PhysX LOD
    if (i >= num_lods || mlod_lods[i].num_points == 0) {
        for (i = 0; i < num_lods; i++) {
            if (mlod_lods[i].resolution == LOD_PHYSX)
                break;
        }
    }

    // mass data available?
    if (i >= num_lods || mlod_lods[i].num_points == 0) {
        model_info.mass = 0;
        model_info.mass_reciprocal = 1;
        model_info.inv_inertia = identity_matrix;
        model_info.centre_of_mass = empty_vector;
        return;
    }

    mass_lod = &mlod_lods[i];
    sum = empty_vector;
    mass = 0;
    for (i = 0; i < mass_lod->num_points; i++) {
        pos = mass_lod->points[i].getPosition();
        mass += mass_lod->mass[i];
        sum += pos * mass_lod->mass[i];
    }

    model_info.centre_of_mass = (mass > 0) ? sum * (1 / mass) : empty_vector;

    inertia = empty_matrix;
    for (i = 0; i < mass_lod->num_points; i++) {
        pos = mass_lod->points[i].getPosition();
        r_tilda = vector_tilda(pos - model_info.centre_of_mass);
        inertia = matrix_sub(inertia, matrix_mult_scalar(mass_lod->mass[i], matrix_mult(r_tilda, r_tilda)));
    }

    // apply calculations to modelinfo
    model_info.mass = mass;
    if (mass > 0) {
        model_info.mass_reciprocal = 1 / mass;
        //model_info->inv_inertia = matrix_inverse(inertia);
        model_info.inv_inertia = empty_matrix;
        model_info.inv_inertia.m00 = 1.0f / inertia.m00;
        model_info.inv_inertia.m11 = 1.0f / inertia.m11;
        model_info.inv_inertia.m22 = 1.0f / inertia.m22;
    }
    else {
        model_info.mass_reciprocal = 1;
        model_info.inv_inertia = identity_matrix;
    }
}


void P3DFile::build_model_info() {
    int i;
    int j;
    float sphere;
    vector3 bbox_total_min;
    vector3 bbox_total_max;

    model_info.lod_resolutions.resize(num_lods);

    for (i = 0; i < num_lods; i++)
        model_info.lod_resolutions[i] = mlod_lods[i].resolution;

    model_info.index = 0;

    model_info.autocenter = false;
    model_info.lock_autocenter = false; // @todo
    model_info.can_occlude = false; // @todo
    model_info.can_be_occluded = true; // @todo
    model_info.ai_cover = false; // @todo
    model_info.animated = false; // @todo

    for (i = 0; i < num_lods; i++) {
        auto& properties = mlod_lods[i].properties;
        auto found = std::find_if(properties.begin(), properties.end(), [](const auto& prop) {
            return prop.name == "autocenter" && prop.value == "1";
        });

        if (found != properties.end())
            model_info.autocenter = true;
    }

    // Bounding box & center
    getBoundingBox(bbox_total_min, bbox_total_max, false, false);

    model_info.bounding_center = (bbox_total_min + bbox_total_max) * 0.5f;

    if (!model_info.autocenter)
        model_info.bounding_center = empty_vector;

    model_info.bbox_min = bbox_total_min - model_info.bounding_center;
    model_info.bbox_max = bbox_total_max - model_info.bounding_center;

    model_info.lod_density_coef = 1.0f; // @todo
    model_info.draw_importance = 1.0f; // @todo

    // Visual bounding box
    getBoundingBox(model_info.bbox_visual_min, model_info.bbox_visual_max, true, false);

    model_info.bbox_visual_min = model_info.bbox_visual_min - model_info.bounding_center;
    model_info.bbox_visual_max = model_info.bbox_visual_max - model_info.bounding_center;

    // Geometry center
    getBoundingBox(bbox_total_min, bbox_total_max, false, true);

    model_info.geometry_center = (bbox_total_min + bbox_total_max) * 0.5f - model_info.bounding_center;

    // Centre of mass, inverse inertia, mass and inverse mass
    get_mass_data();

    // Aiming Center
    // @todo: i think this uses the fire geo lod if available
    model_info.aiming_center = model_info.geometry_center;

    // Spheres
    model_info.bounding_sphere = 0.0f;
    model_info.geo_lod_sphere = 0.0f;
    for (i = 0; i < num_lods; i++) {
        if (model_info.autocenter)
            sphere = mlod_lods[i].getBoundingSphere(model_info.centre_of_mass);
        else
            sphere = mlod_lods[i].getBoundingSphere(empty_vector);

        if (sphere > model_info.bounding_sphere)
            model_info.bounding_sphere = sphere;
        if (mlod_lods[i].resolution == LOD_GEOMETRY)
            model_info.geo_lod_sphere = sphere;
    }

    memset(model_info.point_flags, 0, sizeof(model_info.point_flags));

    model_info.map_icon_color = 0xff9d8254;
    model_info.map_selected_color = 0xff9d8254;

    model_info.view_density = -100.0f; // @todo

    model_info.force_not_alpha = false; //@todo
    model_info.sb_source = 0; //@todo
    model_info.prefer_shadow_volume = false; //@todo
    model_info.shadow_offset = 1.0f; //@todo
    model_info.skeleton = std::make_unique<skeleton_>();
    memset(model_info.skeleton.get(), 0, sizeof(struct skeleton_));

    model_info.map_type = 22; //@todo
    model_info.n_floats = 0;

    model_info.armor = 200.0f; // @todo
    model_info.inv_armor = 0.005f; // @todo

    // the real indices to the lods are calculated by the engine when the model is loaded
    memset(&model_info.special_lod_indices, 255, sizeof(struct lod_indices));
    for (i = 0; i < num_lods; i++) {
        if (mlod_lods[i].resolution == LOD_MEMORY)
            model_info.special_lod_indices.memory = i;
        if (mlod_lods[i].resolution == LOD_GEOMETRY)
            model_info.special_lod_indices.geometry = i;
        if (mlod_lods[i].resolution == LOD_PHYSX)
            model_info.special_lod_indices.geometry_physx = i;
        if (mlod_lods[i].resolution == LOD_FIRE_GEOMETRY)
            model_info.special_lod_indices.geometry_fire = i;
        if (mlod_lods[i].resolution == LOD_VIEW_GEOMETRY)
            model_info.special_lod_indices.geometry_view = i;
        if (mlod_lods[i].resolution == LOD_VIEW_PILOT_GEOMETRY)
            model_info.special_lod_indices.geometry_view_pilot = i;
        if (mlod_lods[i].resolution == LOD_VIEW_GUNNER_GEOMETRY)
            model_info.special_lod_indices.geometry_view_gunner = i;
        if (mlod_lods[i].resolution == LOD_VIEW_CARGO_GEOMETRY)
            model_info.special_lod_indices.geometry_view_cargo = i;
        if (mlod_lods[i].resolution == LOD_LAND_CONTACT)
            model_info.special_lod_indices.land_contact = i;
        if (mlod_lods[i].resolution == LOD_ROADWAY)
            model_info.special_lod_indices.roadway = i;
        if (mlod_lods[i].resolution == LOD_PATHS)
            model_info.special_lod_indices.paths = i;
        if (mlod_lods[i].resolution == LOD_HITPOINTS)
            model_info.special_lod_indices.hitpoints = i;
    }

    if (model_info.special_lod_indices.geometry_view == -1)
        model_info.special_lod_indices.geometry_view = model_info.special_lod_indices.geometry;

    if (model_info.special_lod_indices.geometry_fire == -1)
        model_info.special_lod_indices.geometry_fire = model_info.special_lod_indices.geometry;

    // according to BIS: first LOD that can be used for shadowing
    model_info.min_shadow = num_lods; // @todo
    model_info.can_blend = false; // @todo
    model_info.class_type = 0; // @todo
    model_info.destruct_type = 0; // @todo
    model_info.property_frequent = false; //@todo
    model_info.always_0 = 0;
}

int P3DFile::read_lods(std::istream &source, uint32_t num_lods) {
    /*
     * Reads all LODs (starting at the current position of f_source) into
     * the given LODs array.
     *
     * Returns number of read lods on success and a negative integer on
     * failure.
     */

    char buffer[5];

    source.seekg(0);

    source.read(buffer, 4);
    buffer[4] = 0;
    if (stricmp(buffer, "MLOD"))
        return 0;

    source.seekg(12);

    for (uint32_t i = 0; i < num_lods; i++) {
        source.read(buffer, 4);
        if (strncmp(buffer, "P3DM", 4) != 0)
            return 0;

        mlod_lod newLod;

        if (!newLod.read(source)) return 0;

        mlod_lods.emplace_back(std::move(newLod));
    }

    return mlod_lods.size();
}


void P3DFile::getBoundingBox(vector3 &bbox_min, vector3 &bbox_max, bool visual_only, bool geometry_only) {
    /*
     * Calculate the bounding box for the given LODs and stores it
     * in the given triplets.
     */
    bool first = true;

    for (auto& lod : mlod_lods) {
        if (lod.resolution > LOD_GRAPHICAL_END && visual_only)
            continue;

        if ((lod.resolution != LOD_GEOMETRY) && geometry_only)
            continue;

        for (auto&[x, y, z, flags] : lod.points) {
            if (first || x < bbox_min.x)
                bbox_min.x = x;
            if (first || x > bbox_max.x)
                bbox_max.x = x;

            if (first || y < bbox_min.y)
                bbox_min.y = y;
            if (first || y > bbox_max.y)
                bbox_max.y = y;

            if (first || z < bbox_min.z)
                bbox_min.z = z;
            if (first || z > bbox_max.z)
                bbox_max.z = z;

            first = false;
        }
    }
}

int P3DFile::readMLOD(std::filesystem::path sourceFile) {
    std::ifstream input(sourceFile, std::ifstream::binary);


    if (!input.is_open()) {
        logger.error(sourceFile.string(), 0, "Failed to open source file.\n");
        return 2;
    }

    current_target = sourceFile.string();

    char typeBuffer[5];
    input.read(typeBuffer, 5);

    if (strncmp(typeBuffer, "MLOD", 4) != 0) {
        if (strcmp(args.positionals[0], "binarize") == 0)
            logger.error(sourceFile.string(), 0, "Source file is not MLOD.\n");
        return -3;
    }

    input.seekg(8);

    input.read(reinterpret_cast<char*>(&num_lods), 4);

    num_lods = read_lods(input, num_lods);
    if (num_lods <= 0) {
        logger.error(sourceFile.string(), 0, "Failed to read LODs.\n");
        return 4;
    }

    // Write model info
    build_model_info();
    auto success = model_info.skeleton->read(sourceFile, logger);
    if (success > 0) {
        logger.error(sourceFile.string(), 0, "Failed to read model config.\n");
        return success;
    }
    return 0;
}

int P3DFile::writeODOL(std::filesystem::path targetFile) {
    std::ofstream output(targetFile, std::ofstream::binary);


    if (!output.is_open()) {
        logger.error(targetFile.string(), 0, "Failed to open target file.\n");
        return 1;
    }



    //use targetFile
    current_target = targetFile.string();

    // Write header
    output.write("ODOL", 4);
    uint32_t version = P3DVERSION;
    output.write(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    output.write("\0\0\0\0", sizeof(uint32_t)); // AppID
    output.write("\0", 1); // muzzleFlash string

    output.write(reinterpret_cast<char*>(&num_lods), 4);

    if (model_info.lod_resolutions.size() != num_lods)
        __debugbreak();

    model_info.writeTo(output);

    // Write animations
    if (model_info.skeleton->num_animations > 0) {
        output.put(1); //bool hasAnims
        write_animations(output);
    } else {
        output.put(0);
    }

    // Write place holder LOD addresses
    size_t fp_lods_starts = output.tellp();

    //lod start addresses
    for (uint32_t i = 0; i < num_lods; i++)
        output.write("\0\0\0\0", 4);
    //lod end addresses
    size_t fp_lods_ends = output.tellp();
    for (uint32_t i = 0; i < num_lods; i++)
        output.write("\0\0\0\0", 4);

    //^ that there is structured like the following
    //lod start
    //lod start
    //lod start
    //...
    //lod end
    //lod end
    //lod end


    // Write LOD face defaults (or rather, don't)
    for (uint32_t i = 0; i < num_lods; i++)
        output.put(1); //bool array, telling engine if it should load the LOD right away, or rather stream it in later

    // Write LODs
    for (uint32_t i = 0; i < num_lods; i++) {
        // Write start address
        auto fp_temp = output.tellp();
        output.seekp(fp_lods_starts + i * 4);
        output.write(reinterpret_cast<char*>(&fp_temp), 4);
        output.seekp(0, std::ofstream::end);

        // Convert to ODOL
        odol_lod odol_lod{};
        convert_lod(mlod_lods[i], odol_lod);

        // Write to file
        odol_lod.writeTo(output);

        // Write end address
        fp_temp = output.tellp();
        output.seekp(fp_lods_ends + i * 4);
        output.write(reinterpret_cast<char*>(&fp_temp), 4);
        output.seekp(0, std::ofstream::end);
    }

    // Write PhysX (@todo)

    output.write("\x00\x03\x03\x03\x00\x00\x00\x00", 8);
    output.write("\x00\x03\x03\x03\x00\x00\x00\x00", 8);
    output.write("\x00\x00\x00\x00\x00\x03\x03\x03", 8);
    output.write("\x00\x00\x00\x00\x00\x03\x03\x03", 8);
    output.write("\x00\x00\x00\x00", 4);
}








int mlod2odol(const char *source, const char *target, Logger& logger) {
    /*
     * Converts the MLOD P3D to ODOL. Overwrites the target if it already
     * exists.
     *
     * Returns 0 on success and a positive integer on failure.
     */

    __itt_task_begin(p3dDomain, __itt_null, __itt_null, handle_mlod2odol);

    P3DFile f(logger);
    f.readMLOD(source);
    f.writeODOL(target);


    __itt_task_end(p3dDomain);
    return 0;
}
