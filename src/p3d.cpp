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

#include <algorithm>
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
#include "paaconverter.h"


template <typename Type>
class StreamFixup {
    std::streamoff offset;
    Type value;
public:
    StreamFixup(std::ostream& str) : offset(str.tellp()) { str.write(reinterpret_cast<const char*>(&value), sizeof(Type)); }
    void setValue(const Type& val) { value = val; }
    void write(std::ostream& str) { str.seekp(offset); str.write(reinterpret_cast<const char*>(&value), sizeof(Type)); }
};

//#TODO move this somewhere sensible
bool resIsShadowVolume(ComparableFloat<std::milli> resolution) {

    return ((resolution > LOD_SHADOW_STENCIL_START && resolution < LOD_SHADOW_STENCIL_END) ||
        resolution == LOD_SHADOW_VOLUME_VIEW_CARGO ||
        resolution == LOD_SHADOW_VOLUME_VIEW_PILOT ||
        resolution == LOD_SHADOW_VOLUME_VIEW_GUNNER
        );

}


bool is_alpha(std::string textureName) {
    static thread_local std::map<std::string, PAAFile> texCache; //#TODO global threadsafe texture cache thing

    if (texCache.find(textureName) != texCache.end()) {
        auto& texture = texCache[textureName];
        return texture.isAlpha || texture.isTransparent;
    } else {
        PAAFile paf;

        auto found = find_file(textureName, ""); //#TODO proper origin
        if (!found) {
            //#TODO warning

            return false;
        }
        std::ifstream texIn(*found, std::ifstream::binary);
        paf.readHeaders(texIn);

        //#TODO handle jpg textures.. maybe?

        texCache[textureName] = paf;
        return paf.isAlpha || paf.isTransparent;
    }
}



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
        auto& point = p;
        return (point - center).magnitude_squared();
    });
    __itt_task_end(p3dDomain);

    __itt_task_begin(p3dDomain, __itt_null, __itt_null, handle_bs2);

    for (auto i = 0u; i < num_points; i++) {
        auto dist = (points[i] - center).magnitude_squared();
        if (dist > sphere)
            sphere = dist;
    }
    __itt_task_end(p3dDomain);

    return sqrt(sphere);
}

void mlod_lod::updateColors() {
    std::map<std::string, PAAFile> texCache;

    float areaSum=0;
    float areaTSum=0;
    float alphaSum=0;
    float alphaTSum=0;

    ColorFloat colorSum;
    ColorFloat colorTSum;



    for (int i = 0; i < num_faces; ++i) {
        auto& face = faces[i];
        auto& faceProp = faceInfo[i];

        std::optional<PAAFile> texture;
        if (faceProp.textureIndex != -1) {
            auto& path = textures[faceProp.textureIndex];

            if (texCache.find(path) != texCache.end()) {
                texture = texCache[path];
            } else {

                PAAFile paf;

                auto found = find_file(path, ""); //#TODO proper origin
                if (!found) {
                    //#TODO warning
                }
                std::ifstream texIn(*found, std::ifstream::binary);
                paf.readHeaders(texIn);

                //#TODO handle jpg textures.. maybe?

                texCache[path] = paf;
                texture = paf;
            }
        }

        ColorFloat color = texture ? texture->getTotalColor() : ColorFloat(1, 1, 1);
        auto materialColor = ColorFloat(1, 1, 1);
        if (faceProp.materialIndex != -1) {
            auto& mat = materials[faceProp.materialIndex];

            materialColor =
                (mat.diffuse + mat.forced_diffuse)*0.25 + mat.ambient * (0.75f)
                +
                mat.emissive;
            materialColor.a = 1.f;
        }

        auto ar = face.getArea(points);
        auto arT = face.getAreaTop(points);

        if (texture && texture->isAlpha) {
            ar *= color.a;
            arT *= color.a;
            color.a = 1.f;
        }
        areaSum += ar;
        areaTSum += arT;

        alphaSum += color.a*ar;
        alphaTSum += color.a*arT;

        colorSum += color * materialColor*color.a*ar;
        colorTSum += color * materialColor*color.a*arT;
    }

    if (alphaSum > 0.f) colorSum = colorSum * (1 / alphaSum);
    if (areaSum > 0.f) colorSum.a = (alphaSum/ areaSum);

    if (alphaTSum > 0.f) colorTSum = colorTSum * (1 / alphaTSum);
    if (areaTSum > 0.f) colorTSum.a = (alphaTSum / areaTSum);

    icon_color = colorSum;
    selected_color = colorTSum;
}

