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
#include <unordered_set>


template <typename Type>
class StreamFixup {
    std::streamoff offset;
    Type value;
public:
    StreamFixup(std::ostream& str) : offset(str.tellp()) { str.write(reinterpret_cast<const char*>(&value), sizeof(Type)); }
    void setValue(const Type& val) { value = val; }
    void write(std::ostream& str) { str.seekp(offset); str.write(reinterpret_cast<const char*>(&value), sizeof(Type)); }
};



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
        auto dist = (points[i].getPosition() - center).magnitude_squared();
        if (dist > sphere)
            sphere = dist;
    }
    __itt_task_end(p3dDomain);

    return sqrt(sphere);
}

bool mlod_lod::read(std::istream& source, Logger& logger) {

    //4 byte int header size
    //4 byte int version
    source.seekg(8, std::istream::cur);
    source.read(reinterpret_cast<char*>(&num_points), 4);
    source.read(reinterpret_cast<char*>(&num_facenormals), 4);
    source.read(reinterpret_cast<char*>(&num_faces), 4);
    //4 byte int flags
    source.seekg(4, std::istream::cur);


    std::vector<bool> hiddenPoints;
    hiddenPoints.resize(num_points);


    bool empty = num_points == 0;
#pragma region Basic Geometry
    if (empty) {
        num_points = 1;
        points.resize(1);
        points[0].x = 0.0f;
        points[0].y = 0.0f;
        points[0].z = 0.0f;
        points[0].point_flags = 0;
    } else {
        points.resize(num_points);
        for (int j = 0; j < num_points; j++) {
            source.read(reinterpret_cast<char*>(&points[j]), sizeof(struct point));
            //report invalid point flags? not really needed.

            //#TODO hidden points support
            //If any flags are set, set hints and use them for `clip` array
            //There is also "hidden" array for POINT_SPECIAL_HIDDEN


            if (points[j].point_flags & 0x1000000) { //#TODO enum or #define or smth
                hiddenPoints[j] = true;
            }

        }
    }

    facenormals.resize(num_facenormals);
    //#TODO read all in one go
    for (int j = 0; j < num_facenormals; j++) {
        source.read(reinterpret_cast<char*>(&facenormals[j]), sizeof(vector3)); //#TODO if Geometry lod and NoNormals (geoOnly parameter), set them all to 0
    }

#pragma region FacesAndTexturesAndMaterials

    std::vector<mlod_face> loadingFaces;

    faces.resize(num_faces);
    for (int j = 0; j < num_faces; j++) {
        //4b face type
        //4x pseudovertex
        source.read(reinterpret_cast<char*>(&faces[j]), 72);
        //#TODO seperate face properties array
        //texture, material, special
        size_t fp_tmp = source.tellg();
        std::string textureName;
        std::string materialName;


        std::getline(source, textureName, '\0');

        std::getline(source, materialName, '\0');
        //#TODO tolower texture and material
        //#TODO check for valid flags
        faces[j].section_names = ""; //#TODO section_names shouldn't be here

        //#TODO make seperate array with faceproperties (material,texture index and flags


        //#REFACTOR make a custom array type for this. Give it a "AddUniqueElement" function that get's the string.
        //It will return index if it has one, or create one and then return index of new one
        if (!textureName.empty()) {
            auto foundTex = std::find_if(textures.begin(), textures.end(), textureName);

            __debugbreak(); //Check if this stuff is correct.
            if (foundTex == textures.end()) {
                faces[j].texture_index = faces.size();
                textures.emplace_back(textureName);
            }
            else {
                faces[j].texture_index = foundTex - textures.begin();
            }
        }
        
        if (!materialName.empty()) {
            auto foundMat = std::find_if(materials.begin(), materials.end(), [&searchName = materialName](const Material& mat) {
                return searchName == mat.path;
            });

            __debugbreak(); //Check if this stuff is correct.
            if (foundMat == materials.end()) {
                faces[j].material_index = materials.size();



                Material mat(logger, materialName);

                //#TODO take current_target as parameter from p3d
                mat.read();//#TODO exceptions Though we don't seem to check for errors there? Maybe we should?
                //YES! We should! mat.read will fail if material file isn't found.
                materials.emplace_back(std::move(mat)); //#CHECK new element should be at index j now.

            } else {
                faces[j].material_index = foundMat - materials.begin();
            }
        }

        //check and fix UV
        //shape.cpp 1075
        //Set UV coords to 0 if Geo lod with nouv
    }

#pragma endregion FacesAndTexturesAndMaterials


    //2nd pass over faces, find min and maxUV
    //^ don't need that



    //#TODO turn faces into poly's here (odol_face) and only store them
    //3rd pass over faces
    //collect unique texture paths
    //collect all faces in a poly and fill vertexToPoint and pointToVertex tables
    //the poly object is done in odol, see Odol's "AddPoint"
    //Error if max vertex that are not duplicate (see odol Addpoint) is bigger than 32767 too many verticies
    //Poly object seems to be just the face with n points?

    for (auto& face : faces) {
        class Polygon {
        public:
            uint8_t face_type;
            std::array<uint32_t, 4> points;
        };
        class PolygonInfo { //Special data about a polygon
        public:
            int textureIndex;
            int materialIndex;
            int specialFlags;
            float areaOverTex[2];
            int order; //#TODO?
        };
        Polygon newPoly;
        newPoly.face_type = face.face_type;
        bool polyIsHidden = false;
        for (int i = 0; i < 4; ++i) {
            newPoly.points[i] = face.table[i].points_index;
            //#TODO polyIsHidden
        }
        PolygonInfo newPolyInfo;
        newPolyInfo.textureIndex = face.texture_index;
        newPolyInfo.materialIndex = face.material_index;

        int specialFlags = 0;


        if (face.face_flags & 123123123) {//#TODO fix shape.cpp L1174

            if (face.face_flags & 0x8) specialFlags |= 64; //Is shadow
            if (face.face_flags & 0x10) specialFlags |= 32; //no shadow

            //#TODO zbias 

        }

        //#TODO apply material flags Line 1345
        //#TODO apply texture flags //line 1352

        if (polyIsHidden) {
            specialFlags |= 0x400000;
        }
         //#TODO if not loading UV (geometry lod) set no clamp flags L1390
        newPolyInfo.specialFlags = specialFlags;

        //#TODO add poly to faces list
        //#TODO faceToOriginalFace
    }


#pragma endregion Basic Geometry


    //Unroll multipass materials? Don't think we need that

    // make sure all points are represented with vertices?


    //set special flags
    //RecalculateNormals





    min_pos = empty_vector;
    max_pos = empty_vector;

    //#TODO use bounding box calc func
    for (int i = 0; i < num_points; i++) {
        if (i == 0 || points[i].x < min_pos.x)
            min_pos.x = points[i].x;
        if (i == 0 || points[i].y < min_pos.y)
            min_pos.y = points[i].y;
        if (i == 0 || points[i].z < min_pos.z)
            min_pos.z = points[i].z;
        if (i == 0 || points[i].x > max_pos.x)
            max_pos.x = points[i].x;
        if (i == 0 || points[i].y > max_pos.y)
            max_pos.y = points[i].y;
        if (i == 0 || points[i].z > max_pos.z)
            max_pos.z = points[i].z;
    }

    autocenter_pos = (min_pos + max_pos) * 0.5f;

    boundingSphere = getBoundingSphere(autocenter_pos);




    //Calculate MinMax, boundingCenter and bRadius
    //Is currently in convert to odol


    char buffer[5];
    uint32_t tagg_len;
    source.read(buffer, 4);
    if (strncmp(buffer, "TAGG", 4) != 0)
        return false;

    num_sharp_edges = 0;
    properties.clear();

    while (true) { //read tags
        source.seekg(1, std::istream::cur); //#TODO startTag flag, signed char. special handling for <0 and 1
        std::string entry;


        std::getline(source, entry, '\0');
        //#TODO max length 1024. check for that
        source.read(reinterpret_cast<char*>(&tagg_len), 4);
        size_t fp_tmp = static_cast<size_t>(source.tellg()) + tagg_len;

        if (entry.front() == '#') {

            if (entry == "#Mass#") {
                if (empty) {
                    mass.resize(1);
                    mass[0] = 0.0f; //#TODO get rid of this Shouldn't be hard to detect empty mass array
                } else {
                    //#TODO check that tagSize matches?
                    mass.resize(num_points);
                    source.read(reinterpret_cast<char*>(mass.data()), sizeof(float) * num_points);
                }
            }
            //#Animation
            //#TODO ^
            

            //#UVSet
            //32 bit stageID. If >1 then unsupported
            //if == 1
            //else skip


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

            if (entry == "#EndOfFile#") //Might want to  source.seekg(fp_tmp); before break. Check if fp_temp == tellg and debugbreak if not
                break;
        } else {
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
            //Might want to  source.seekg(fp_tmp); before break. Check if fp_temp == tellg and debugbreak if not
        }
    }
    //if have phases (animation tag)
    //check for keyframe property. Needs to exist otherwise warning

    //Sort verticies if not geometryOnly


    num_selections = selections.size();

    source.read(reinterpret_cast<char*>(&resolution), 4);
    return true;
}