bool mlod_lod::read(std::istream& source, Logger& logger, std::vector<float> &mass) {

    //4 byte int header size
    //4 byte int version
    source.seekg(8, std::istream::cur);

    uint32_t numPointsTemp, numNormalsTemp;

    source.read(reinterpret_cast<char*>(&numPointsTemp), 4);
    source.read(reinterpret_cast<char*>(&numNormalsTemp), 4);
    source.read(reinterpret_cast<char*>(&num_faces), 4);
    //4 byte int flags
    source.seekg(4, std::istream::cur);


    std::vector<bool> hiddenPoints;
    hiddenPoints.resize(numPointsTemp);
    std::vector<point> tempPoints;

    bool empty = numPointsTemp == 0;
#pragma region Basic Geometry
    if (empty) {
        numPointsTemp = 1;
        tempPoints.resize(1);
        tempPoints[0].point_flags = 0;
    } else {
        tempPoints.resize(numPointsTemp);
        for (int j = 0; j < numPointsTemp; j++) {
            source.read(reinterpret_cast<char*>(&tempPoints[j]), sizeof(struct point));
            //report invalid point flags? not really needed.

            //#TODO hidden points support
            //If any flags are set, set hints and use them for `clip` array
            //There is also "hidden" array for POINT_SPECIAL_HIDDEN

            //#TODO read points into array of vec3. And throw away pointflags
            if (tempPoints[j].point_flags & 0x1000000) { //#TODO enum or #define or smth
                hiddenPoints[j] = true;
            }
            //#TODO clip flags
        }
    }


    std::vector<vector3> normalsTemp;
    normalsTemp.resize(numNormalsTemp); //#TODO remove num_facenormals from header
    //#TODO read all in one go
    for (int j = 0; j < numNormalsTemp; j++) {
        source.read(reinterpret_cast<char*>(&normalsTemp[j]), sizeof(vector3)); //#TODO if Geometry lod and NoNormals (geoOnly parameter), set them all to 0
    }

#pragma region FacesAndTexturesAndMaterials


    std::vector<int> faceToOrig;



    std::vector<mlod_face> loadingFaces;

    loadingFaces.resize(num_faces);
    for (int j = 0; j < num_faces; j++) {
        //4b face type
        //4x pseudovertex
        source.read(reinterpret_cast<char*>(&loadingFaces[j]), 72);

        size_t fp_tmp = source.tellg();
        std::string textureName;
        std::string materialName;


        std::getline(source, textureName, '\0');

        std::getline(source, materialName, '\0');
        //#TODO tolower texture and material
        //#TODO check for valid flags
        loadingFaces[j].section_names = ""; //#TODO section_names shouldn't be here

        //#TODO make seperate array with faceproperties (material,texture index and flags


        //#REFACTOR make a custom array type for this. Give it a "AddUniqueElement" function that get's the string.
        //It will return index if it has one, or create one and then return index of new one
        if (!textureName.empty()) {
            auto foundTex = std::find(textures.begin(), textures.end(), textureName);
         
            if (foundTex == textures.end()) {
                loadingFaces[j].texture_index = faces.size();
                textures.emplace_back(textureName);
            } else {
                loadingFaces[j].texture_index = foundTex - textures.begin();
            }
        }
        
        if (!materialName.empty()) {
            auto foundMat = std::find_if(materials.begin(), materials.end(), [&searchName = materialName](const Material& mat) {
                return searchName == mat.path;
            });

            __debugbreak(); //Check if this stuff is correct.
            if (foundMat == materials.end()) {
                loadingFaces[j].material_index = materials.size();



                Material mat(logger, materialName);

                //#TODO take current_target as parameter from p3d
                mat.read();//#TODO exceptions Though we don't seem to check for errors there? Maybe we should?
                //YES! We should! mat.read will fail if material file isn't found.
                materials.emplace_back(std::move(mat)); //#CHECK new element should be at index j now.

            } else {
                loadingFaces[j].material_index = foundMat - materials.begin();
            }
        }

        //check and fix UV
        //shape.cpp 1075
        //Set UV coords to 0 if Geo lod with nouv
    }


#pragma endregion FacesAndTexturesAndMaterials

    minUV = uv_pair{ std::numeric_limits<float>::max(),std::numeric_limits<float>::max() };
    maxUV = uv_pair{std::numeric_limits<float>::min(),std::numeric_limits<float>::min() };

    for (auto& it : loadingFaces) {
        for (int i = 0; i < it.face_type; ++i) {
            auto& u = it.table[i].u;
            auto& v = it.table[i].v;

            maxUV.u = std::max(maxUV.u, u);
            maxUV.v = std::max(maxUV.v, v);
            minUV.u = std::min(minUV.u, u);
            minUV.v = std::min(minUV.v, v);
        }
    }
    uv_pair inverseScalingUV{
        1/maxUV.u-minUV.u,
        1/maxUV.v-minUV.v
        };








    //2nd pass over faces, find min and maxUV
    //^ don't need that



    //#TODO turn faces into poly's here (odol_face) and only store them
    //3rd pass over faces
    //collect unique texture paths
    //collect all faces in a poly and fill vertexToPoint and pointToVertex tables
    //the poly object is done in odol, see Odol's "AddPoint"
    //Error if max vertex that are not duplicate (see odol Addpoint) is bigger than 32767 too many verticies
    //Poly object seems to be just the face with n points?
    vertex_to_point.reserve(tempPoints.size());
    point_to_vertex.resize(tempPoints.size());
    faceToOrig.resize(loadingFaces.size());
    int allFaceSpecFlagsAnd = ~0;
    for (int i = 0; i < loadingFaces.size(); ++i) {
        auto& face = loadingFaces[i];






        odol_face newPoly;
        newPoly.face_type = face.face_type;

        PolygonInfo newPolyInfo;
        newPolyInfo.textureIndex = face.texture_index;
        newPolyInfo.materialIndex = face.material_index;



        bool polyIsHidden = true;


        //clip flags


        // Change vertex order for ODOL
        // Tris:  0 1 2   -> 1 0 2
        // Quads: 0 1 2 3 -> 1 0 3 2
        //#TODO do this after points were inserted, then we only have to swap one int, instead of 2+2 floats
        std::swap(face.table[0], face.table[1]);

        if (face.face_type == 4)
            std::swap(face.table[2], face.table[3]);


        for (int i = 0; i < face.face_type; ++i) {
            auto faceP = face.table[i].points_index;
            auto faceN = face.table[i].normals_index;
            auto faceU = face.table[i].u;
            auto faceV = face.table[i].v;

            if (!hiddenPoints[faceP]) polyIsHidden = false;

            auto norm = normalsTemp[faceN];

            //#TODO check if normal is valid and fix it L1241



            //#TODO uv coord compression!!!
            auto newVert = add_point(tempPoints[faceP].pos, norm, { faceU,faceV }, faceP, inverseScalingUV);

            newPoly.points[i] = newVert;

            if (newVert+1 > vertex_to_point.size())
                vertex_to_point.resize(vertex_to_point.size()+1);
            vertex_to_point[newVert] = faceP;
            point_to_vertex[faceP] = newVert;
        }
        

        //#TODO polyIsHidden






        int specialFlags = 0;


        if (face.face_flags & 123123123) {//#TODO fix shape.cpp L1174

            if (face.face_flags & 0x8) specialFlags |= 64; //Is shadow
            if (face.face_flags & 0x10) specialFlags |= 32; //no shadow

            //#TODO zbias 

        }

        //#TODO apply material flags Line 1345
        //#TODO apply texture flags //line 1352

        //#TODO only if face has material!
        bool transmat = false;
        if (face.material_index != -1) //#TODO nullable
            switch (materials[face.material_index].pixelshader_id) {
                case 1: //#TODO move into material class and use enum
                case 3:
                case 60:
                case 58:
                case 61:
                case 107:
                case 108:
                case 109:
                case 114:
                case 115:
                case 116:
                case 117:
                case 120:
                case 148:
                case 132:
                    transmat = true;
            }
        if (transmat) {
            specialFlags |= FLAG_ISTRANSPARENT;
        } else if (face.texture_index != -1) {

            
            if (is_alpha(textures[face.texture_index])) {
                __debugbreak();
                specialFlags |= FLAG_ISALPHA;
            }
                
        }





        if (polyIsHidden) {
            specialFlags |= 0x400000;
        }
         //#TODO if not loading UV (geometry lod) set no clamp flags L1390
        newPolyInfo.specialFlags = specialFlags;
        special |= specialFlags;
        allFaceSpecFlagsAnd &= specialFlags;
        //#TODO add poly to faces list
        faces.emplace_back(newPoly);
        faceInfo.emplace_back(newPolyInfo);
        faceToOrig[i] = i;
    }

#pragma endregion Basic Geometry


    //Unroll multipass materials? Don't think we need that






    // Sort faces
    //If we do this, we need lookup map from oldindex->newIndex. Which is what face_lookup did in old armake
    //if (mlod_lod.num_faces > 1) {
    //    std::sort(mlod_lod.faces.begin(), mlod_lod.faces.end());
    //}


    // Write remaining vertices
    for (int i = 0; i < tempPoints.size(); ++i) {
        //#TODO prefill point_to_vertex with ~0's and then check that here
        if (point_to_vertex[i] < NOPOINT) continue; //#TODO wtf? no. Doesn't work

        auto newVert = add_point(tempPoints[i].pos, {}, { 0.f,0.f }, i, inverseScalingUV);

        point_to_vertex[i] = newVert;
        if (newVert > vertex_to_point.size())
            vertex_to_point.resize(vertex_to_point.size()+1);
        vertex_to_point[newVert] = i;
    }

    //#TODO
    //set special flags
    special &= IsAlpha | IsTransparent | IsAnimated | OnSurface;
    special |= allFaceSpecFlagsAnd & (NoShadow | ZBiasMask);
    //#TODO calc Hints 1487
    //Needs clip flags 864
    


    vertex_to_point.shrink_to_fit(); //#TODO shrink the others too
    point_to_vertex.shrink_to_fit();



#pragma region UVClamp
    // UV clamping 1506
    std::vector<bool> tileU, tileV;
    tileU.resize(textures.size()); 
    tileV.resize(textures.size());

    for (int i = 0; i < num_faces; i++) {
        if (faceInfo[i].textureIndex == -1)
            continue;
        if (tileU[faceInfo[i].textureIndex] && tileV[faceInfo[i].textureIndex])
            continue;

        
        for (int j = 0; j < faces[i].face_type; j++) { //#TODO move clamplimit here into a constexpr float
            if (uv_coords[faces[i].points[j]].u < -CLAMPLIMIT || uv_coords[faces[i].points[j]].u > 1 + CLAMPLIMIT)
                tileU[faceInfo[i].textureIndex] = true;
            if (uv_coords[faces[i].points[j]].v < -CLAMPLIMIT || uv_coords[faces[i].points[j]].v > 1 + CLAMPLIMIT)
                tileV[faceInfo[i].textureIndex] = true;
        }
    }

    for (int i = 0; i < num_faces; i++) {
        if (faceInfo[i].specialFlags & (FLAG_NOCLAMP | FLAG_CLAMPU | FLAG_CLAMPV)) //already clamped
            continue;
        if (faceInfo[i].textureIndex == -1) {
            faceInfo[i].specialFlags |= FLAG_NOCLAMP;
            continue;
        }

        if (!tileU[faceInfo[i].textureIndex])
            faceInfo[i].specialFlags |= FLAG_CLAMPU;
        if (!tileV[faceInfo[i].textureIndex])
            faceInfo[i].specialFlags |= FLAG_CLAMPV;
        if (tileU[faceInfo[i].textureIndex] && tileV[faceInfo[i].textureIndex])
            faceInfo[i].specialFlags |= FLAG_NOCLAMP;
    }
#pragma endregion UVClamp


    //rebuild normals not needed


    //minmax
    updateBoundingBox();
    autocenter_pos = (min_pos + max_pos) * 0.5f;
    boundingSphere = getBoundingSphere(autocenter_pos);

    updateColors();

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
                if (empty) { //#TODO empty var not set correctly. Check tagg_len
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
            //#TODO #Selected#
            if (entry == "#EndOfFile#") //Might want to  source.seekg(fp_tmp); before break. Check if fp_temp == tellg and debugbreak if not
                break;
        } else {
            odol_selection newSel;

            newSel.name = entry;

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

            std::vector<odol_selection::selectionVertex> temp;
            for (int i = 0; i < num_points; ++i) {
                
                //#TODO use vertexToPoint
                auto vertIndex = vertex_to_point[i];

                auto thingy = newSelection.points[vertIndex];
                if (thingy) temp.emplace_back(i, -thingy);
            }

            newSel.init(std::move(temp));

            for (int i = 0; i < num_faces; ++i) {
                if (newSelection.faces[faceToOrig[i]]) newSel.faces.emplace_back(i);
            }

            


            selections.emplace_back(std::move(newSel));
            //#TODO Might want to  source.seekg(fp_tmp); before break. Check if fp_temp == tellg and debugbreak if not
        }
    }
    num_selections = selections.size();
    //if have phases (animation tag)
    //check for keyframe property. Needs to exist otherwise warning

    //#TODO 1860
    //Sort verticies if not geometryOnly




    source.read(reinterpret_cast<char*>(&resolution), 4);
    return true;
}

void mlod_lod::writeODOL(std::ostream& output) {
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
    for (i = 0; i < num_bones_skeleton; i++) {
        WRITE_CASTED(skeleton_to_subskeleton[i].num_links, sizeof(uint32_t));
        output.write(reinterpret_cast<const char*>(skeleton_to_subskeleton[i].links), sizeof(uint32_t) * skeleton_to_subskeleton[i].num_links);
    }

    WRITE_CASTED(num_points, sizeof(uint32_t));
    WRITE_CASTED(faceArea, sizeof(float));
    WRITE_CASTED(orHints, sizeof(uint32_t));
    WRITE_CASTED(andHints, sizeof(uint32_t));


    WRITE_CASTED(min_pos, sizeof(vector3));
    WRITE_CASTED(max_pos, sizeof(vector3));
    WRITE_CASTED(autocenter_pos, sizeof(vector3));
    WRITE_CASTED(boundingSphere, sizeof(float));
    

    uint32_t num_textures = textures.size();
    WRITE_CASTED(num_textures, sizeof(uint32_t));

    std::string texString;
    for (auto& tex : textures) {
        texString += tex;
        texString.push_back('\0');
    }
    if (textures.size() > 1)
        __debugbreak(); //check if the textures string is correct. null char delimited array of texture paths

    output.write(texString.c_str(), texString.length() + 1);

    uint32_t num_materials = materials.size();
    WRITE_CASTED(num_materials, sizeof(uint32_t));
    for (uint32_t i = 0; i < num_materials; i++) //#TODO ranged for
        materials[i].writeTo(output);

    // the point-to-vertex and vertex-to-point arrays are just left out
    output.write("\0\0\0\0\0\0\0\0", sizeof(uint32_t) * 2);

    WRITE_CASTED(num_faces, sizeof(uint32_t));

    uint32_t face_allocation_size = std::transform_reduce(faces.begin(), faces.end(), 0u, std::plus<>(), [](const odol_face& f) {
        return (f.face_type == 4) ? 20 : 16;
        });


    WRITE_CASTED(face_allocation_size, sizeof(uint32_t));
    output.write("\0\0", sizeof(uint16_t)); //always 0

    for (i = 0; i < num_faces; i++) {
        WRITE_CASTED(faces[i].face_type, sizeof(uint8_t));
        output.write(reinterpret_cast<char*>(faces[i].points.data()), sizeof(uint32_t) * faces[i].face_type);
    }

    uint32_t num_sections = sections.size();
    WRITE_CASTED(num_sections, sizeof(uint32_t));
    for (i = 0; i < num_sections; i++) {//#TODO foreach
        sections[i].writeTo(output);
    }

    //#TODO may want a array writer for this. It's always number followed by elements
    WRITE_CASTED(num_selections, sizeof(uint32_t));
    for (i = 0; i < num_selections; i++) {
        selections[i].writeTo(output);
    }

    uint32_t num_properties = properties.size();
    WRITE_CASTED(num_properties, sizeof(uint32_t));
    for (auto& prop : properties) {
        output.write(prop.name.c_str(), prop.name.length() + 1);
        output.write(prop.value.c_str(), prop.value.length() + 1);
    }

    //#TODO animation phases
    uint32_t num_frames = 0;
    WRITE_CASTED(num_frames, sizeof(uint32_t));
    // @todo frames
  

    WRITE_CASTED(icon_color, sizeof(uint32_t));
    WRITE_CASTED(selected_color, sizeof(uint32_t));
    WRITE_CASTED(special, sizeof(uint32_t));
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
    WRITE_CASTED(minUV, sizeof(struct uv_pair));
    WRITE_CASTED(maxUV, sizeof(struct uv_pair));


    std::vector<uv_paircompact> uv_coords;


    uint32_t num_uvs = uv_coords.size();
    WRITE_CASTED(num_uvs, sizeof(uint32_t));
    //#TODO LZO/LZW compression for this
    output.put(0);//is LZ compressed
    if (num_points > 0) {
        output.put(0); //thingy.. Complicated. Only in non compressed thing
        for (i = 0; i < num_uvs; i++) { //#TODO foreach
            // write compressed pair
            WRITE_CASTED(uv_coords[i].u, sizeof(int16_t));
            WRITE_CASTED(uv_coords[i].v, sizeof(int16_t));
        }
    }
    output.write("\x01\0\0\0", 4); //Other UV stages, 32bit int. Here we say we have 1 UV stage, That's the one we've just written.
    //Write the same stuff as above again for a potential second UV stage
    //#TODO ^

    // points
    WRITE_CASTED(num_points, sizeof(uint32_t));
    if (num_points > 0) {
        output.put(0);
        output.write(reinterpret_cast<char*>(points.data()), sizeof(vector3) * num_points);
    }

    // normals
    WRITE_CASTED(num_points, sizeof(uint32_t));
    output.put(0);//is LZ compressed
    if (num_points > 0) {
        output.put(0); //thingy.. Complicated. Only in non compressed thing
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

    //#TODO st coords? in FinalizeLOD
    // ST coordinates
    output.write("\0\0\0\0", 4);

    // vertex bone ref
    if (vertexboneref.empty() || num_points == 0) {
        output.write("\0\0\0\0", 4);
    } else {
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

uint32_t mlod_lod::getAndHints() {
    return andHints;
}

uint32_t mlod_lod::getOrHints() {
    return orHints;
}

uint32_t mlod_lod::getSpecialFlags() {
    return special;
}

void mlod_lod::buildSections() {
    //#TODO shape.cpp 3902

    if (num_faces == 0) {
        sections.clear();
        return;
    }

    std::vector<uint32_t> offsetMap;
    offsetMap.reserve(num_faces);

    uint32_t lastO = 0;
    for (auto& it : faces) {
        offsetMap.emplace_back(lastO);
        lastO += (it.face_type == 4) ? 20 : 16;
    }
    offsetMap.emplace_back(lastO);


    bool needsSections = std::any_of(selections.begin(), selections.end(), [](const odol_selection& sel)
        {
            return sel.needsSections;
        });

    bool hasAlpha = std::any_of(faceInfo.begin(), faceInfo.end(), [](const PolygonInfo& inf)
        {
            return inf.specialFlags & IsAlpha;
        });
    //#TODO can do the below async
    if (!hasAlpha) {//Get rid of alpha ordering if there is no alpha
        std::for_each(faceInfo.begin(), faceInfo.end(), [](PolygonInfo& inf)
            {
                inf.specialFlags &= ~IsAlphaOrdered;
            });
    }

    if (!needsSections) {
        std::vector<odol_section> sections;
        std::vector<odol_section>::iterator curSec = sections.end();


        bool isSingleSection = true;
        //#TODO use std::none_of and custom face iterator
        for (int i = 0; i < faceInfo.size(); ++i) {
            const PolygonInfo& inf = faceInfo[i];
            if (inf.specialFlags & IsAlpha) {
                isSingleSection = false;
                break;
            }

            if (sections.empty()) {
                sections.emplace_back();
                curSec = sections.end() - 1;
                curSec->face_start = offsetMap[i];
                curSec->face_end = offsetMap[i];

            } else if (curSec->common_texture_index != inf.textureIndex || curSec->common_face_flags != inf.specialFlags || curSec->material_index != inf.materialIndex) {
                if (std::any_of(sections.begin(), sections.end(), [&inf](const odol_section& sec)
                    {
                        return sec.common_texture_index == inf.textureIndex &&
                            sec.common_face_flags == inf.specialFlags &&
                            sec.material_index == inf.materialIndex;
                    })) {
                    isSingleSection = false;
                    break;
                }

                curSec->face_end += (faces[i].face_type == 4) ? 20 : 16;
 
                sections.emplace_back();
                curSec = sections.end() - 1;
                curSec->face_start = offsetMap[i];
                curSec->face_end = offsetMap[i];

            }
        }

        if (isSingleSection) {
            if (curSec != sections.end()) {
                curSec->face_end = offsetMap.back(); //#TODO maybe we need to add one more?
                //#CHECK this using a model with like 4 sections made up of 8 triangles each
            }
            //#TODO set sections member variable
        }

    }

    struct sectionInfo {
        PolygonInfo info;
        //beg, end
        std::vector<std::pair<uint32_t, uint32_t>> startEnds;
        bool hasFace(uint32_t f) {
            for (auto& it : startEnds) {
                if (f >= it.first && f < it.second) return true;
            }
            return false;
        }
        void addFace(uint32_t f) {
            addBegEnd(f, f + 1);
        }
        void addBegEnd(uint32_t beg, uint32_t end) {
            for (auto& it : startEnds) {
                if (it.second < beg || it.first > end) continue;
                it.first = std::min(it.first, beg);
                it.second = std::min(it.second, end);
                return;
            }
            startEnds.emplace_back(beg, end);
        }
        void removeFace(uint32_t f) {
            for (auto& it : startEnds) {
                if (!(f >= it.first && f < it.second)) continue;

                if (it.first < f && it.second > f+1) {
                    startEnds.emplace_back(f + 1, it.second);
                    it.second = f;
                    break; //iterator invalidated
                } else {
                    if (it.first == f) it.first++;
                    else if (it.second == f + 1) it.second--;
                    if (it.first == it.second) {
                        startEnds.erase(std::find(startEnds.begin(), startEnds.end(), it));
                        break; //iterator invalidated
                    }
                }
                break;
            }
        }

    };
    std::vector<sectionInfo> secInfs;

    auto addSection = [&secInfs](const PolygonInfo& inf) -> uint32_t {
        for (int i = 0; i < secInfs.size(); ++i) {
            if (secInfs[i].info == inf) return i;
        }
        sectionInfo newSec;
        newSec.info = inf;

        secInfs.emplace_back(newSec);
        return secInfs.size() - 1;
    };


    int lastSec = -1;
    int lastSecStart = -1;
    for (int i = 0; i < faces.size(); ++i) {
        auto& prop = faceInfo[i];
        if (lastSec > -1) {
            auto& info = secInfs[lastSec];
            if (info.info == prop) continue;
            info.addBegEnd(lastSecStart, i);
        }
        auto secIdx = addSection(prop);
        lastSec = secIdx;
        lastSecStart = i;
    }
    if (lastSec >= 0) {
        auto& info = secInfs[lastSec];
        info.addBegEnd(lastSecStart, faces.size());
    }


    std::for_each(selections.begin(), selections.end(), [&](const odol_selection& sel) {
            if (sel.needsSections && sel.num_faces != 0) {

                for (int i = 0; i < secInfs.size(); ++i) {
                    auto& sec = secInfs[i];


                    sectionInfo split;
                    split.info = sec.info;
                    for (int face = 0; face < sel.num_faces; ++face) {
                        if (sec.hasFace(face)) {
                            split.addFace(face);
                            sec.removeFace(face);
                        }
                    }

                    //split
                    if (split.startEnds.empty()) continue; //no split?
                    if (sec.startEnds.empty())
                        sec = split;//everything moved to split
                    else
                        secInfs.emplace_back(split);//new section
                }
            }
        });

    struct sortyThing {
        const sectionInfo* section;
        uint32_t beg, end;
        uint32_t indexBeg, indexEnd;
    };

    std::vector<sortyThing> sortyThings;

    for (auto& section : secInfs) {
        for (auto& it: section.startEnds) {
            sortyThing newSorty;
            newSorty.beg = offsetMap[it.first];
            newSorty.end = offsetMap[it.second];
            newSorty.indexBeg = it.first;
            newSorty.indexEnd = it.second;
            newSorty.section = &section;
            sortyThings.emplace_back(newSorty);
        }
    }

    //#TODO should be able to multithread this
    std::sort(sortyThings.begin(), sortyThings.end(), [this](sortyThing& l, sortyThing& r) {
        //less than operation.

        auto& sec1 = *l.section;
        auto& sec2 = *r.section;
        bool lAlpha = sec1.info.specialFlags&IsAlphaOrdered;
        bool rAlpha = sec2.info.specialFlags&IsAlphaOrdered;

        if (lAlpha != rAlpha)
            return lAlpha < rAlpha;
        if (lAlpha) {
            return l.beg < r.beg;
        }

        //#TODO move this below into a function

        auto& lTex = sec1.info.textureIndex;
        auto& rTex = sec2.info.textureIndex;
        auto& lMat = sec1.info.materialIndex;
        auto& rMat = sec2.info.materialIndex;
        if (lMat != rMat) {
            if (lMat != -1 && rMat != -1) {
                
                auto& lMatI = materials[lMat];
                auto& rMatI = materials[rMat];

                if (lMatI.vertexshader_id != rMatI.vertexshader_id) return lMatI.vertexshader_id < rMatI.vertexshader_id;
                if (lMatI.pixelshader_id != rMatI.pixelshader_id) return lMatI.pixelshader_id < rMatI.pixelshader_id;

                for (int i = 0; i < std::min(lMatI.textures.size(), rMatI.textures.size()); ++i) {
                    auto& ltex = lMatI.textures[i];
                    auto& rtex = rMatI.textures[i];
                    if (ltex.path != rtex.path) return ltex.path < rtex.path;
                }
                if (lMatI.textures.size() != rMatI.textures.size()) return lMatI.textures.size() < rMatI.textures.size();
            }
            return lMat < rMat;
        }

        if (sec1.info.specialFlags != sec2.info.specialFlags) return sec1.info.specialFlags < sec2.info.specialFlags;
        if (lTex != rTex) return lTex < rTex;
        if (l.section != r.section) return l.section < r.section; //Yes, pointer comparison. They need to be seperated, order doesn't matter
        return false; //they are equal, which means not less than.

        });

    if (hasAlpha) {


        int beg = -1;
        int end = -1;
        for (int i = 0; i < sortyThings.size(); ++i) {
            auto& thing = sortyThings[i];
            if ((thing.section->info.specialFlags & (IsAlpha|IsAlphaOrdered)) == IsAlphaOrdered) {
                if (beg == -1) beg = i;
                end = i + 1;
            } else if (end != -1) {
                //#TODO should be able to multithread this
                std::sort(sortyThings.begin()+beg, sortyThings.begin()+end, [this](sortyThing& l, sortyThing& r) {
                    //less than operation.
                    //#TODO move this into a function
                    auto& sec1 = *l.section;
                    auto& sec2 = *r.section;

                    auto& lTex = sec1.info.textureIndex;
                    auto& rTex = sec2.info.textureIndex;
                    auto& lMat = sec1.info.materialIndex;
                    auto& rMat = sec2.info.materialIndex;
                    if (lMat != rMat) {
                        if (lMat != -1 && rMat != -1) {

                            auto& lMatI = materials[lMat];
                            auto& rMatI = materials[rMat];

                            if (lMatI.vertexshader_id != rMatI.vertexshader_id) return lMatI.vertexshader_id < rMatI.vertexshader_id;
                            if (lMatI.pixelshader_id != rMatI.pixelshader_id) return lMatI.pixelshader_id < rMatI.pixelshader_id;

                            for (int i = 0; i < std::min(lMatI.textures.size(), rMatI.textures.size()); ++i) {
                                auto& ltex = lMatI.textures[i];
                                auto& rtex = rMatI.textures[i];
                                if (ltex.path != rtex.path) return ltex.path < rtex.path;
                            }
                            if (lMatI.textures.size() != rMatI.textures.size()) return lMatI.textures.size() < rMatI.textures.size();
                        }
                        return lMat < rMat;
                    }

                    if (sec1.info.specialFlags != sec2.info.specialFlags) return sec1.info.specialFlags < sec2.info.specialFlags;
                    if (lTex != rTex) return lTex < rTex;
                    if (l.section != r.section) return l.section < r.section; //Yes, pointer comparison. They need to be seperated, order doesn't matter
                    return false; //they are equal, which means not less than.

                    });
                beg = -1;
                end = -1;
            }
        }
        if (end != -1) {
            std::sort(sortyThings.begin() + beg, sortyThings.begin() + end, [this](sortyThing& l, sortyThing& r) {
                //less than operation.
                //#TODO move this into a function
                auto& sec1 = *l.section;
                auto& sec2 = *r.section;

                auto& lTex = sec1.info.textureIndex;
                auto& rTex = sec2.info.textureIndex;
                auto& lMat = sec1.info.materialIndex;
                auto& rMat = sec2.info.materialIndex;
                if (lMat != rMat) {
                    if (lMat != -1 && rMat != -1) {

                        auto& lMatI = materials[lMat];
                        auto& rMatI = materials[rMat];

                        if (lMatI.vertexshader_id != rMatI.vertexshader_id) return lMatI.vertexshader_id < rMatI.vertexshader_id;
                        if (lMatI.pixelshader_id != rMatI.pixelshader_id) return lMatI.pixelshader_id < rMatI.pixelshader_id;

                        for (int i = 0; i < std::min(lMatI.textures.size(), rMatI.textures.size()); ++i) {
                            auto& ltex = lMatI.textures[i];
                            auto& rtex = rMatI.textures[i];
                            if (ltex.path != rtex.path) return ltex.path < rtex.path;
                        }
                        if (lMatI.textures.size() != rMatI.textures.size()) return lMatI.textures.size() < rMatI.textures.size();
                    }
                    return lMat < rMat;
                }

                if (sec1.info.specialFlags != sec2.info.specialFlags) return sec1.info.specialFlags < sec2.info.specialFlags;
                if (lTex != rTex) return lTex < rTex;
                if (l.section != r.section) return l.section < r.section; //Yes, pointer comparison. They need to be seperated, order doesn't matter
                return false; //they are equal, which means not less than.

                });
        }


    }



    std::vector<uint32_t> ordering;
    ordering.resize(num_faces);

    uint32_t curOrder = 0;
    for (auto&it : sortyThings) {
        auto beg = it.indexBeg;
        auto end = it.indexEnd;

        for (uint32_t i = beg; i < end; ++i) {
            ordering[i] = curOrder++;
        }
    }

    //reorder named selections

    for (auto& it : selections) {
        if (it.num_faces == 0) continue;

    
        std::vector<uint32_t> newFaces;
        newFaces.resize(it.faces.size());

        for (uint32_t i = 0; i < it.faces.size(); ++i) {
            newFaces[i] = ordering[it.faces[i]];
        }
        it.faces = newFaces;
    }

    //Reorder faces and build sections
    std::vector<odol_face> newFaces;

    uint32_t begOffs=0;
    odol_section curSec;
    const sectionInfo* lastSorty = nullptr;
    for (auto& it : sortyThings) {


        auto beginOffsetInNewFaces = begOffs;

        for (int i = it.indexBeg; i < it.indexEnd; ++i) {
            newFaces.emplace_back(faces[i]);
        }
        auto offsSize = it.end - it.beg;
        begOffs += offsSize;
        auto endOffsetInNewFaces = begOffs;

        if (lastSorty != it.section) {
            if (lastSorty) {
                sections.emplace_back(curSec);
            }
            curSec = odol_section(); //null out


            curSec.common_texture_index = it.section->info.textureIndex;
            curSec.material_index = it.section->info.materialIndex;
            curSec.common_face_flags = it.section->info.specialFlags;
            curSec.area_over_tex[0] = it.section->info.areaOverTex[0];
            curSec.area_over_tex[1] = it.section->info.areaOverTex[1];
            curSec.unknown_long = it.section->info.order; //#TODO rename unknown long
            //#TODO
            //sections[k].face_index_start = face_start; //quite sure the face index shouldn't even be in there
            //sections[k].face_index_end = face_start;
            curSec.min_bone_index = 0; //I think these are always 0? atleast here?
            curSec.bones_count = 0;// num_bones_subskeleton; //#TODO fixme
            //sections[k].mat_dummy = 0;
            //sections[k].num_stages = 2; // num_stages defines number of entries in area_over_tex

            curSec.face_start_index = it.indexBeg;
            curSec.face_end_index = it.indexEnd;
            curSec.face_start = beginOffsetInNewFaces;
            curSec.face_end = endOffsetInNewFaces;




            lastSorty = it.section;
        } else {
            curSec.face_end = endOffsetInNewFaces;
        }
    }
    if (lastSorty) {
        sections.emplace_back(curSec);
    }
    faces = std::move(newFaces); //move sorted faces
}

uint32_t mlod_lod::add_point(vector3 point, vector3 normal, const uv_pair& uv_coords_input, uint32_t point_index_mlod, const uv_pair& inverseScalingUV) {

    float u_relative = (uv_coords_input.u - minUV.u) * inverseScalingUV.u;
    float v_relative = (uv_coords_input.v - minUV.v) * inverseScalingUV.v;
    //#CHECK the UV code seems to be correct, But check if ingame result looks correct
    short u = (short)(u_relative * 2 * INT16_MAX - INT16_MAX);
    short v = (short)(v_relative * 2 * INT16_MAX - INT16_MAX);
    // Check if there already is a vertex that satisfies the requirements
    for (uint32_t i = 0; i < num_points; i++) { //#TODO make vertex_to_point a unordered_map?
        if (!vertex_to_point.empty() && vertex_to_point[i] != point_index_mlod)
            continue;


        //#TODO check position difference

        // normals and uvs don't matter for non-visual lods
        if (resolution < LOD_GEOMETRY) { //#TODO use pos distance check
            if (normals[i].distance_squared(normal) > 0.0001*0.0001)
                continue;

            if (uv_coords[i].u != u ||
                uv_coords[i].v != v)
                continue;
        }

        return i; //vertex already exists, don't create new one
    }










    // Add vertex
    points.emplace_back(point);
    normals.emplace_back(normal);
    uv_coords.emplace_back(uv_paircompact{u,v});


    //#TODO seems like this belongs into skeleton scan thing
    //No idea what this was supposed to be
    /*
        uint32_t j;
    uint32_t weight_index;

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

            if (mlod_lod.selections[j].vertices[point_index_mlod] == 0)
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
            vertexboneref[num_points].weights[weight_index][1] = mlod_lod.selections[j].vertices[point_index_mlod];

            // convert weight
            if (vertexboneref[num_points].weights[weight_index][1] == 0x01)
                vertexboneref[num_points].weights[weight_index][1] = 0xff;
            else
                vertexboneref[num_points].weights[weight_index][1]--;

            if (model_info.skeleton->is_discrete)
                break;
        }
    }
    */
    
    
    //this is done by the caller of the func
    //vertex_to_point[num_points] = point_index_mlod;
    //point_to_vertex[point_index_mlod] = num_points;

    num_points++;

    return (num_points - 1);



}

void mlod_lod::buildSubskeleton(std::unique_ptr<skeleton_>& skeleton, bool neighbour_faces) {
    


    if (skeleton->num_bones > 0)
        vertexboneref.resize(num_faces * 4 + num_points);




    // Normalize vertex bone ref
    vertexboneref_is_simple = 1;
    float weight_sum;
    if (!vertexboneref.empty()) {
        for (int i = 0; i < num_points; i++) {
            if (vertexboneref[i].num_bones == 0)
                continue;

            if (vertexboneref[i].num_bones > 1)
                vertexboneref_is_simple = 0;

            weight_sum = 0;
            for (int j = 0; j < vertexboneref[i].num_bones; j++) {
                weight_sum += vertexboneref[i].weights[j][1] / 255.0f;
            }

            for (int j = 0; j < vertexboneref[i].num_bones; j++) {
                vertexboneref[i].weights[j][1] *= (1.0 / weight_sum);
            }
        }
    }

//#TODO above implementation is not correct


    //create vertex bone ref

    //if neighbor create neightbor bone ref


    //split sections after bones

    //set sections bone references


    num_bones_skeleton = skeleton->num_bones;
    num_bones_subskeleton = skeleton->num_bones;
    subskeleton_to_skeleton.resize(num_bones_skeleton);
    skeleton_to_subskeleton.resize(num_bones_skeleton);

    for (int i = 0; i < skeleton->num_bones; i++) {
        subskeleton_to_skeleton[i] = i;
        skeleton_to_subskeleton[i].num_links = 1;
        skeleton_to_subskeleton[i].links[0] = i;
    }

    //#TODO the above implementation is not correct.


    //build skeletonToSubSkeleto

    //create hierarchy 5571

    //log if needed to create sections for skeleton



    //lots of stuff







}

void mlod_lod::updateBoundingBox() {

 /*
  * Calculate the bounding box for the given LOD and store it.
 */
    min_pos = empty_vector;
    max_pos = empty_vector;

    bool first = true;

    for (auto& [x, y, z] : points) {
        if (first || x < min_pos.x)
            min_pos.x = x;
        if (first || x > max_pos.x)
            max_pos.x = x;

        if (first || y < min_pos.y)
            min_pos.y = y;
        if (first || y > max_pos.y)
            max_pos.y = y;

        if (first || z < min_pos.z)
            min_pos.z = z;
        if (first || z > max_pos.z)
            max_pos.z = z;

        first = false;
    }

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
    WRITE_CASTED(face_start, sizeof(uint32_t));
    WRITE_CASTED(face_end, sizeof(uint32_t));
    WRITE_CASTED(min_bone_index, sizeof(uint32_t));
    WRITE_CASTED(bones_count, sizeof(uint32_t));

    output.write("\0\0\0\0", 4); //matDummy always 0

    WRITE_CASTED(common_texture_index, sizeof(uint16_t));
    WRITE_CASTED(common_face_flags, sizeof(uint32_t));
    WRITE_CASTED(material_index, sizeof(int32_t));
    if (material_index == -1) //#TODO write surface material path here
        output.put(0); //#TODO surface material?

    uint32_t num_stages = 2; //has to be.
    WRITE_CASTED(num_stages, sizeof(uint32_t)); //#TODO has to be 2!!! Else assert trip

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

    WRITE_CASTED(num_weights, sizeof(uint32_t));
    if (num_weights > 0) {
        output.put(0);
        output.write(reinterpret_cast<char*>(weights.data()), sizeof(uint8_t) * num_weights);
    }
}

void odol_selection::init(std::vector<selectionVertex> verts) {




    weights.clear();
    vertices.clear();
    vertices.resize(verts.size());
    std::transform(verts.begin(), verts.end(), vertices.begin(), [](const selectionVertex& vert) {
        return vert.vertexIndex;
        });
    num_vertices = verts.size();


    //If there are no weights, we just don't store them.
    bool allMax = std::all_of(verts.begin(), verts.end(), [](const selectionVertex& vert) {
        return vert.weight == 255;
        });

    if (!allMax) {
        weights.resize(verts.size());
        std::transform(verts.begin(), verts.end(), weights.begin(), [](const selectionVertex& vert) {
            return vert.weight;
            });
        num_weights = verts.size();
    }
}

void odol_selection::updateSections(mlod_lod& model) {
    //animation.cpp L152
    __debugbreak(); //check
    for (int i = 0; i < model.sections.size(); ++i) {
        auto& sec = model.sections[i];

        bool hasSomeFaces = false;
        bool hasAllFaces = true;

        for (int i = sec.face_start; i < sec.face_end; ++i) {
            
            auto found = std::find(faces.begin(), faces.end(), i);
            if (found != faces.end())
                hasSomeFaces = true;
            else
                hasAllFaces = false;
        }
        if (hasSomeFaces != hasAllFaces) {
            //#TODO throw warning. Section partially contained
        }
        if (hasAllFaces) {
            sections.emplace_back(i);
        }    
    }

}

void model_info::writeTo(std::ostream& output) {
    auto num_lods = lod_resolutions.size();

    output.write(reinterpret_cast<char*>(lod_resolutions.data()), sizeof(float) * num_lods); //#TODO lod resolutions doesn't belong in here. Belongs to parent
    WRITE_CASTED(specialFlags, sizeof(uint32_t)); //#TODO these are special flags
    WRITE_CASTED(bounding_sphere, sizeof(float));
    WRITE_CASTED(geo_lod_sphere, sizeof(float));

    //#TODO
    WRITE_CASTED(remarks, sizeof(uint32_t));
    WRITE_CASTED(andHints, sizeof(uint32_t));
    WRITE_CASTED(orHints, sizeof(uint32_t));
 

    WRITE_CASTED(aiming_center, sizeof(vector3));
    WRITE_CASTED(map_icon_color.value, sizeof(uint32_t));
    WRITE_CASTED(map_selected_color.value, sizeof(uint32_t));
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

    WRITE_CASTED(ht_min, sizeof(float));//>=v42
    WRITE_CASTED(ht_max, sizeof(float));//>=v42
    WRITE_CASTED(af_max, sizeof(float));//>=v42
    WRITE_CASTED(mf_max, sizeof(float));//>=v42
    WRITE_CASTED(mf_act, sizeof(float));//>=v43
    WRITE_CASTED(t_body, sizeof(float));//>=v43
    WRITE_CASTED(force_not_alpha, sizeof(bool));//>=V33
    WRITE_CASTED(sb_source, sizeof(int32_t));//>=v37
    WRITE_CASTED(prefer_shadow_volume, sizeof(bool));//>=v37
    WRITE_CASTED(shadow_offset, sizeof(float));

    WRITE_CASTED(animated, sizeof(bool)); //AnimationTyoe == Software

    if (skeleton)
        skeleton->writeTo(output);
    else
        output.put(0); //Write empty skeleton name

    //#TODO check that animationType is hardware, if you print a skeleton


    WRITE_CASTED(map_type, sizeof(char));

    //#TODO mass array
    uint32_t n_floats = 0;
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


    uint32_t always_0 = 0;
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
                    if (mlod_lods[i].selections[j].vertices[k] > 0) {
                        anim->axis_pos = mlod_lods[i].points[k];
                        break;
                    }
                }
            }
            if (stricmp(mlod_lods[i].selections[j].name.c_str(), anim->end.c_str()) == 0) {
                for (k = 0; k < mlod_lods[i].num_points; k++) {
                    if (mlod_lods[i].selections[j].vertices[k] > 0) {
                        anim->axis_dir = mlod_lods[i].points[k];
                        break;
                    }
                }
            }
        } else if (stricmp(mlod_lods[i].selections[j].name.c_str(), anim->axis.c_str()) == 0) {
            for (k = 0; k < mlod_lods[i].num_points; k++) {
                if (mlod_lods[i].selections[j].vertices[k] > 0) {
                    anim->axis_pos = mlod_lods[i].points[k];
                    break;
                }
            }
            for (k = k + 1; k < mlod_lods[i].num_points; k++) {
                if (mlod_lods[i].selections[j].vertices[k] > 0) {
                    anim->axis_dir = mlod_lods[i].points[k];
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

void MultiLODShape::updateHints(float viewDensityCoef) {

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
    model_info.map_icon_color = mlod_lods[0].icon_color;
    model_info.map_selected_color = mlod_lods[0].selected_color;
#pragma endregion Color

    //In CalculateViewDensity using the coef
#pragma region ViewDensity
    int colorAlpha = model_info.map_icon_color.getA();
    float alpha = colorAlpha * (1.0 / 255); //#TODO color getAsFloat func to packed color type

    float transp = 1 - alpha * 1.5;
    if (transp >= 0.99)
        model_info.view_density = 0;
    else if (transp > 0.01)
        model_info.view_density = log(transp) * 4 * viewDensityCoef;
    else
        model_info.view_density = -100.0f;
#pragma endregion ViewDensity
}

void MultiLODShape::scanProjectedShadow() {
    
    projectedShadow = false;
    if (!(model_info.specialFlags & OnSurface)) { //On surface can't use projected shadows
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

    for (uint32_t i = 0; i < mlod_lods.size(); i++) {
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


void MultiLODShape::write_animations(std::ostream& output) {
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


void MultiLODShape::get_mass_data() {
    // mass is primarily stored in geometry


    mlod_lod *mass_lod = nullptr;

    if (model_info.special_lod_indices.geometry.isDefined()) {
        mass_lod = &mlod_lods[model_info.special_lod_indices.geometry];
    }

    if (!mass_lod || mass_lod->num_points != massArray.size()) {
        if (model_info.special_lod_indices.geometry_physx.isDefined()) {
            mass_lod = &mlod_lods[model_info.special_lod_indices.geometry_physx];
        } else if (model_info.special_lod_indices.geometry_simple.isDefined()) {
            mass_lod = &mlod_lods[model_info.special_lod_indices.geometry_simple];
        } else if(model_info.special_lod_indices.geometry.isDefined()) {
            mass_lod = &mlod_lods[model_info.special_lod_indices.geometry];
        }
    }

    if (!mass_lod) return;

    //// mass data available?
    //if (i >= num_lods || mlod_lods[i].num_points == 0) {
    //    model_info.mass = 0;
    //    model_info.mass_reciprocal = 1;
    //    model_info.inv_inertia = identity_matrix;
    //    model_info.centre_of_mass = empty_vector;
    //    return;
    //}

    if (mass_lod->num_points != massArray.size()) {//shape.cpp L6967
        //#TODO what's here?
    }
        






    vector3 centerOfMass = empty_vector;
    float mass = 0;
    for (int i = 0; i < mass_lod->num_points; i++) {
        auto& pos = mass_lod->points[i];
        mass += massArray[i];
        centerOfMass += pos * massArray[i];
    }
    
    model_info.centre_of_mass = (mass > 0) ? centerOfMass * (1 / mass) : empty_vector;

    matrix inertia = empty_matrix;
    for (int i = 0; i < mass_lod->num_points; i++) {
        auto& pos = mass_lod->points[i];
        matrix r_tilda = vector_tilda(pos - model_info.centre_of_mass);
        inertia = matrix_sub(inertia, matrix_mult_scalar(massArray[i], matrix_mult(r_tilda, r_tilda)));
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
    } else {
        model_info.mass_reciprocal = 1;
        model_info.inv_inertia = identity_matrix;
    }
    massArray.clear();
}

void MultiLODShape::optimizeLODS() {
    

    int offs = 0;
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
            mlod_lods[offs] = std::move(mlod_lods[i]);//#TODO do nothing if offs==i
            //#TODO move resolution too
            offs++;
        }
    }

    mlod_lods.resize(offs);
    num_lods = offs;

    int i = 0; //#TODO replace by find_if
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

void MultiLODShape::updateBoundingSphere() {
    float sphere1 = 0.f;
    for (auto& lod : mlod_lods) {
        sphere1 = std::max(sphere1, lod.getBoundingSphere({0,0,0}));
    }

    //#TODO anim phase

   model_info.bounding_sphere = sqrt(sphere1);

}

void MultiLODShape::updateBounds() {


    model_info.bbox_min = { 10e10,10e10,10e10 };
    model_info.bbox_max = { -10e10,-10e10,-10e10 };
    //global bbox
    for (auto& lod : mlod_lods) {
        model_info.bbox_min.x = std::min(model_info.bbox_min.x, lod.min_pos.x);
        model_info.bbox_min.y = std::min(model_info.bbox_min.y, lod.min_pos.y);
        model_info.bbox_min.z = std::min(model_info.bbox_min.z, lod.min_pos.z);
        model_info.bbox_max.x = std::max(model_info.bbox_max.x, lod.max_pos.x);
        model_info.bbox_max.y = std::max(model_info.bbox_max.y, lod.max_pos.y);
        model_info.bbox_max.z = std::max(model_info.bbox_max.z, lod.max_pos.z);
    }


    if (model_info.numberGraphicalLods == num_lods) {
        model_info.bbox_visual_min = model_info.bbox_min;
        model_info.bbox_visual_max = model_info.bbox_max;
    } else {
        

        model_info.bbox_visual_min = { 10e10,10e10,10e10 };
        model_info.bbox_visual_max = { -10e10,-10e10,-10e10 };



        uint8_t idx = 0;
        //global bbox
        for (auto& lod : mlod_lods) {
            if (idx == model_info.numberGraphicalLods) break; //only check vis lods

            model_info.bbox_visual_min.x = std::min(model_info.bbox_visual_min.x, lod.min_pos.x);
            model_info.bbox_visual_min.y = std::min(model_info.bbox_visual_min.y, lod.min_pos.y);
            model_info.bbox_visual_min.z = std::min(model_info.bbox_visual_min.z, lod.min_pos.z);
            model_info.bbox_visual_max.x = std::max(model_info.bbox_visual_max.x, lod.min_pos.x);
            model_info.bbox_visual_max.y = std::max(model_info.bbox_visual_max.y, lod.min_pos.y);
            model_info.bbox_visual_max.z = std::max(model_info.bbox_visual_max.z, lod.min_pos.z);
            idx++;
        }
        if (idx == 0) {//Wut? no graphical lods?
            model_info.bbox_visual_min = model_info.bbox_min;
            model_info.bbox_visual_max = model_info.bbox_max;
        }

    }

#pragma region BoundingSphere
    //if (!model_info.lock_autocenter) .... useless as this is always false

    model_info.bounding_center = empty_vector;


    vector3 boundingCenterChange;
    if (model_info.autocenter && num_lods > 0) {
        boundingCenterChange = (model_info.bbox_min + model_info.bbox_max) * 0.5f;


        //#TODO check clip and onsurface flags of lod0


    }
    else {
        //boundingCenterChange = -model_info.bounding_center; useless. It's already 0
    }
    if (boundingCenterChange.magnitude_squared() < 1e-10 && model_info.bounding_sphere > 0) {
        updateBoundingSphere();
    }


    //Adjust existing positions for changed center
    //#TODO if change is 0, don't do anything here
    model_info.bounding_center += boundingCenterChange;


    model_info.bbox_min -= boundingCenterChange;
    model_info.bbox_max -= boundingCenterChange;


    model_info.bbox_visual_min -= boundingCenterChange;
    model_info.bbox_visual_max -= boundingCenterChange;

    model_info.aiming_center -= boundingCenterChange;

    for (auto& lod : mlod_lods) {

        for (auto& it : lod.points) {
            it -= boundingCenterChange;
        }
        //#TODO update animation phases for lod


    }






    //#TODO apply bounding center change to all lods

    updateBoundingSphere();


    //#TODO apply bounding center change to tree crown

    //#TODO if change is big enough, recalculate normals



#pragma endregion BoundingSphere


    float sphere;

    //#TODO fix this up. Seems like boundingSphere should == boundingCenterChange.magnitude?

    //#TODO move geo_lod_sphere to initConvexComponents

    // Spheres
    //model_info.bounding_sphere = 0.0f;
    //model_info.geo_lod_sphere = 0.0f;
    //for (uint32_t i = 0; i < num_lods; i++) {
    //    if (model_info.autocenter)
    //        sphere = mlod_lods[i].getBoundingSphere(model_info.centre_of_mass);
    //    else
    //        sphere = mlod_lods[i].getBoundingSphere(empty_vector);
    //
    //    if (sphere > model_info.bounding_sphere)
    //        model_info.bounding_sphere = sphere;
    //    if (mlod_lods[i].resolution == LOD_GEOMETRY)
    //        model_info.geo_lod_sphere = sphere;
    //}
}


void MultiLODShape::build_model_info() {


    model_info.lod_resolutions.resize(num_lods);

    for (int i = 0; i < num_lods; i++)
        model_info.lod_resolutions[i] = mlod_lods[i].resolution;

    model_info.specialFlags = 0; //#TODO special flags. Is this init correct?

    //#TODO make these default values and get rid of this
    model_info.lock_autocenter = false; //#TODO this is always false!

    //#TODO treeCrownNeeded shape.cpp 8545
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

    updateBounds();

    updateHints(viewDensityCoef);


    // Centre of mass, inverse inertia, mass and inverse mass
    //#TODO only if massArray is there. Also have to build mass array
    if (!massArray.empty()) get_mass_data();

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

    scanProjectedShadow(); //make sure flag is set

    auto sbSourceProp = getPropertyGeo("sbsource");


    if (!sbSourceProp) {
       //autodetect

        auto shadowProp = getPropertyGeo("shadow");

        if (shadowProp && *shadowProp == "hybrid") {
            //model_info.sb_source = _projShadow ? SBSource::Visual : SBSource::None;
            model_info.prefer_shadow_volume = false;
        } else if (!shadowVolumes.empty()) {
            //#TODO see *sbSourceProp == "shadowvolume" lower down thing that needs shapeSV. This seems to be same
        } else {
            model_info.sb_source = projectedShadow ? SBSource::Visual : SBSource::None;
        }
       
    } else if (*sbSourceProp == "visual") {
        model_info.sb_source = SBSource::Visual;

        if (model_info.shadowBufferCount > 0) {
            //warning
            //("Warning: %s: Shadow buffer levels are present, but visual LODs for SB rendering are required (in sbsource property)", modelname);
        }
        if (!projectedShadow) {
            //error
            //("Error: %s: Shadows cannot be drawn for this model, but 'visual' source is specified (in sbsource property)", modelname);
            model_info.sb_source = SBSource::None;
        }

    } else if (*sbSourceProp == "explicit") {
        if (model_info.shadowBufferCount <= 0) {
            //error
            //("Error: %s: Explicit shadow buffer levels are required (in sbsource property), but no one is present - forcing 'none'", modelname);
            model_info.sb_source = SBSource::None;
        } else {
            model_info.sb_source = SBSource::Explicit;
        }
    } else if (*sbSourceProp == "shadowvolume") {//#TODO should this be lowercase?
        if (shadowVolumes.empty() || shadowVolumes.size() != shadowVolumesResolutions.size()) {
            //error
            //("Error: %s: Shadow volume lod is required for shadow buffer (in sbsource property), but no shadow volume lod is present", modelname);
            model_info.sb_source = SBSource::None;
        } else {


            ComparableFloat<std::milli> lastResolution = LOD_SHADOW_VOLUME_START;
            for (int i = 0; i < shadowVolumes.size(); ++i) {
                auto res = shadowVolumesResolutions[i]; //#TODO why not just get it from the shadowVolume?

                if (res > LOD_SHADOW_STENCIL_START && res < LOD_SHADOW_STENCIL_END)
                    res = LOD_SHADOW_VOLUME_START + (res - LOD_SHADOW_STENCIL_START);
                else
                    res = lastResolution + 10.f;
                lastResolution = res;

                //#TODO add shape, and set resoltion member variable

                //#TODO apply bounding center offset and fix minMax

                //Run customizeShape

                //shape.cpp 8717


            }
        }



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
        auto& preferredShadowVolumeLod = model_info.preferredShadowVolumeLod;//#TODO these should be -1 by default
        auto& preferredShadowBufferLod = model_info.preferredShadowBufferLod;
        auto& preferredShadowBufferVisibleLod = model_info.preferredShadowBufferVisibleLod;

        for (auto& it : mlod_lods) {
            auto SLprop = it.getProperty("shadowlod");
            int32_t SL = SLprop ? stoi(*SLprop) : -1;
            auto SVLprop = it.getProperty("shadowvolumelod");
            int32_t SVL = SVLprop ? stoi(*SVLprop) : -1;
            auto SBLprop = it.getProperty("shadowbufferlod");
            int32_t  SBL = SBLprop ? stoi(*SBLprop) : -1;
            auto SBLVprop = it.getProperty("shadowbufferlodvis"); //#TODO rename preferredShadowBufferVisibleLod swap vis and lod
            int32_t SBLV = SBLVprop ? stoi(*SBLVprop) : -1;

            if (SBL == -1) {
                SBL = SL;
                if (SBL == -1)
                    SBL = SVL;
            }
            if (SVL == -1) {
                SVL = SL;
                if (SVL == -1)
                    SVL = SBL;
            }

            preferredShadowVolumeLod.emplace_back(SVL);
            preferredShadowBufferLod.emplace_back(SBL);
            preferredShadowBufferVisibleLod.emplace_back(SBLV);
        }
    }
#pragma endregion PreferredShadowLODs


    optimizeLODS();
    updateBounds();//If we removed lods, we need to update bounding.
    //#TODO only update bounds if optimizeLODS actually changed anything to save a little perf


#pragma region Special Flags
    for (auto& it : mlod_lods) {
        model_info.specialFlags |= it.getSpecialFlags();
    }
#pragma endregion Special Flags

    //CalculateMinMax again
    if (!massArray.empty()) get_mass_data();

    //Read modelconfig and grab properties
    //checkForcedProperties
    if (modelConfig.isLoaded() && model_info.special_lod_indices.geometry.isDefined()) {
        //#TODO we are supposed to generate a GEOM level if it doesn't exist...
        auto entr = modelConfig.getModelConfig()->getArrayOfStringViews({ "properties" });
        if (entr && num_lods) {

            auto& geoLod = mlod_lods[model_info.special_lod_indices.geometry];

            while (entr->size() >= 2) { //#TODO use fori loop
                std::string_view p1 = entr->front(); entr->erase(entr->begin());
                std::string_view p2 = entr->front(); entr->erase(entr->begin());
                property newProp;
                newProp.name = p1;
                newProp.value = p2;

                geoLod.properties.emplace_back(newProp);

            }
        }
    }
    shapeListUpdated(); //properties can change things? Can they? If we really generate a geom lod then indeed they do






#pragma region CanOcclude
    {
        int complexity = mlod_lods[0].num_faces;
        float size = model_info.bounding_sphere;
        uint32_t viewComplexity = 0;

        if (model_info.special_lod_indices.geometry_view.isDefined())
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


    if (geoLodID.isDefined()) {
        auto& geoLod = mlod_lods[geoLodID];
        model_info.geometry_center = (geoLod.max_pos + geoLod.min_pos)*0.5;
        model_info.geo_lod_sphere = geoLod.max_pos.distance(geoLod.min_pos)*0.5;
        model_info.aiming_center = model_info.geometry_center;
    }

    auto memLodID = model_info.special_lod_indices.memory;

    if (memLodID.isDefined()) {
        auto& memLod = mlod_lods[memLodID];

        //#TODO make getMemoryPoint function
        auto found = std::find_if(memLod.selections.begin(), memLod.selections.end(), [](const auto& it) {
            return it.name == "zamerny";
        });
        vector3 aimMemPoint;
        bool memPointExists = false;
        if (found != memLod.selections.end()) {
            if (found->vertices.empty()) {
                //#TODO warning "No point in selection %s"
            } else {
                memPointExists = found->vertices[0] >= 0;

                aimMemPoint = memLod.points[found->vertices[0]];
                model_info.aiming_center = aimMemPoint;
            }
        }



    }
    //#TODO calculateNormals stuff in initPlanes


#pragma endregion initConvexComp


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

    updateHints(viewDensityCoef);

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
        if (dammageProp) {
             model_info.destruct_type = std::move(*dammageProp);
            //#TODO print warning about wrong name
        }
    }

    auto frequentProp = getPropertyGeo("frequent");
    if (frequentProp) model_info.property_frequent = stoi(*frequentProp) != 0;

#pragma endregion ClassDamageFreqProps

    //#TODO load skeleton source thing?


   


    //#TODO set animation enabled thingy to false
    if (auto success = model_info.skeleton->read(modelConfig, logger); success > 0) {
        logger.error(modelConfig.getSourcePath().string(), 0, "Failed to read model config.\n");
        //return success; 
        //#TODO throw
    }

    //animations are loaded inside skeleton read





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

    BuildSpecialLodList();
}

void MultiLODShape::finishLOD(mlod_lod& lod, uint32_t lodIndex, float resolution) {

    //Customize Shape (shape.cpp 8468)
        //detect empty lods if non-first and res>900.f


    if (resolution < 900.f && lod.num_points == 0 && lodIndex != 0) {
        //#TODO warning empty lod detected, get name of LOD and name of model
    }

    //Delete back faces?



    //Build sections based on model.cfg

    if (modelConfig.isLoaded()) {

        auto curCfg = modelConfig.getModelConfig();


        while (curCfg) { //shape 2476

            auto sectionsRd = curCfg->getArrayOfStrings({ "sections" });
            if (!sectionsRd) {
                logger.error("Failed to read sections.\n");
                //return -1;
                //#TODO throw exception
            } else {
                for (auto&& it : *sectionsRd)
                    if (!it.empty()) {


                        auto found = lod.findSelectionByName(it);
                        if (found) (*found)->needsSections = true;
                        //#TODO this really doesn't belong here, skeleton is not initalized yet
                        model_info.skeleton->sections.emplace_back(it); //#TODO this (skeleton->sections) doesn't belong here, find out where it comes from and what it's used for
                    }
                        
            }

            auto sectionsInherit = modelConfig.getModelConfig()->getString({ "sectionsInherit" });
            if (!sectionsInherit) {
                curCfg = nullptr;
            } else {
                curCfg = modelConfig.getConfig()->getClass({ "CfgModels", *sectionsInherit });
            }
        }

        model_info.skeleton->num_sections = model_info.skeleton->sections.size();
    }

    //scan for proxies
    for (auto& selection : lod.selections) {
        if (std::string_view(selection.name).substr(0,6) == "proxy:") {
            
            if (selection.vertices.size() != 3) {
                //#TODO error bad proxy object, not correct amount of verticies on proxy, also log proxy name
                continue;
            }
            if (selection.faces.size() != 1) {
                //#TODO error Proxy object should be single face, also log proxy name
                if (selection.faces.empty()) continue;
            }
            for (auto& it : selection.faces) {
                auto& face = lod.faceInfo[it];
                face.specialFlags |= FLAG_ISHIDDENPROXY;
                face.specialFlags &= ~IsHidden; //Can't be hidden
            }
        }
    }


    //shape.cpp 7927

    bool neighbourFaces = false;
    if (resIsShadowVolume(resolution)) {//#TODO don't really need resolution param to finishLOD, can just take from LOD

        //#TODO shadow geometry processing on lod

        for (auto& it : lod.faceInfo) {
            it.materialIndex = -1; //Remove material
            it.textureIndex = -1; //Remove texture
            it.specialFlags &= IsHiddenProxy | NoShadow;

        }
        neighbourFaces = true; //#TODO just call resIsShadowVolume again
    }

    //If shadow volume do extra stuff shape.cpp L7928

    if (lod.resolution > LOD_SHADOW_VOLUME_START && lod.resolution < LOD_SHADOW_VOLUME_END) {
        for (auto& it : lod.faceInfo) {
            it.specialFlags &= IsHiddenProxy | NoShadow;
            if (it.textureIndex != -1 && is_alpha(lod.textures[it.textureIndex])) {
                it.specialFlags |= IsTransparent;
            } else {
                it.textureIndex = -1; //Remove texture
            }
            it.materialIndex = -1; //Remove material
        }
    }

    //Build sections here
    //#TODO not sure if that implementation below is correct

    lod.buildSections();    

    // If shadow buffer, set everything (apart of base texture if not opaque) to predefined values
    
    if (model_info.skeleton) { //#TODO make sure this is loaded after htMin/htMax/afMax and so on are read in readlod
        lod.buildSubskeleton(model_info.skeleton, neighbourFaces);
    }

    //Create subskeleton



    //rescan sections





    if (resIsShadowVolume(resolution)) {    //update material if shadowVOlume

        Material shadowMaterial(logger);


        shadowMaterial.vertexshader_id = 6; //Shadow volume
        shadowMaterial.pixelshader_id = 146; //empty
        shadowMaterial.fogMode = FogMode::None;

        auto shadowMatOffs = lod.materials.size();
        lod.materials.emplace_back(std::move(shadowMaterial));

        //#TODO this should run on sections
        for (auto& it : lod.faceInfo) {
            it.materialIndex = shadowMatOffs;
            it.textureIndex = -1;
        }

        //#TODO make a global shadow material and set this here
        //txtPreload.cpp L195



    }

    //GenerateSTArray?
    //Warning: %s:%s: 2nd UV set needed, but not defined
    // Clear the point and vertex conversion arrays




    //#TODO this should go into finishLOD
    // Proxies

    for (int i = 0; i < lod.num_selections; ++i) {
        auto& selection = lod.selections[i];
        if (std::string_view(selection.name).substr(0, 6) != "proxy:") continue;

        std::string_view selectionNameNoProxy = std::string_view(selection.name).substr(0, 6);



        if (selection.vertices.size() != 3) {
            //#TODO we already log error bad proxy object above.. We should log again here, but why
            continue;
        }
        if (selection.faces.size() != 1) {
            //#TODO error Proxy object should be single face, also log proxy name
            //These are just the same checks as above :thinking:
            if (selection.faces.empty()) continue; //#TODO warn if empty. Also do in above check
        }

        odol_proxy newProxy;
        newProxy.name = selectionNameNoProxy;
        char* endptr; //#TODO use stol here or from_chars using string_view
        newProxy.proxy_id = strtol(strrchr(selectionNameNoProxy.data(), '.') + 1, &endptr, 10);

        auto newLength = strrchr(newProxy.name.c_str(), '.') - newProxy.name.data();
        newProxy.name.resize(newLength);
        //#TODO make that better ^

        std::transform(newProxy.name.begin(), newProxy.name.end(), newProxy.name.begin(), tolower);


        newProxy.selection_index = i;
        newProxy.bone_index = -1;
        newProxy.section_index = -1;

        //#TODO shape.cpp 8092
        //if (!vertexboneref.empty() &&
        //    vertexboneref[faces[face].table[0]].num_bones > 0) {
        //    proxies[k].bone_index = vertexboneref[faces[face].table[0]].weights[0][0];
        //}




        //#TODO shape.cpp 8123
        //for (j = 0; j < num_sections; j++) {
        //    if (face > sections[j].face_start)
        //        continue;
        //    proxies[k].section_index = j;
        //    break;
        //}




        auto& proxyFace = lod.faces[selection.faces.front()];

        //#TODO fixup rotation? Shortest distance and such.
        //shape.cpp 8074

        newProxy.transform_y = (
            lod.points[proxyFace.points[1]]
            -
            lod.points[proxyFace.points[0]]).normalize();

        newProxy.transform_z = (
            lod.points[proxyFace.points[2]]
            -
            lod.points[proxyFace.points[0]]).normalize();

        newProxy.transform_x =
            newProxy.transform_y.cross(newProxy.transform_z);

        newProxy.transform_n =
            lod.points[proxyFace.points[0]];

        lod.proxies.emplace_back(std::move(newProxy));
    }

    lod.num_proxies = lod.proxies.size();




    //Look at TODO "this should go into finishLOD" and move that here
    // scan selections for proxy objects


    //optimize if no selections?
    //if (geometryOnly&GONoUV)
    //else recalculate facearea

    lod.faceArea = 0.f;
    for(auto& it : lod.sections) {
        float area = 0.f;
        for (int i = it.face_start_index; i < it.face_end_index; ++i) {
            area += lod.faces[i].getArea(lod.points);
        }
        if (!(it.common_face_flags & IsHiddenProxy)) {
            lod.faceArea += area;

            if (it.common_face_flags & NoBackfaceCull)
                lod.faceArea += area;
        }
    }


    //Done
}

int MultiLODShape::read_lods(std::istream &source, uint32_t num_lods) {
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


    std::vector<float> masstempPhysx;
    for (uint32_t i = 0; i < num_lods; i++) {
        source.read(buffer, 4);
        if (strncmp(buffer, "P3DM", 4) != 0) //#TODO move into lod load
            return 0;


        std::vector<float> masstemp;
        mlod_lod newLod;

        if (!newLod.read(source, logger, masstemp)) return 0;

        //#TODO ResolGeometryOnly on resolution
        //We might want to remove normals (set them all to 0,1,0)
        //or UV's (set them all to 0 in the faces)

        if (!masstemp.empty()) {
            if (newLod.resolution == LOD_PHYSX) {
                masstempPhysx = std::move(masstemp);
            } else {
                massArray = std::move(masstemp);
            }
        }

        if (resIsShadowVolume(newLod.resolution)) { //Check if shadow volume
            shadowVolumes.emplace_back(newLod); //copy it.
            shadowVolumesResolutions.emplace_back(newLod.resolution);
            
        }
        //If lod is shadow volume, copy it into an array with shadow volume lods
        //Also keep an array of sahdow volume resolitoons "shapeSVResol"

        //AddShape? Yeah. The ScanShapes thing inside there.
        mlod_lods.emplace_back(std::move(newLod));

        shapeListUpdated();

        finishLOD(mlod_lods.back(), i, newLod.resolution);



        //Tree crown needed
        //int ShapePropertiesNeeded(const TexMaterial *mat) ? set _shapePropertiesNeeded based on surface material

        //Blending required

        
    }

    //Only use physx lod mass if geo is 0 mass
    auto totalMass = std::accumulate(massArray.begin(), massArray.end(), 0.f);
    if (totalMass == 0.f && !masstempPhysx.empty())
        massArray = std::move(masstempPhysx);


    return mlod_lods.size();
}


void MultiLODShape::getBoundingBox(vector3 &bbox_min, vector3 &bbox_max, bool visual_only, bool geometry_only) {
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

        for (auto&[x, y, z] : lod.points) {
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
            if (tex.front() != '#')
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

    if (auto success = modelConfig.load(sourceFile, logger); success != 0) {
        logger.error(sourceFile.string(), 0, "Failed to read model config.\n");
        return success;
    }

    //default values are 0

    // Read thermal stuff
    if (modelConfig.isLoaded()) {
        if (modelConfig.getConfig()->hasEntry({ "htMin" })) model_info.ht_min = *modelConfig.getConfig()->getFloat({ "htMin" });
        if (modelConfig.getConfig()->hasEntry({ "htMax" })) model_info.ht_max = *modelConfig.getConfig()->getFloat({ "htMax" });
        if (modelConfig.getConfig()->hasEntry({ "afMax" })) model_info.af_max = *modelConfig.getConfig()->getFloat({ "afMax" });
        if (modelConfig.getConfig()->hasEntry({ "mfMax" })) model_info.mf_max = *modelConfig.getConfig()->getFloat({ "mfMax" });
        if (modelConfig.getConfig()->hasEntry({ "mfAct" })) model_info.mf_act = *modelConfig.getConfig()->getFloat({ "mfAct" });
        if (modelConfig.getConfig()->hasEntry({ "tBody" })) model_info.t_body = *modelConfig.getConfig()->getFloat({ "tBody" });
    }




    char typeBuffer[5];
    input.read(typeBuffer, 5);

    if (strncmp(typeBuffer, "MLOD", 4) != 0) {
        if (strcmp(args.positionals[0], "binarize") == 0)
            logger.error(sourceFile.string(), 0, "Source file is not MLOD.\n");
        return -3;
    }

    input.seekg(8);
    input.read(reinterpret_cast<char*>(&num_lods), 4);

    model_info.skeleton = std::make_unique<skeleton_>();
    num_lods = read_lods(input, num_lods);
    if (num_lods <= 0) {
        logger.error(sourceFile.string(), 0, "Failed to read LODs.\n");
        return 4;
    }

    // Write model info
    build_model_info();
    //#TODO catch throw from skeleton config read








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
    if (model_info.skeleton && model_info.skeleton->num_animations > 0) {
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

        // write to file
        mlod_lods[i].writeODOL(output);

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

    //#TODO


    output.write("\x00\x03\x03\x03\x00\x00\x00\x00", 8);
    output.write("\x00\x03\x03\x03\x00\x00\x00\x00", 8);
    output.write("\x00\x00\x00\x00\x00\x03\x03\x03", 8);
    output.write("\x00\x00\x00\x00\x00\x03\x03\x03", 8);
    output.write("\x00\x00\x00\x00", 4);
    return 0;
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