uint32_t mlod_lod::getAndHints() {
    return andHints;
}

uint32_t mlod_lod::getOrHints() {
    return orHints;
}

uint32_t mlod_lod::getSpecialFlags() {
    return special;
}

std::optional<std::string> mlod_lod::getProperty(std::string_view propName) const {
    auto foundProp = std::find_if(properties.begin(), properties.end(), [propName](const property& prop) {
        return prop.name == propName;
    });
    if (foundProp != properties.end() && !foundProp->value.empty())
        return foundProp->value;
    return {};
}

void odol_section::writeTo(std::ostream& output) {
    WRITE_CASTED(face_index_start, sizeof(uint32_t));
    WRITE_CASTED(face_index_end, sizeof(uint32_t));
    WRITE_CASTED(min_bone_index, sizeof(uint32_t));
    WRITE_CASTED(bones_count, sizeof(uint32_t));
    WRITE_CASTED(mat_dummy, sizeof(uint32_t)); //always 0
    WRITE_CASTED(common_texture_index, sizeof(uint16_t));
    WRITE_CASTED(common_face_flags, sizeof(uint32_t));
    WRITE_CASTED(material_index, sizeof(int32_t));
    if (material_index == -1)
        output.put(0); //#TODO surface material?
    WRITE_CASTED(num_stages, sizeof(uint32_t)); //#TODO has to be 2!!! Else assert trip
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
            for (j = 0; j < mlod_lod.num_selections; j++) {//#TODO find_if
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
    output.write(textures.c_str(), textures.length()+1);

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
    if (compare != 0) //#TODO this is stupid, just compare them
        return compare;

    return faces[a_index].section_names.compare(faces[b_index].section_names);
}


bool is_alpha(struct mlod_face *face) {
    return false; //#TODO fix this is_alpha function move to mlod_lod
    // @todo check actual texture maybe?
    //if (strstr(face->texture_name.c_str(), "_ca.paa") != NULL)
    //    return true;
    //if (strstr(face->texture_name.c_str(), "ca)") != NULL)
    //    return true;
    //return false;
}


void P3DFile::convert_lod(mlod_lod &mlod_lod, odol_lod &odol_lod) {
    extern std::string current_target;
    unsigned long i;
    unsigned long j;
    unsigned long k;
    unsigned long face;
    unsigned long face_start;
    unsigned long face_end;
    char *ptr;
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



    odol_lod.min_pos = mlod_lod.min_pos;
    odol_lod.max_pos = mlod_lod.max_pos;
    odol_lod.autocenter_pos = mlod_lod.autocenter_pos;
    odol_lod.sphere = mlod_lod.boundingSphere;


#pragma region TexturesAndMaterials 
    //#TODO merge this into one single LOD class.

    // Textures & Materials
    odol_lod.num_textures = mlod_lod.textures.size();
    odol_lod.num_materials = mlod_lod.materials.size();

    odol_lod.materials = mlod_lod.materials; 
    for (auto& tex : mlod_lod.textures) {
        odol_lod.textures += tex;
        odol_lod.textures.push_back('\0');
    }
    __debugbreak(); //check if the textures string is correct. null char delimited array of texture paths

#pragma endregion TexturesAndMaterials





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
        if (mlod_lod.faces[i].texture_index == -1)
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
        if (mlod_lod.faces[i].texture_index == -1) {
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


    //#TODO move ^ that to mlod read faces and textures and materials


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

    output.write(reinterpret_cast<char*>(lod_resolutions.data()), sizeof(float) * num_lods); //#TODO lod resolutions doesn't belong in here. Belongs to parent
    WRITE_CASTED(index, sizeof(uint32_t)); //#TODO these are special flags
    WRITE_CASTED(bounding_sphere, sizeof(float));
    WRITE_CASTED(geo_lod_sphere, sizeof(float));

    //#TODO
    WRITE_CASTED(remarks, sizeof(uint32_t));
    WRITE_CASTED(andHints, sizeof(uint32_t));
    WRITE_CASTED(orHints, sizeof(uint32_t));
 

    WRITE_CASTED(aiming_center, sizeof(vector3));
    WRITE_CASTED(map_icon_color, sizeof(uint32_t));
    WRITE_CASTED(map_selected_color, sizeof(uint32_t));
    WRITE_CASTED(view_density, sizeof(float));
    WRITE_CASTED(bbox_min, sizeof(vector3));
    WRITE_CASTED(bbox_max, sizeof(vector3));


    WRITE_CASTED(lod_density_coef, sizeof(float));//v70
    WRITE_CASTED(draw_importance, sizeof(float));//v71


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

#if P3DVERSION >= 73
    WRITE_CASTED(ai_cover,            sizeof(bool));  // v73
#endif

    WRITE_CASTED(skeleton->ht_min, sizeof(float));//>=v42
    WRITE_CASTED(skeleton->ht_max, sizeof(float));//>=v42
    WRITE_CASTED(skeleton->af_max, sizeof(float));//>=v42
    WRITE_CASTED(skeleton->mf_max, sizeof(float));//>=v42
    WRITE_CASTED(skeleton->mf_act, sizeof(float));//>=v43
    WRITE_CASTED(skeleton->t_body, sizeof(float));//>=v43
    WRITE_CASTED(force_not_alpha, sizeof(bool));//>=V33
    WRITE_CASTED(sb_source, sizeof(int32_t));//>=v37
    WRITE_CASTED(prefer_shadow_volume, sizeof(bool));//>=v37
    WRITE_CASTED(shadow_offset, sizeof(float));

    WRITE_CASTED(animated, sizeof(bool)); //AnimationTyoe == Software


    skeleton->writeTo(output);
    //#TODO check that animationType is hardware, if you print a skeleton


    WRITE_CASTED(map_type, sizeof(char));

    //#TODO mass array
    WRITE_CASTED(n_floats, sizeof(uint32_t)); //_massArray size
    //! array of mass assigned to all points of geometry level
    //! this is used to calculate angular inertia tensor (_invInertia)


    //output.write("\0\0\0\0\0", 4); // compression header for empty array
    WRITE_CASTED(mass, sizeof(float));
    WRITE_CASTED(mass_reciprocal, sizeof(float));
    WRITE_CASTED(armor, sizeof(float));
    WRITE_CASTED(inv_armor, sizeof(float));


    //#TODO check if this is correctly placed
#if P3DVERSION > 72
    WRITE_CASTED(explosionShielding, sizeof(float)); //v72
#endif


    WRITE_CASTED(special_lod_indices, sizeof(struct lod_indices));


    WRITE_CASTED(min_shadow, sizeof(uint32_t));
    WRITE_CASTED(can_blend, sizeof(bool));

    WRITE_CASTED(class_type, sizeof(char)); //#TODO propertyClass string
    WRITE_CASTED(destruct_type, sizeof(char)); //#TODO propertyDamage string
    WRITE_CASTED(property_frequent, sizeof(bool)); //#TODO is this properly read from odol?

    WRITE_CASTED(always_0, sizeof(uint32_t)); //@todo Array of unused Selection Names




    //#TODO
    preferredShadowVolumeLod;
    preferredShadowBufferLod;
    preferredShadowBufferVisibleLod;


    //#TODO ScanPreferredShadowLODs

    //#TODO _preferredShadowVolumeLod
    //#TODO _preferredShadowBufferLod
    //#TODO _preferredShadowBufferLodVis

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




std::optional<std::string> MultiLODShape::getPropertyGeo(std::string_view propName) {
    mlod_lod* ld;
        
    if (model_info.special_lod_indices.geometry.isDefined()) {
        auto& geomLod = mlod_lods[model_info.special_lod_indices.geometry];
        auto foundProp = geomLod.getProperty(propName); //#TODO properties MUST be lowercase. Else assert
        //#TODO also warn when reading properties from MLOD if they are non lowercase

        if (foundProp)
            return foundProp;
    }

    if (model_info.special_lod_indices.geometry.isNull() && mlod_lods.empty()) {
        //#TODO throw exception "No geometry and no shape"
        return {};
    }


    auto& lod0 = mlod_lods.front();
    auto foundProp = lod0.getProperty(propName);

    if (foundProp && model_info.special_lod_indices.geometry.isDefined()) {
        //#TODO warning Property %s not in geometry lod
    }

    return foundProp;
}

void MultiLODShape::scanProjectedShadow() {
    
    projectedShadow = false;
    if (!(model_info.index & OnSurface)) { //On surface can't use projected shadows
        if (model_info.min_shadow < model_info.numberGraphicalLods) {
            if (model_info.shadowVolume.isNull()) {
                projectedShadow = true;
            } else if (model_info.special_lod_indices.geometry.isDefined()) { //#TODO properly handle unset being -1 !!!!
                auto shadowProp = getPropertyGeo("shadow");
                if (shadowProp && *shadowProp == "hybrid")
                    projectedShadow = true;
            }
        }
    }


}

void MultiLODShape::BuildSpecialLodList() {
    memset(&model_info.special_lod_indices, 255, sizeof(struct lod_indices));
    model_info.numberGraphicalLods = num_lods;
    model_info.shadowVolumeCount = 0;
    model_info.shadowBufferCount = 0;

    for (uint32_t i = 0; i < num_lods; i++) {
        auto& resolution = mlod_lods[i].resolution;
        auto& indicies = model_info.special_lod_indices;

        if (resolution <= 900.f) continue;

        if (model_info.numberGraphicalLods > i) model_info.numberGraphicalLods = i;



        if (resolution == LOD_MEMORY) indicies.memory = i;
        if (resolution == LOD_GEOMETRY) indicies.geometry = i;
        if (resolution == LOD_GEOMETRY_SIMPLE) indicies.geometry_simple = i;
        if (resolution == LOD_PHYSX || resolution == LOD_PHYSX_OLD) indicies.geometry_physx = i;
        if (resolution == LOD_FIRE_GEOMETRY) indicies.geometry_fire = i;
        if (resolution == LOD_VIEW_GEOMETRY) indicies.geometry_view = i;
        if (resolution == LOD_VIEW_PILOT_GEOMETRY) indicies.geometry_view_pilot = i;
        if (resolution == LOD_VIEW_GUNNER_GEOMETRY) indicies.geometry_view_gunner = i;
        if (resolution == LOD_VIEW_CARGO_GEOMETRY) indicies.geometry_view_cargo = i;
        //#TODO print warning if geometryViewCommander exists
        //"%s: Geometry view commander lod found - obsolete"
        if (resolution == LOD_LAND_CONTACT) indicies.land_contact = i;
        if (resolution == LOD_ROADWAY) indicies.roadway = i;
        if (resolution == LOD_PATHS) indicies.paths = i;
        if (resolution == LOD_HITPOINTS) indicies.hitpoints = i;

        if (resolution > LOD_SHADOW_STENCIL_START && resolution < LOD_SHADOW_STENCIL_END) { //#TODO this is shadowvolume
            if (model_info.shadowVolume == -1) model_info.shadowVolume = i;
            model_info.shadowVolumeCount++;
        }
        if (resolution > LOD_SHADOW_VOLUME_START && resolution < LOD_SHADOW_VOLUME_END) {//#TODO this is shadowBuffer
            if (model_info.shadowBuffer == -1) model_info.shadowVolume = i;
            model_info.shadowBufferCount++;
        }
    }

    //#TODO use -1 for these. Make a wrapper class with functions for isDefined and such
    if (model_info.special_lod_indices.geometry.isDefined()) {
        mlod_lods[model_info.special_lod_indices.geometry].special |= IsAlpha | IsAlphaFog | IsColored;
    }
    if (model_info.special_lod_indices.geometry_simple.isDefined()) {
        mlod_lods[model_info.special_lod_indices.geometry_simple].special |= IsAlpha | IsAlphaFog | IsColored;
    }
    if (model_info.special_lod_indices.geometry_physx.isDefined()) {
        mlod_lods[model_info.special_lod_indices.geometry_physx].special |= IsAlpha | IsAlphaFog | IsColored;
    }
    if (model_info.special_lod_indices.geometry_view.isDefined()) {
        mlod_lods[model_info.special_lod_indices.geometry_view].special |= IsAlpha | IsAlphaFog | IsColored;
    }
    if (model_info.special_lod_indices.geometry_fire.isDefined()) {
        mlod_lods[model_info.special_lod_indices.geometry_fire].special |= IsAlpha | IsAlphaFog | IsColored;
    }

    if (model_info.special_lod_indices.geometry_view.isNull())
        model_info.special_lod_indices.geometry_view = model_info.special_lod_indices.geometry;

    if (model_info.special_lod_indices.geometry_fire.isNull())
        model_info.special_lod_indices.geometry_fire = model_info.special_lod_indices.geometry;

    if (model_info.special_lod_indices.geometry.isDefined()) {
        auto& lod = mlod_lods[model_info.special_lod_indices.geometry];
        auto fireGeoProp = lod.getProperty("firegeometry");
        auto viewGeoProp = lod.getProperty("viewgeometry");

        if (fireGeoProp && stoi(*fireGeoProp) > 0)
            model_info.special_lod_indices.geometry_fire = model_info.special_lod_indices.geometry;

        if (viewGeoProp && stoi(*viewGeoProp) > 0)
            model_info.special_lod_indices.geometry_view = model_info.special_lod_indices.geometry;
    }
}

void MultiLODShape::shapeListUpdated() {
    BuildSpecialLodList();
    scanProjectedShadow();




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


    //Bone mapping info below here

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
        mass += mass_lod->mass[i];//#TODO is this mass array?
        sum += pos * mass_lod->mass[i];
    }
    
    model_info.centre_of_mass = (mass > 0) ? sum * (1 / mass) : empty_vector;

    matrix inertia = empty_matrix;
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

void P3DFile::optimizeLODS() {
    
    for (int i = 0; i < num_lods; i++) {
        
        auto noTLProp = mlod_lods[i].getProperty("notl");

        //Here we want to erase lods. Remove from mlod_lods, model_info lod resolutions and decrement num_lods
        if (i != 0 && (mlod_lods[i].getAndHints()&ClipDecalMask)!=ClipDecalNone
            && mlod_lods[i].num_points > 0) {
            //VDecal
        } else if (noTLProp && stoi(*noTLProp) >0) {
            //TL dropped
        } else if (model_info.lod_resolutions[i] >= 20000 && model_info.lod_resolutions[i] <= 20999) {
            //Edit lods
        } else {
            //Keep lod and move up in lods array and lod_resolutions array
        }

    }


    int i = 0;
    for (i = 0; i < num_lods; i++) {
        if (model_info.lod_resolutions[i] >= 900) break;
        if (mlod_lods[i].num_faces < 1024) break;
    }

    if (i > 0 && model_info.lod_resolutions[i] >= 900) --i;

    model_info.min_shadow = i;

    for (int i = model_info.min_shadow; i < num_lods; i++)
    {
        if (model_info.lod_resolutions[i] > 900) break; // only normal LODs can be used for shadowing
        // disable shadows of too complex LODs
        auto LNSProp = mlod_lods[i].getProperty("lodnoshadow");
        if (LNSProp && stoi(*LNSProp) > 0) {
            model_info.min_shadow = i + 1;
            if (model_info.min_shadow >= num_lods || model_info.lod_resolutions[model_info.min_shadow] > 900) {
                model_info.min_shadow = num_lods;
            }
        }
    }
    if (model_info.min_shadow < num_lods) {
        int nFaces = mlod_lods[model_info.min_shadow].num_faces;
        if (nFaces >= 2000)
        {
            //error
            //("Too detailed shadow lod in %s (%d:%f : %d) - shadows disabled",
            //    modelname,
            //    model_info.min_shadow, model_info.lod_resolutions[model_info.min_shadow], nFaces
            //);
            model_info.min_shadow = num_lods;
        }
    }

    shapeListUpdated();



#pragma region Scan Shadow lods




    //#TODO scan shadow lods. shape.cpp 11641


#pragma endregion Scan Shadow lods



}


void P3DFile::build_model_info() {


    model_info.lod_resolutions.resize(num_lods);

    for (int i = 0; i < num_lods; i++)
        model_info.lod_resolutions[i] = mlod_lods[i].resolution;

    model_info.index = 0; //#TODO special flags. Is this init correct?

    //#TODO make these default values and get rid of this
    model_info.lock_autocenter = false; //#TODO this is always false!

    //#TODO treeCrownNeeded
    //#TODO canBlend
    model_info.can_blend = false; // @todo




#pragma region MapType
    auto mapType = getPropertyGeo("map");
    if (!mapType)
        model_info.map_type = MapType::Hide; //#TODO do we even need to set this? It's already default Initialized
    else {
        
        //try lookup map
        auto found = std::find_if(MapTypeToString.begin(), MapTypeToString.end(), [&searchName = *mapType](const auto& it) {
            return iequals(it.second, searchName);
        });
        if (found == MapTypeToString.end()) {
            //#TODO need path
            logger.warning(""sv, -1, "Unknown map type: \"%s\". Falling back to \"Hide\".\n", mapType->c_str());
        } else
            model_info.map_type = found->first;
    }
#pragma endregion MapType


    
#pragma region ViewDensityCoef
    float viewDensityCoef = 1.f;
    auto viewDensCoef = getPropertyGeo("viewdensitycoef");
    if (viewDensCoef)
        viewDensityCoef = stof(*viewDensCoef);
#pragma endregion ViewDensityCoef

#pragma region LODDensityCoef
    auto LODDensCoef = getPropertyGeo("viewdensitycoef");
    if (LODDensCoef)
        model_info.lod_density_coef = stof(*LODDensCoef);
#pragma endregion LODDensityCoef


#pragma region DrawImportance
    auto DrawImp = getPropertyGeo("drawimportance");
    if (DrawImp)
        model_info.draw_importance = stof(*DrawImp);
#pragma endregion DrawImportance

#pragma region Autocenter
    model_info.autocenter = true;
    auto autoCenterProp = getPropertyGeo("autocenter");
    if (autoCenterProp && stoi(*autoCenterProp) == 0) model_info.autocenter = false;
#pragma endregion Autocenter


#pragma region AICover
    auto aiCoverProp = getPropertyGeo("aicovers");
    if (aiCoverProp && stoi(*aiCoverProp) == 0) model_info.ai_cover = true;
#pragma endregion AICover


  
        //#TODO store bounding box of each lod level, and then just grab it here? Seems wasteful though
        //Actually, no it doesn't seem wasteful. getBoundingBox iterates all levels anyway


#pragma region BoundingBoxTotal
    getBoundingBox(model_info.bbox_min, model_info.bbox_max, false, false);
#pragma endregion BoundingBoxTotal


#pragma region BoundingBoxVisual
    //#TODO if all lods are graphical, that means total == Visual and we can skip calc


    //Iterate through all numberGraphicalLods and find the max

    // Visual bounding box
    getBoundingBox(model_info.bbox_visual_min, model_info.bbox_visual_max, true, false);
#pragma endregion BoundingBoxVisual


#pragma region BoundingSphere
    //if (!model_info.lock_autocenter) .... useless as this is always false

    model_info.bounding_center = empty_vector;


    vector3 boundingCenterChange;
    if (model_info.autocenter && num_lods > 0) {
        boundingCenterChange = (model_info.bbox_min + model_info.bbox_max) * 0.5f;


        //#TODO check clip and onsurface flags of lod0


    } else {
        //boundingCenterChange = -model_info.bounding_center; useless. It's already 0
    }
    if (boundingCenterChange.magnitude_squared() < 1e-10 && model_info.bounding_sphere>0) {
        model_info.bounding_sphere += boundingCenterChange.magnitude();
    }


    //Adjust existing positions for changed center

    model_info.bounding_center += boundingCenterChange;


    model_info.bbox_min -= boundingCenterChange;
    model_info.bbox_max -= boundingCenterChange;


    model_info.bbox_visual_min -= boundingCenterChange;
    model_info.bbox_visual_max -= boundingCenterChange;

    model_info.aiming_center -= boundingCenterChange;



    //#TODO apply bounding center change to all lods


    model_info.bounding_sphere += boundingCenterChange.magnitude(); //#TODO Arma does this again, but we already did that above? is this correct?


    //#TODO apply bounding center change to tree crown

    //#TODO if change is big enough, recalculate normals



#pragma endregion BoundingSphere


    float sphere;

    //#TODO fix this up. Seems like boundingSphere should == boundingCenterChange.magnitude?
    // Spheres
    model_info.bounding_sphere = 0.0f;
    model_info.geo_lod_sphere = 0.0f;
    for (uint32_t i = 0; i < num_lods; i++) {
        if (model_info.autocenter)
            sphere = mlod_lods[i].getBoundingSphere(model_info.centre_of_mass);
        else
            sphere = mlod_lods[i].getBoundingSphere(empty_vector);

        if (sphere > model_info.bounding_sphere)
            model_info.bounding_sphere = sphere;
        if (mlod_lods[i].resolution == LOD_GEOMETRY)
            model_info.geo_lod_sphere = sphere;
    }







    //#TODO CalculateHints See map_icon_color below


    //This is calculate hints. Should move into func
#pragma region Calc Hints
    model_info.andHints = 0b11111111'11111111'11111111'11111111;
    model_info.orHints = 0;

    for (auto& it : mlod_lods) {
        model_info.andHints &= it.getAndHints();
        model_info.orHints |= it.getOrHints();
    }
#pragma endregion Calc Hints


#pragma region Color
    //calculate color

    //mlod_lods[0].color; //#TODO
    //mlod_lods[0].colorTop; //#TODO
    //shape.cpp 6490

    //Inside calculate hints
    //#TODO packed color type
    model_info.map_icon_color = 0xff9d8254;
    model_info.map_selected_color = 0xff9d8254;
#pragma endregion Color

    //In CalculateViewDensity using the coef
#pragma region ViewDensity
    int colorAlpha = (model_info.map_icon_color >> 24) & 0xff;
    float alpha = colorAlpha * (1.0 / 255); //#TODO color getAsFloat func to packed color type

    float transp = 1 - alpha * 1.5;
    if (transp >= 0.99)
        model_info.view_density = 0;
    else if (transp > 0.01)
        model_info.view_density = log(transp) * 4 * viewDensityCoef;
    else
        model_info.view_density = -100.0f;
#pragma endregion ViewDensity


    // Centre of mass, inverse inertia, mass and inverse mass
    //#TODO only if massArray is there. Also have to build mass array
    get_mass_data();

#pragma region AnimatedFlag
    if (model_info.orHints&(ClipLandMask | ClipDecalMask))
    {
        if (!(model_info.orHints&ClipLandMask)) {
            model_info.animated = true;
        }
            if (
                (model_info.orHints&ClipLandMask) == ClipLandKeep &&
                (model_info.andHints&ClipLandMask) == ClipLandKeep
                ) {
                //#TODO better structure this
            } else if ((model_info.orHints&ClipLandMask) == ClipLandOn) {
                if ((model_info.andHints&ClipLandMask) != ClipLandOn) {
                    //#TODO warn
                    //"Not all levels have On Surface set"
                }
                model_info.animated = true;
            } else if ((model_info.andHints&ClipLandMask) != ClipLandOn) {
                model_info.animated = true;
                //#TODO warn "SW animation used for %s - Not all levels have Keep Height set"
            }
    }
    if ((model_info.orHints&ClipLightMask) == (model_info.andHints&ClipLightMask)) {
        if ((model_info.andHints & ClipLightMask)&ClipLightLine) {
            model_info.animated = true;
        }
    }

    if (!model_info.animated) {
        auto animProp = getPropertyGeo("animated");
        model_info.animated = (animProp && stoi(*animProp));
    }

#pragma endregion AnimatedFlag

#pragma region ForceNotAlpha
    auto forceNAProp = getPropertyGeo("forcenotalpha");
    if (forceNAProp)
        model_info.force_not_alpha = stoi(*forceNAProp) != 0;
#pragma endregion ForceNotAlpha


#pragma region SBSource
    //Sb source shape.cpp 8660
    model_info.sb_source = SBSource::Visual; //@todo


    //Store if we have a shadowvolume in a var "shapeSV"
    //get projShadow flag


    auto sbSourceProp = getPropertyGeo("sbsource");


    if (!sbSourceProp) {
       //autodetect

        auto shadowProp = getPropertyGeo("shadow");

        if (shadowProp && *shadowProp == "hybrid") {
            //model_info.sb_source = _projShadow ? SBSource::Visual : SBSource::None;
            model_info.prefer_shadow_volume = false;
        } else if (false) {
            //#TODO see *sbSourceProp == "shadowvolume" lower down thing that needs shapeSV. This seems to be same
        } else {
            //model_info.sb_source = _projShadow ? SBSource::Visual : SBSource::None;
        }
       




    } else if (*sbSourceProp == "visual") {
        model_info.sb_source = SBSource::Visual;
        //#TODO
        //if (_shadowBufferCount > 0) {
        //    //warning
        //    //("Warning: %s: Shadow buffer levels are present, but visual LODs for SB rendering are required (in sbsource property)", modelname);
        //}
        //if (!_projShadow) {
        //    //error
        //    //("Error: %s: Shadows cannot be drawn for this model, but 'visual' source is specified (in sbsource property)", modelname);
        //    model_info.sb_source = SBSource::None;
        //}

    } else if (*sbSourceProp == "explicit") {
        //if (_shadowBufferCount <= 0) {
        //    //error
        //    //("Error: %s: Explicit shadow buffer levels are required (in sbsource property), but no one is present - forcing 'none'", modelname);
        //    model_info.sb_source = SBSource::None;
        //} else {
        //    model_info.sb_source = SBSource::Explicit;
        //}
    } else if (*sbSourceProp == "shadowvolume") {//#TODO should this be lowercase?
        //if (shapeSV.Size() == 0 || shapeSV.Size() != shapeSVResol.Size()) {
            //error
            //("Error: %s: Shadow volume lod is required for shadow buffer (in sbsource property), but no shadow volume lod is present", modelname);
            model_info.sb_source = SBSource::None;
        //} else {
            
            //#TODO shape.cpp 8697 needs shapeSV







        //}



    } else if (*sbSourceProp == "none") {
        model_info.sb_source = SBSource::None;
    } else {
        //#TODO throw error
    }

#pragma endregion SBSource



#pragma region PreferShadowVolume
    auto preferShadPRop = getPropertyGeo("prefershadowvolume");
    if (preferShadPRop)
        model_info.prefer_shadow_volume = stoi(*preferShadPRop) != 0;
#pragma endregion PreferShadowVolume


#pragma region ShadowOffset
    model_info.shadow_offset = std::numeric_limits<float>::max(); //@todo

    if (model_info.special_lod_indices.geometry_simple.isDefined()) {
        auto shadOffsProp = getPropertyGeo("shadowoffset");
        if (shadOffsProp) {
            model_info.shadow_offset = stof(*shadOffsProp);
        }
    }

    if (model_info.shadow_offset >= std::numeric_limits<float>::max()) {
        float size = model_info.bbox_min.distance(model_info.bbox_max);


        if (size < 2.5f)
            model_info.shadow_offset = 0.02f;
        else if (size > 10.0f)
            model_info.shadow_offset = 0.10f;
        else
            model_info.shadow_offset = (size - 2.5f)*(1.0f / 7.5f)*0.08f + 0.02f;
    }
#pragma endregion ShadowOffset

#pragma region PreferredShadowLODs
    {
        auto& preferredShadowVolumeLod = model_info.preferredShadowVolumeLod;
        auto& preferredShadowBufferLod = model_info.preferredShadowBufferLod;
        auto& preferredShadowBufferVisibleLod = model_info.preferredShadowBufferVisibleLod;

        for (auto& it : mlod_lods) {
            auto SLprop = it.getProperty("shadowlod");
            uint32_t SL = SLprop ? stoi(*SLprop) : 0xFFFFFFFF;
            auto SVLprop = it.getProperty("shadowvolumelod");
            uint32_t SVL = SVLprop ? stoi(*SVLprop) : 0xFFFFFFFF;
            auto SBLprop = it.getProperty("shadowbufferlod");
            uint32_t  SBL = SBLprop ? stoi(*SBLprop) : 0xFFFFFFFF;
            auto SBLVprop = it.getProperty("shadowbufferlodvis"); //#TODO rename preferredShadowBufferVisibleLod swap vis and lod
            uint32_t SBLV = SBLVprop ? stoi(*SBLVprop) : 0xFFFFFFFF;

            if (SBL == 0xFFFFFFFF) {
                SBL = SL;
                if (SBL == 0xFFFFFFFF)
                    SBL = SVL;
            }
            if (SVL == 0xFFFFFFFF) {
                SVL = SL;
                if (SVL == 0xFFFFFFFF)
                    SVL = SBL;
            }

            preferredShadowVolumeLod.emplace_back(SVL);
            preferredShadowBufferLod.emplace_back(SBL);
            preferredShadowBufferVisibleLod.emplace_back(SBLV);
        }
    }
#pragma endregion PreferredShadowLODs


    optimizeLODS();
    //#TODO OptimizeShapes

    //set special flags thing

#pragma region Special Flags
    for (auto& it : mlod_lods) {
        model_info.index |= it.getSpecialFlags();
    }
#pragma endregion Special Flags

    //CalculateMinMax again
    //CalculateMass again

    //Read modelconfig and grab properties
    //checkForcedProperties


#pragma region CanOcclude
    {
        int complexity = mlod_lods[0].num_faces;
        float size = model_info.bounding_sphere;
        uint32_t viewComplexity = 0;

        if (model_info.special_lod_indices.geometry_view)
            viewComplexity = mlod_lods[model_info.special_lod_indices.geometry_view].num_faces;

        model_info.can_occlude = false;
        // _viewDensity was calculated few lines above - CalculateHints
        if (viewComplexity > 0 && model_info.view_density <= -9) {
            if (size > 5)  model_info.can_occlude = true;
            if (size > 2 && viewComplexity <= 6)  model_info.can_occlude = true;
        }
        // large or complex objects may be occluded
        if (complexity >= 6 || size > 5) model_info.can_be_occluded = true;
        // allow override with property

        auto canOccludeProp = getPropertyGeo("canocclude");
        if (canOccludeProp)  model_info.can_occlude = stoi(*canOccludeProp) != 0;
        auto canBeOccludeProp = getPropertyGeo("canbeoccluded");
        if (canBeOccludeProp) model_info.can_be_occluded = stoi(*canBeOccludeProp) != 0;
    }
#pragma endregion CanOcclude



#pragma region initConvexComp




    auto geoLodID = model_info.special_lod_indices.geometry;


    if (geoLodID != -1) {
        auto& geoLod = mlod_lods[geoLodID];
        model_info.geometry_center = (geoLod.max_pos + geoLod.min_pos)*0.5;
        model_info.geo_lod_sphere = geoLod.max_pos.distance(geoLod.min_pos)*0.5;
        model_info.aiming_center = model_info.geometry_center;
    }

    auto memLodID = model_info.special_lod_indices.memory;

    if (memLodID != -1) {
        auto& memLod = mlod_lods[memLodID];


        //#TODO make getMemoryPoint function
        auto found = std::find_if(memLod.selections.begin(), memLod.selections.end(), [](const mlod_selection& it) {
            return it.name == "zamerny";
        });
        vector3 aimMemPoint;
        bool memPointExists = false;
        if (found != memLod.selections.end()) {
            if (found->points.empty()) {
                //#TODO warning "No point in selection %s"
            } else {
                memPointExists = found->points[0] >= 0;

                aimMemPoint = memLod.points[found->points[0]].getPosition();
                model_info.aiming_center = aimMemPoint;
            }
        }



    }
    //#TODO calculateNormals stuff


#pragma endregion initConvexComp


    //InitConvexComponents


#pragma region Armor
    auto armorProp = getPropertyGeo("armor");
    if (armorProp) model_info.armor = stof(*armorProp);
    else
        model_info.armor = 200.0f;

    if (model_info.armor > 1e-10) {
        model_info.inv_armor = 1 / model_info.armor;
    } else {
        model_info.inv_armor = 1e10;
    }
#pragma endregion Armor

    //CalculateHints again!!


#pragma region ClassDamageFreqProps

    auto classProp = getPropertyGeo("class");
    if (classProp) {
        std::transform(classProp->begin(), classProp->end(), classProp->begin(), ::tolower);
        model_info.class_type = std::move(*classProp);
    }


    auto damageProp = getPropertyGeo("damage");

    if (damageProp) {
        model_info.destruct_type = std::move(*damageProp);
    } else {
        auto dammageProp = getPropertyGeo("dammage"); //DUH
        model_info.destruct_type = std::move(*dammageProp);
        //#TODO print warning about wrong name

    }

    auto frequentProp = getPropertyGeo("frequent");
    if (frequentProp) model_info.property_frequent = stoi(*frequentProp) != 0;

#pragma endregion ClassDamageFreqProps


    //#TODO load skeleton
    //#TODO load animations

    model_info.skeleton = std::make_unique<skeleton_>();


    //This seems to be old thingy? new uses buoyancy property below
    //bool canFloat = true;
    //if (model_info.numberGraphicalLods <= 0 ||
    //    !model_info.special_lod_indices.geometry
    //    || model_info.class_type == "building"
    //    || model_info.class_type == "bushhard"
    //    || model_info.class_type == "bushsoft"
    //    || model_info.class_type == "church"
    //    || model_info.class_type == "forest"
    //    || model_info.class_type == "house"
    //    || model_info.class_type == "man"
    //    || model_info.class_type == "road"
    //    || model_info.class_type == "streetlamp"
    //    || model_info.class_type == "treehard"
    //    || model_info.class_type == "treesoft"
    //    || model_info.class_type == "clutter"
    //    || model_info.class_type == "none"
    //    || !model_info.autocenter
    //    ) {
    //    canFloat = false;
    //}
    //if (canFloat) {
    //    if (model_info.special_lod_indices.geometry_simple) {
    //        auto nBue = std::make_unique<BuoyantIteration>();
    //        nBue->init(*this);
    //        buoy = std::move(nBue);
    //    } else {
    //        auto nBue = std::make_unique<BuoyantSphere>();
    //        nBue->init(*this);
    //        buoy = std::move(nBue);
    //    }
    //}



    if (model_info.numberGraphicalLods > 0 && model_info.autocenter) {
        auto buoyProp = getPropertyGeo("buoyancy");
        if (stoi(*buoyProp) != 0) {

            //set up buoyancy
            if (model_info.special_lod_indices.geometry_simple.isDefined()) {
                auto nBue = std::make_unique<BuoyantIteration>();
                nBue->init(*this);
                buoy = std::move(nBue);
            } else {
                auto nBue = std::make_unique<BuoyantSphere>();
                nBue->init(*this);
                buoy = std::move(nBue);
            }
        }
    }







    //THE END!

    model_info.n_floats = 0;


    // the real indices to the lods are calculated by the engine when the model is loaded




    BuildSpecialLodList();



    // according to BIS: first LOD that can be used for shadowing
    model_info.min_shadow = num_lods; // @todo
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
        if (strncmp(buffer, "P3DM", 4) != 0) //#TODO move into lod load
            return 0;

        mlod_lod newLod;

        if (!newLod.read(source, logger)) return 0;

        //#TODO ResolGeometryOnly on resolution
        //We might want to remove normals (set them all to 0,1,0)
        //or UV's (set them all to 0 in the faces)





        //If lod is shadow volume, copy it into an array with shadow volume lods
        //Also keep an array of sahdow volume resolitoons "shapeSVResol"

        //AddShape? Yeah. The ScanShapes thing inside there.
        mlod_lods.emplace_back(std::move(newLod));

        BuildSpecialLodList();
        scanProjectedShadow();


        //Customize Shape (shape.cpp 8468)
            //detect empty lods if non-first and res>900.f
            //Delete back faces?
            //Build sections based on model.cfg
            //scan for proxies
            //If shadow volume do extra stuff shape.cpp L7928
            // If shadow buffer, set everything (apart of base texture if not opaque) to predefined values
            //Create subskeleton
            //rescan sections
            //update material if shadowVOlume
            //GenerateSTArray?
            //Warning: %s:%s: 2nd UV set needed, but not defined
            // Clear the point and vertex conversion arrays
            // scan selections for proxy objects
            //optimize if no selections?
            //if (geometryOnly&GONoUV)

        //Tree crown needed
        //int ShapePropertiesNeeded(const TexMaterial *mat) ? set _shapePropertiesNeeded based on surface material

        //Blending required



        
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

std::vector<std::string> P3DFile::retrieveDependencies(std::filesystem::path sourceFile) {
    //Copy of readMLOD without model info

    std::ifstream input(sourceFile, std::ifstream::binary);


    if (!input.is_open()) {
        logger.error(sourceFile.string(), 0, "Failed to open source file.\n");
        return {};
    }

    current_target = sourceFile.string();

    char typeBuffer[5];
    input.read(typeBuffer, 5);

    if (strncmp(typeBuffer, "MLOD", 4) != 0) {
        if (strcmp(args.positionals[0], "binarize") == 0)
            logger.error(sourceFile.string(), 0, "Source file is not MLOD.\n");
        return {};
    }

    input.seekg(8);

    input.read(reinterpret_cast<char*>(&num_lods), 4);

    num_lods = read_lods(input, num_lods);
    if (num_lods <= 0) {
        logger.error(sourceFile.string(), 0, "Failed to read LODs.\n");
        return {};
    }
    //^^^^^^ Copy of readMLOD without model info

    std::unordered_set<std::string> dependencies; //we don't want duplicates, and doing it with a set is much faster


    for (const auto& it : mlod_lods) {
        for (auto& tex : it.textures)
            if (tex.front != '#')
                dependencies.insert(tex);
        for (auto& mat : it.materials)
            if (mat.path.front() != '#')
                dependencies.insert(mat.path);
    }

    std::vector<std::string> depVec;
    depVec.resize(dependencies.size());

    std::move(dependencies.begin(), dependencies.end(), std::back_inserter(depVec));
    return depVec;
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
    //#TODO ^ we could pre-calculate that?

    output.write(reinterpret_cast<char*>(&num_lods), 4);

    if (model_info.lod_resolutions.size() != num_lods)
        __debugbreak();

    model_info.writeTo(output);

    // Write animations
    if (model_info.skeleton->num_animations > 0) {
        output.put(1); //bool hasAnims
        write_animations(output);
    } else {
        output.put(0); //bool hasAnims
    }

    // Write place holder LOD addresses
    size_t fp_lods_starts = output.tellp();


    std::vector<StreamFixup<uint32_t>> startOffsets;
    std::vector<StreamFixup<uint32_t>> endOffsets;


    //lod start addresses
    for (uint32_t i = 0; i < num_lods; i++)
        startOffsets.emplace_back(output);
    //lod end addresses
    size_t fp_lods_ends = output.tellp();
    for (uint32_t i = 0; i < num_lods; i++)
        endOffsets.emplace_back(output);

    //^ that there is structured like the following
    //lod start
    //lod start
    //lod start
    //...
    //lod end
    //lod end
    //lod end



    //#TODO set properly LODShape::IsPermanent ||
    // i == _nGraphical - 1 && (_nGraphical > 1 || _lods[i]->NFaces() < 100)

    // Write LOD face defaults (or rather, don't)
    for (uint32_t i = 0; i < num_lods; i++)
        output.put(1); //bool array, telling engine if it should load the LOD right away, or rather stream it in later

    //#TODO iterate once for non-permantent to save
    //32bit number of faces
    //32bit color
    //32bit special
    //32bit or hints
    //char subskeletonSize>0
    //32bit number of verticies
    //float total area of faces


    // Write LODs
    for (uint32_t i = 0; i < num_lods; i++) {


        // Write start address
        startOffsets[i].setValue(output.tellp());

        // Convert to ODOL
        odol_lod odol_lod{};
        convert_lod(mlod_lods[i], odol_lod);

        // Write to file
        odol_lod.writeTo(output);

        // Write end address
        endOffsets[i].setValue(output.tellp());
    }

    for (auto& it : startOffsets)
        it.write(output);
    for (auto& it : endOffsets)
        it.write(output);

    output.seekp(0, std::ostream::end);


    //#TODO write buoyancy
    //32bit isused
    //00 none.
    //1 iteration
    //2 segmentation

    //1 //if _geometrySimple=>0
    //float fullVolume
    //float basicLeakiness
    //float resistanceCoef
    //float linDampCoefX
    //float linDampCoefY
    //float angDampCoef
    //Vec3 shapeMin
    //Vec3 shapeMax

    //2
    //32bit arraySizex
    //32bit arraySizey
    //32bit arraySizez
    //float stepX
    //float stepY
    //float stepZ
    //float fullSphereRadius
    //32bit minSpheres
    //32bit maxSpheres

    //size = arraySizeX*Y*Z

    //for each element 0 to size
    //Vec3 mCoord
    //float sphereRadius
    //float typicalSurface

    //Serialize 1 here


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

