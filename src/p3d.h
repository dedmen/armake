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


#define P3DVERSION 71

#define MAXTEXTURES 128
#define MAXMATERIALS 128
#define MAXPROPERTIES 128

#define LOD_GRAPHICAL_START                              0.0f
#define LOD_GRAPHICAL_END                              999.9f
#define LOD_VIEW_GUNNER                               1000.0f
#define LOD_VIEW_PILOT                                1100.0f
#define LOD_VIEW_CARGO                                1200.0f
#define LOD_SHADOW_STENCIL_START                     10000.0f
#define LOD_SHADOW_STENCIL_END                       10999.0f
#define LOD_SHADOW_VOLUME_START                      11000.0f
#define LOD_SHADOW_VOLUME_END                        11999.0f
#define LOD_EDIT_START                               20000.0f
#define LOD_EDIT_END                                 20999.0f
#define LOD_GEOMETRY                        1e13f
#define LOD_GEOMETRY_SIMPLE                 LOD_GEOMETRY*2
#define LOD_PHYSX_OLD                       LOD_GEOMETRY*3
#define LOD_PHYSX                           LOD_GEOMETRY*4
#define LOD_MEMORY                        1e15f
#define LOD_LAND_CONTACT                  LOD_MEMORY*2
#define LOD_ROADWAY                       LOD_MEMORY*3
#define LOD_PATHS                         LOD_MEMORY*4
#define LOD_HITPOINTS                     LOD_MEMORY*5
#define LOD_VIEW_GEOMETRY                 LOD_MEMORY*6
#define LOD_FIRE_GEOMETRY                 LOD_MEMORY*7
#define LOD_VIEW_CARGO_GEOMETRY           LOD_MEMORY*8
#define LOD_VIEW_CARGO_FIRE_GEOMETRY      9000000000000000.0f
#define LOD_VIEW_COMMANDER               10000000000000000.0f
#define LOD_VIEW_COMMANDER_GEOMETRY      11000000000000000.0f
#define LOD_VIEW_COMMANDER_FIRE_GEOMETRY 12000000000000000.0f
#define LOD_VIEW_PILOT_GEOMETRY          LOD_MEMORY*13
#define LOD_VIEW_PILOT_FIRE_GEOMETRY     14000000000000000.0f
#define LOD_VIEW_GUNNER_GEOMETRY         LOD_MEMORY*15
#define LOD_VIEW_GUNNER_FIRE_GEOMETRY    LOD_MEMORY*16
#define LOD_SUB_PARTS                    LOD_MEMORY*17
#define LOD_SHADOW_VOLUME_VIEW_CARGO     LOD_MEMORY*18
#define LOD_SHADOW_VOLUME_VIEW_PILOT     LOD_MEMORY*19
#define LOD_SHADOW_VOLUME_VIEW_GUNNER    LOD_MEMORY*20
#define LOD_WRECK                        LOD_MEMORY*21

#define FLAG_NOZWRITE            0x10
#define FLAG_NOSHADOW            0x20
#define FLAG_NOALPHAWRITE        0x80
#define FLAG_ISALPHA            0x100
#define FLAG_ISTRANSPARENT      0x200
#define FLAG_NOCLAMP           0x2000
#define FLAG_CLAMPU            0x4000
#define FLAG_CLAMPV            0x8000
#define FLAG_ISALPHAORDERED   0x20000
#define FLAG_NOCOLORWRITE     0x40000
#define FLAG_ISALPHAFOG       0x80000
#define FLAG_DSTBLENDZERO    0x100000
#define FLAG_ISHIDDENPROXY 0x10000000

#define CLAMPLIMIT (1.0 / 128)

#define NOPOINT UINT32_MAX //=-1 as int32_t


//#include "utils.h"
#include "model_config.h"
#include "material.h"
#include "matrix.h"
#include <vector>
#include <memory>
#include "logger.h"


struct model_info;

struct uv_pair {
    float u;
    float v;
};


struct uv_paircompact {
    short u;
    short v;
};

struct property {
    std::string name;
    std::string value;
};

struct pseudovertextable {
    uint32_t points_index;
    uint32_t normals_index;
    float u;
    float v;
};

struct mlod_face {
    uint32_t face_type;
    struct pseudovertextable table[4];
    uint32_t face_flags;
    //std::string texture_name;
    int texture_index { -1 }; //#TODO create "optional" unsigned int type. Where the "value not set" indicator is just all bits on 1
    //std::string material_name;
    int material_index { -1 }; //#TODO nullableintegral
    std::string section_names;
    bool operator<(const mlod_face& other) {
        uint32_t compare;
        compare = material_index - other.material_index;
        if (compare != 0)
            return compare < 0;

        compare = face_flags - other.face_flags;
        if (compare != 0)
            return compare < 0;

        compare = texture_index - other.texture_index;
        if (compare != 0)
            return compare < 0;

        return section_names.compare(other.section_names) < 0;
    }


};


struct odol_face {
    uint8_t face_type;
    std::array<uint32_t, 4> points;



    float getArea(const std::vector<point>& pointPos) {

        const vector3& p1 = pointPos[points[0]].pos;
        const vector3& p2 = pointPos[points[1]].pos;
        const vector3& p3 = pointPos[points[2]].pos;


        float area = ((p2 - p1).cross(p3 - p1)).magnitude();

        if (face_type == 4) {
            const vector3& p1 = pointPos[points[0]].pos;
            const vector3& p2 = pointPos[points[2]].pos;
            const vector3& p3 = pointPos[points[3]].pos;


            area += ((p2 - p1).cross(p3 - p1)).magnitude();
        }
        return area * 0.5f;
    }

};

struct odol_proxy {
    std::string name;
    vector3 transform_x;
    vector3 transform_y;
    vector3 transform_z;
    vector3 transform_n;
    uint32_t proxy_id;
    uint32_t selection_index;
    int32_t bone_index;
    uint32_t section_index;
};

struct odol_bonelink {
    uint32_t num_links;
    uint32_t links[4];
};

struct odol_section {

    void writeTo(std::ostream& output);


    uint32_t face_start;
    uint32_t face_end;
    uint32_t face_index_start;
    uint32_t face_index_end;
    uint32_t min_bone_index;
    uint32_t bones_count;
    uint32_t mat_dummy;
    int16_t common_texture_index;
    uint32_t common_face_flags;
    int32_t material_index;
    uint32_t num_stages;
    float area_over_tex[2];
    uint32_t unknown_long;
};

struct odol_selection {

    void writeTo(std::ostream& output);

    std::string name;

    class selectionVertex {
    public:
        uint32_t vertexIndex;
        uint8_t weight;
    };

    void init(std::vector<selectionVertex> verts);

    uint32_t num_faces;
    std::vector<uint32_t> faces;
    uint32_t always_0;
    bool is_sectional;
    uint32_t num_sections;
    std::vector<uint32_t> sections;

    uint32_t num_vertices;
    std::vector<uint32_t> vertices;

    uint32_t num_weights;
    std::vector<uint8_t> weights;


};

struct odol_frame {
};

struct odol_vertexboneref {
    uint32_t num_bones;
    uint8_t weights[4][2];
};




struct mlod_selection {
    std::string name;
    std::vector<uint8_t> points;
    std::vector<uint8_t> faces;
};

class mlod_lod {
public:

    float getBoundingSphere(const vector3& center);

    bool read(std::istream& source, Logger& logger, std::vector<float> &mass);
    uint32_t getAndHints();
    uint32_t getOrHints();
    uint32_t getSpecialFlags();
    void buildSections();

    uint32_t add_point(
        vector3 point, vector3 normal, const uv_pair &uv_coords_input,
        uint32_t point_index_mlod, const uv_pair& inverseScalingUV);






    uint32_t orHints;
    uint32_t andHints;
    uint32_t special;

    uint32_t num_points;
    uint32_t num_faces;
    uint32_t num_sharp_edges;
    std::vector<vector3> points;

    //#TODO ^ points seems wrong. Shouldn't be here. Should just be vector of vec3 without flags
    std::vector<vector3> normals;

    uv_pair minUV;
    uv_pair maxUV;

    std::vector<uv_paircompact> uv_coords;

    class PolygonInfo { //Special data about a polygon
    public:
        int textureIndex;
        int materialIndex;
        int specialFlags;
        float areaOverTex[2];
        int order; //#TODO?
    };

    std::vector<odol_face> faces;
    std::vector<PolygonInfo> faceInfo;
    std::vector<uint32_t> sharp_edges;
    std::vector<property> properties;
    ComparableFloat<std::milli> resolution; //Yet milli is correct. That's what Arma uses

    uint32_t num_selections;
    std::vector<odol_selection> selections;




    void updateBoundingBox();

    vector3 min_pos;
    vector3 max_pos;
    vector3 autocenter_pos;
    float boundingSphere;

    std::optional<std::string> getProperty(std::string_view propName) const;



    std::vector<std::string> textures;
    std::vector<Material> materials;


    uint32_t num_sections;
    std::vector<struct odol_section> sections;



    uint32_t num_proxies;
    std::vector<odol_proxy> proxies;

    std::vector<uint32_t> point_to_vertex;
    std::vector<uint32_t> vertex_to_point;
};

class odol_lod {
public:

    uint32_t add_point(const mlod_lod &mlod_lod, const model_info &model_info,
        uint32_t point_index_mlod, vector3 normal, struct uv_pair *uv_coords, Logger& logger);

    void writeTo(std::ostream& output);

    uint32_t num_bones_subskeleton;
    std::vector<uint32_t> subskeleton_to_skeleton;
    uint32_t num_bones_skeleton;
    std::vector<odol_bonelink> skeleton_to_subskeleton;
    uint32_t num_points;
    uint32_t num_points_mlod;
    float face_area;
    uint32_t clip_flags[2];
    vector3 min_pos;
    vector3 max_pos;
    vector3 autocenter_pos;
    float sphere;
    uint32_t num_textures;
    std::string textures;
    uint32_t num_materials;
    std::vector<Material> materials;
    std::vector<uint32_t> point_to_vertex;
    std::vector<uint32_t> vertex_to_point;

    uint32_t num_faces;
    uint32_t face_allocation_size;
    uint16_t always_0;
    std::vector<struct odol_face> faces;

    uint32_t num_selections;
    std::vector<struct odol_selection> selections;
    uint32_t num_properties;
    std::vector<property> properties;
    uint32_t num_frames;
    struct odol_frame *frames;
    uint32_t icon_color;
    uint32_t selected_color;
    uint32_t flags;
    bool vertexboneref_is_simple;
    struct uv_pair uv_scale[4];
    std::vector<struct uv_pair> uv_coords;
    std::vector<vector3> points;
    std::vector<vector3> normals;
    std::vector<struct odol_vertexboneref> vertexboneref;
};

template <typename Type, Type nullVal>
class NullableIntegral {
public:

    NullableIntegral() noexcept : val(nullVal) {}
    NullableIntegral(Type newVal) noexcept : val(newVal) {}
    NullableIntegral& operator=(Type newVal) noexcept { val = newVal; return *this; }

    operator Type() const noexcept { return val; }
    operator Type&() noexcept { return val; }
    bool isDefined() const noexcept { return val != nullVal; }
    bool isNull() const noexcept { return val!=nullVal; }

    Type val;
};


struct lod_indices {
    NullableIntegral<int8_t,-1> memory;
    NullableIntegral<int8_t,-1> geometry;
    NullableIntegral<int8_t,-1> geometry_simple;
    NullableIntegral<int8_t,-1> geometry_physx;
    NullableIntegral<int8_t,-1> geometry_fire;
    NullableIntegral<int8_t,-1> geometry_view;
    NullableIntegral<int8_t,-1> geometry_view_pilot;
    NullableIntegral<int8_t,-1> geometry_view_gunner;
    NullableIntegral<int8_t,-1> geometry_view_commander; //always -1 because it is not used anymore
    NullableIntegral<int8_t,-1> geometry_view_cargo;
    NullableIntegral<int8_t,-1> land_contact;
    NullableIntegral<int8_t,-1> roadway;
    NullableIntegral<int8_t,-1> paths;
    NullableIntegral<int8_t,-1> hitpoints;
};


enum class MapType : uint32_t {
    Tree,
    SmallTree,
    Bush,
    Building,
    House,
    ForestBorder,
    ForestTriangle,
    ForestSquare,
    Church,
    Chapel,
    Cross,
    Rock,
    Bunker,
    Fortress,
    Fountain,
    ViewTower,
    Lighthouse,
    Quay,
    Fuelstation,
    Hospital,
    Fence,
    Wall,
    Hide, //default value
    BusStop,
    Road,
    Forest,
    Transmitter,
    Stack,
    Ruin,
    Tourism,
    Watertower,
    Track,
    MainRoad,
    Rocks,
    PowerLines,
    RailWay,
    PowerSolar,
    PowerWave,
    PowerWind,
    Shipwreck,
    NMapType
};

constexpr std::array<std::pair<MapType, std::string_view>, static_cast<size_t>(MapType::NMapType)> MapTypeToString {
    std::pair<MapType, std::string_view>
  { MapType::Tree, "TREE" },
  { MapType::SmallTree, "SMALL TREE" },
  { MapType::Bush, "BUSH" },
  { MapType::Building, "BUILDING" },
  { MapType::House, "HOUSE" },
  { MapType::ForestBorder, "FOREST BORDER" },
  { MapType::ForestTriangle, "FOREST TRIANGLE" },
  { MapType::ForestSquare, "FOREST SQUARE" },
  { MapType::Church, "CHURCH" },
  { MapType::Chapel, "CHAPEL" },
  { MapType::Cross, "CROSS" },
  { MapType::Rock, "ROCK" },
  { MapType::Bunker, "BUNKER" },
  { MapType::Fortress, "FORTRESS" },
  { MapType::Fountain, "FOUNTAIN" },
  { MapType::ViewTower, "VIEW-TOWER" },
  { MapType::Lighthouse, "LIGHTHOUSE" },
  { MapType::Quay, "QUAY" },
  { MapType::Fuelstation, "FUELSTATION" },
  { MapType::Hospital, "HOSPITAL" },
  { MapType::Fence, "FENCE" },
  { MapType::Wall, "WALL" },
  { MapType::Hide, "HIDE" },
  { MapType::BusStop, "BUSSTOP" },
  { MapType::Road, "ROAD" },
  { MapType::Forest, "FOREST" },
  { MapType::Transmitter, "TRANSMITTER" },
  { MapType::Stack, "STACK" },
  { MapType::Ruin, "RUIN" },
  { MapType::Tourism, "TOURISM" },
  { MapType::Watertower, "WATERTOWER" },
  { MapType::Track, "TRACK" },
  { MapType::MainRoad, "MAIN ROAD" },
  { MapType::Rocks, "ROCKS" },
  { MapType::PowerLines, "POWER LINES" },
  { MapType::RailWay, "RAILWAY" },
  { MapType::PowerSolar, "POWERSOLAR" },
  { MapType::PowerWave, "POWERWAVE" },
  { MapType::PowerWind, "POWERWIND" },
  { MapType::Shipwreck, "SHIPWRECK" }
};



enum ClipFlags : uint32_t {
    
    ClipNone = 0,
    ClipFront = 1,
    ClipBack = 2,
    ClipLeft = 4,
    ClipRight = 8,
    ClipBottom = 16,
    ClipTop = 32,
    ClipUser0 = 0x40,

    ClipAll = ClipFront | ClipBack | ClipLeft | ClipRight | ClipBottom | ClipTop,

    //#TODO remove unused ones

    ClipLandMask = 0xf00,
    ClipLandStep = 0x100,
    ClipLandNone = ClipLandStep * 0,
    ClipLandOn = ClipLandStep * 1,
    ClipLandUnder = ClipLandStep * 2,
    ClipLandAbove = ClipLandStep * 4,
    ClipLandKeep = ClipLandStep * 8,

    ClipDecalMask = 0x3000,
    ClipDecalStep = 0x1000,
    ClipDecalNone = ClipDecalStep * 0,
    ClipDecalNormal = ClipDecalStep * 1,
    ClipDecalVertical = ClipDecalStep * 2,

    ClipFogMask = 0xc000, 
    ClipFogStep = 0x4000,
    ClipFogNormal = ClipFogStep * 0,
    ClipFogDisable = ClipFogStep * 1,
    ClipFogSky = ClipFogStep * 2,

    ClipLightMask = 0xf0000, 
    ClipLightStep = 0x10000,
    ClipLightNormal = ClipLightStep * 0,
    ClipLightLine = ClipLightStep * 8,

    ClipUserMask = 0xff00000, 
    ClipUserStep = 0x100000,
    MaxUserValue = 0xff,

    // all hints mask
    ClipHints = ClipLandMask | ClipDecalMask | ClipFogMask | ClipLightMask | ClipUserMask
};


enum SpecialFlags {
    SunPrecalculated = 1,
    OnSurface = 2,
    IsOnSurface = 4,
    NoZBuf = 8,  //#TODO remove unused ones
    NoZWrite = 0x10,

    NoShadow = 0x20,
    IsShadow = 0x40,
    NoAlphaWrite = 0x80,

    IsAlpha = 0x100,
    IsTransparent = 0x200,
    IsShadowVolume = 0x400,
    IsLight = 0x800,
    DstBlendOne = IsLight,
    ShadowVolumeFrontFaces = 0x1000,
    NoBackfaceCull = ShadowVolumeFrontFaces,
    ClampLog = 14,
    ClampMask = 3,
    NoClamp = 0x2000,
    ClampU = 0x4000,
    ClampV = 0x8000,

    IsAnimated = 0x10000,
    IsAlphaOrdered = 0x20000,
    NoColorWrite = 0x40000,
    IsAlphaFog = 0x80000,
    DstBlendZero = 0x100000,
    IsColored = 0x200000,
    IsHidden = 0x400000,
    BestMipmap = 0x800000,
    FilterMask = 0x3000000,
    FilterTrilinear = 0x0000000,
    FilterLinear = 0x1000000,
    FilterAnizotrop = 0x2000000,
    FilterPoint = 0x3000000,
    ZBiasMask = 0xc000000, ZBiasStep = 0x4000000,
    IsHiddenProxy = 0x10000000,
    NoStencilWrite = 0x20000000,
    TracerLighting = 0x40000000,
    DisableSun = 0x80000000,
};


enum class SBSource : uint32_t {
    Visual,
    ShadowVolume,
    Explicit,
    None
};


struct model_info {

    void writeTo(std::ostream& output);

    std::vector<float> lod_resolutions;
    uint32_t specialFlags; //#TODO rename special flags
    float bounding_sphere;
    float geo_lod_sphere;

    uint32_t remarks { 0 };
    uint32_t andHints { 0 };
    uint32_t orHints { 0 };


    vector3 aiming_center;
    uint32_t map_icon_color;
    uint32_t map_selected_color;
    float view_density;
    vector3 bbox_min;
    vector3 bbox_max;
    float lod_density_coef;
    float draw_importance;
    vector3 bbox_visual_min;
    vector3 bbox_visual_max;
    vector3 bounding_center;
    vector3 geometry_center;
    vector3 centre_of_mass;
    matrix inv_inertia;
    bool autocenter;
    bool lock_autocenter;
    bool can_occlude;
    bool can_be_occluded;
    bool ai_cover { false };
    bool force_not_alpha;
    SBSource sb_source;
    bool prefer_shadow_volume;
    float shadow_offset;
    bool animated { false };
    std::unique_ptr<struct skeleton_> skeleton;
    MapType map_type { MapType::Hide };

    uint32_t n_floats;
    float mass;
    float mass_reciprocal; //#TODO rename invMass
    float armor;
    float inv_armor;
    float explosionShielding;
    struct lod_indices special_lod_indices;
    uint32_t min_shadow;
    bool can_blend;
    std::string class_type;
    std::string destruct_type;
    bool property_frequent { false };
    uint32_t always_0;
    uint32_t numberGraphicalLods;



    NullableIntegral<int8_t,-1> shadowVolume; //lod index. Belongs into special_lod_indices
    NullableIntegral<int8_t,-1> shadowBuffer; //lod index. Belongs into special_lod_indices
    uint32_t shadowVolumeCount { 0 };
    uint32_t shadowBufferCount { 0 };



    std::vector<NullableIntegral<int32_t,-1>> preferredShadowVolumeLod;
    std::vector<NullableIntegral<int32_t,-1>> preferredShadowBufferLod;
    std::vector<NullableIntegral<int32_t,-1>> preferredShadowBufferVisibleLod;


    float ht_min{ 0 };
    float ht_max{ 0 };
    float af_max{ 0 };
    float mf_max{ 0 };
    float mf_act{ 0 };
    float t_body{ 0 };

};

int mlod2odol(const char *source, const char *target, Logger& logger);

class Buoyant;


class MultiLODShape {
public:

    ModelConfig modelConfig;

    std::vector<float> massArray;

    uint32_t num_lods;
    std::vector<mlod_lod> mlod_lods;
    struct model_info model_info;
    std::unique_ptr<Buoyant> buoy;

    //Get's property from GEO or LOD0
    std::optional<std::string> getPropertyGeo(std::string_view propName);
    void updateHints(float viewDensityCoef);



    std::vector<mlod_lod> shadowVolumes;
    std::vector<ComparableFloat<std::milli>> shadowVolumesResolutions;



    bool projectedShadow;

    void scanProjectedShadow();
    void BuildSpecialLodList(); //#TODO rename
    void shapeListUpdated();



};

class Buoyant {
public:
    float volume; //cubic meters
    float basicLeakiness;

    //float resistanceCoef;
    //float linearDampeningCoefX;
    //float linearDampeningCoefY;
    //float angularDampeningCoef;

    vector3 bbMin; //boundingbox
    vector3 bbMax;

    virtual void init(const MultiLODShape& shape) {
        const mlod_lod* ld = nullptr;
        if (shape.model_info.special_lod_indices.geometry_simple.isDefined())
            ld = &shape.mlod_lods[shape.model_info.special_lod_indices.geometry_simple];
        if (!ld && shape.model_info.special_lod_indices.geometry.isDefined())
            ld = &shape.mlod_lods[shape.model_info.special_lod_indices.geometry];
        if (!ld) return;

        bbMin = ld->min_pos;
        bbMax = ld->max_pos;

        bbMin.y = bbMax.y = shape.model_info.centre_of_mass.y; //Dunno why?


    }
    virtual void writeTo(std::ostream& out) const {
        out.write(reinterpret_cast<const char*>(&volume), sizeof(volume));
    }
};


class BuoyantIteration : public Buoyant {
public:

    float stuff(vector3 a, vector3 b, vector3 c) {
        return a.tripleProd(b, c) / 6.0f;
    }

    virtual void writeTo(std::ostream& out) const {
        Buoyant::writeTo(out);
    };

    void init(const MultiLODShape& shape) override {
        Buoyant::init(shape);

        //Get GeometrySimple
        //if not get GeometryPhys
        //if not get Geometry
        const mlod_lod* ld = nullptr;
        if (shape.model_info.special_lod_indices.geometry_simple.isDefined())
            ld = &shape.mlod_lods[shape.model_info.special_lod_indices.geometry_simple];
        if (!ld && shape.model_info.special_lod_indices.geometry_physx.isDefined())
            ld = &shape.mlod_lods[shape.model_info.special_lod_indices.geometry_physx];
        if (!ld && shape.model_info.special_lod_indices.geometry.isDefined())
            ld = &shape.mlod_lods[shape.model_info.special_lod_indices.geometry];
        if (!ld) return;

        float vol = 0;


        for (auto& mface : ld->faces) {
            //Get 3 verticies. If 4 vertex face then first do 0,1,2 and then again do 0,2,3

            if (mface.face_type >= 3) {
                vol += stuff(
                    ld->points[mface.table[0].points_index].pos,
                    ld->points[mface.table[1].points_index].pos,
                    ld->points[mface.table[2].points_index].pos
                    );
            }
            if (mface.face_type == 4) {
                vol += stuff(
                    ld->points[mface.table[0].points_index].pos,
                    ld->points[mface.table[2].points_index].pos,
                    ld->points[mface.table[3].points_index].pos
                    );
            }

            
        }
        volume = vol;

    }
};

struct BuoyantPoint { //#TODO rename
    vector3 coord;
    float sphereRadius;
    float typicalSurface;

    void writeTo(std::ostream& out) const {
        out.write(reinterpret_cast<const char*>(&coord), sizeof(coord));
        out.write(reinterpret_cast<const char*>(&sphereRadius), sizeof(sphereRadius));
        out.write(reinterpret_cast<const char*>(&typicalSurface), sizeof(typicalSurface));
    }

};

class BuoyantSphere : public Buoyant {
public:
    std::vector<BuoyantPoint> _aBuoyancy; //#TODO rename var
    int _arraySizeX;
    int _arraySizeY;
    int _arraySizeZ;

    float _stepX;
    float _stepY;
    float _stepZ;

    float _fullSphereRadius;
    int _minSpheres;
    int _maxSpheres;


    void initBuoyancy(const mlod_lod &geometry, const MultiLODShape &lodShape) {
        vector3 mMin = geometry.min_pos; //#TODO minmax bounding box
        vector3 mMax = geometry.max_pos;
        auto [xSize, ySize, zSize] = (mMax - mMin);

#undef max
        float maxSize = std::max({ xSize, ySize, zSize });
        if (maxSize <= 0) {
            //#TODO warning
            //bounding box in geo level has 0 size. buoyoncany won't work, missing property class or autocenter=0?
            return;
        }
        float invStep = (float)_maxSpheres / maxSize;
#undef max
        int xSegments = std::max(xSize*invStep, (float)_minSpheres);
        int ySegments = std::max(ySize*invStep, (float)_minSpheres);
        int zSegments = std::max(zSize*invStep, (float)_minSpheres);

        _stepX = xSize / xSegments;
        _stepY = ySize / ySegments;
        _stepZ = zSize / zSegments;

        if (_stepX > 0 && _stepY > 0 && _stepZ > 0) {
            _arraySizeX = xSegments;
            _arraySizeY = ySegments;
            _arraySizeZ = zSegments;
        } else {
            _arraySizeX = _arraySizeY = _arraySizeZ = 0;
        }

        // every rectangle is represented by sphere with same capacity for static buoyancy
        //#TODO use other pi
#define H_PI  ( 3.14159265358979323846f )


        _fullSphereRadius = pow((3.0*_stepX*_stepY*_stepZ) / (4.0*H_PI), 1.0 / 3.0); // r = sqrt3( 3/4 * (x*y*z)/pi )

        _aBuoyancy.resize(_arraySizeX*_arraySizeY*_arraySizeZ);

        for (int z = 0; z < _arraySizeZ; z++) {
            float mCoordZ = z * _stepZ + mMin.z + _stepZ * 0.5;
            for (int y = 0; y < _arraySizeY; y++) {
                float mCoordY = y * _stepY + mMin.y + _stepY * 0.5;
                for (int x = 0; x < _arraySizeX; x++) {
                    float mCoordX = x * _stepX + mMin.x + _stepX * 0.5;

                    BuoyantPoint &buoyantPoint = _aBuoyancy[x + y * _arraySizeX + z * _arraySizeX*_arraySizeY];
                    buoyantPoint.sphereRadius = _fullSphereRadius; // default value
                    buoyantPoint.coord = vector3(mCoordX, mCoordY, mCoordZ);
                    buoyantPoint.typicalSurface = 0; // default value
                }
            }
        }
    }

    bool insideX(odol_face& f, const mlod_lod& v, vector3 pos) const {
        if (f.face_type < 3) return false;

        vector3 pPos(0, pos.y, pos.z);
        auto lastVertID = f.table[f.face_type - 1];

        vector3 lPos = v.points[lastVertID].pos;
        bool andIn = true;
        bool orIn = false;
        for (int i = 0; i < f.face_type; i++)
        {
            vector3 aPos = v.points[f.table[i]].pos;

            vector3 lineNormal(0, lPos.z - aPos.z, aPos.y - lPos.y);
            vector3 aPPos(0, aPos.y, aPos.z);

            float checkIn = lineNormal.dot(pPos - aPPos);
            if (checkIn < 0) andIn = false;
            else orIn = true;
            lPos = aPos;
        }
        return andIn == orIn;
    }

    bool insideY(odol_face& f, const mlod_lod& v, vector3 pos) const {
        if (f.face_type < 3) return false;

        vector3 pPos(pos.x, 0, pos.z);
        auto lastVertID = f.table[f.face_type - 1];

        vector3 lPos = v.points[lastVertID].pos;
        bool andIn = true;
        bool orIn = false;
        for (int i = 0; i < f.face_type; i++)
        {
            vector3 aPos = v.points[f.table[i]].pos;

            vector3 lineNormal(lPos.z - aPos.z, 0, aPos.x - lPos.x);
            vector3 aPPos(aPos.x, 0, aPos.z);

            float checkIn = lineNormal.dot(pPos - aPPos);
            if (checkIn < 0) andIn = false;
            else orIn = true;
            lPos = aPos;
        }
        return andIn == orIn;
    }

    bool insideZ(odol_face& f, const mlod_lod& v, vector3 pos) const {
        if (f.face_type < 3) return false;

        vector3 pPos(pos.x, pos.y, 0);
        auto lastVertID = f.table[f.face_type - 1];

        vector3 lPos = v.points[lastVertID].pos;
        bool andIn = true;
        bool orIn = false;
        for (int i = 0; i < f.face_type; i++)
        {
            vector3 aPos = v.points[f.table[i]].pos;

            vector3 lineNormal(aPos.y - lPos.y, lPos.x - aPos.x, 0);
            vector3 aPPos(aPos.x, aPos.y, 0);

            float checkIn = lineNormal.dot(pPos - aPPos);
            if (checkIn < 0) andIn = false;
            else orIn = true;
            lPos = aPos;
        }
        return andIn == orIn;
    }



    void cutX(std::vector<bool>& rays, const mlod_lod& shape) const {
        vector3 mMin = shape.min_pos;
        float stepRayY = _stepY / (float)10;
        float stepRayZ = _stepZ / (float)10;

        for (int y = 0; y < _arraySizeY * 10; y++) {
            float mCoordY = y * stepRayY + mMin.y + stepRayY * 0.5;	// ray Y coord

            for (int z = 0; z < _arraySizeZ * 10; z++) {
                float mCoordZ = z * stepRayZ + mMin.z + stepRayZ * 0.5; // ray Z coord

                vector3 rayX(0, mCoordY, mCoordZ);
                bool isInside = false;

                //for each faces
                for (auto& mface : shape.faces) {
                    odol_face face;
                    face.face_type = mface.face_type;
                    face.table[0] = mface.table[0].points_index;
                    face.table[1] = mface.table[1].points_index;
                    face.table[2] = mface.table[2].points_index;
                    face.table[3] = mface.table[3].points_index;

                    if (insideX(face, shape, rayX)) {
                        isInside = true;
                        break;
                    }
                }

                if (!isInside) {
                    for (int x = 0; x < _arraySizeX * 10; x++) {
                        rays[x + y * _arraySizeX * 10 + z * _arraySizeX * 10 * _arraySizeY * 10] = false;
                    }
                }
            }
        }
    }

    void cutY(std::vector<bool>& rays, const mlod_lod& shape) const {
        vector3 mMin = shape.min_pos;
        float stepRayX = _stepX / (float)10;
        float stepRayZ = _stepZ / (float)10;

        for (int x = 0; x < _arraySizeX * 10; x++) {
            float mCoordX = x * stepRayX + mMin.x + stepRayX * 0.5;	// ray X coord

            for (int z = 0; z < _arraySizeZ * 10; z++) {
                float mCoordZ = z * stepRayZ + mMin.z + stepRayZ * 0.5; // ray Z coord

                vector3 rayY(mCoordX, 0, mCoordZ);
                bool isInside = false;

                for (auto& mface : shape.faces) {
                    odol_face face;
                    face.face_type = mface.face_type;
                    face.table[0] = mface.table[0].points_index;
                    face.table[1] = mface.table[1].points_index;
                    face.table[2] = mface.table[2].points_index;
                    face.table[3] = mface.table[3].points_index;

                    if (insideY(face, shape, rayY)) {
                        isInside = true;
                        break;
                    }
                }

                if (!isInside) {
                    for (int y = 0; y < _arraySizeY * 10; x++) {
                        rays[x + y * _arraySizeX * 10 + z * _arraySizeX * 10 * _arraySizeY * 10] = false;
                    }
                }
            }
        }
    }

    void cutZ(std::vector<bool>& rays, const mlod_lod& shape) const {
        vector3 mMin = shape.min_pos;
        float stepRayX = _stepX / (float)10;
        float stepRayY = _stepY / (float)10;

        for (int x = 0; x < _arraySizeX * 10; x++) {
            float mCoordX = x * stepRayX + mMin.x + stepRayX * 0.5;	// ray X coord

            for (int y = 0; y < _arraySizeY * 10; y++) {
                float mCoordY = y * stepRayY + mMin.y + stepRayY * 0.5; // ray Z coord

                vector3 rayY(mCoordX, mCoordY, 0);
                bool isInside = false;

                for (auto& mface : shape.faces) {
                    odol_face face;
                    face.face_type = mface.face_type;
                    face.table[0] = mface.table[0].points_index;
                    face.table[1] = mface.table[1].points_index;
                    face.table[2] = mface.table[2].points_index;
                    face.table[3] = mface.table[3].points_index;

                    if (insideZ(face, shape, rayY)) {
                        isInside = true;
                        break;
                    }
                }

                if (!isInside) {
                    for (int z = 0; z < _arraySizeY * 10; x++) {
                        rays[x + y * _arraySizeX * 10 + z * _arraySizeX * 10 * _arraySizeY * 10] = false;
                    }
                }
            }
        }
    }

    void fillInsideArea(std::vector<bool>& rays) const {
        //#TODO multithreading
        for (int x = 0; x < _arraySizeX * 10; x++) {
            for (int y = 0; y < _arraySizeY * 10; y++) {
                for (int z = 0; z < _arraySizeZ * 10; z++) {
                    // do not check for points inside geometry
                    if (rays[x + y * _arraySizeX * 10 + z * _arraySizeX * 10 * _arraySizeY * 10]) continue;

                    bool findFilled;

                    //check x axis
                    {
                        //check x- (check if there is some filled point on x- from our position)
                        findFilled = false;
                        for (int chX = x; chX >= 0; chX--) {
                            if (rays[chX + y * _arraySizeX * 10 + z * _arraySizeX * 10 * _arraySizeY * 10]) {
                                findFilled = true;
                                break;
                            }
                        }
                        if (!findFilled) continue;

                        findFilled = false;
                        for (int chX = x; chX < _arraySizeX * 10; chX++) {
                            if (rays[chX + y * _arraySizeX * 10 + z * _arraySizeX * 10 * _arraySizeY * 10]) {
                                findFilled = true;
                                break;
                            }
                        }
                        if (!findFilled) continue;
                    }

                    //check y axis
                    {
                        findFilled = false;
                        for (int chY = y; chY >= 0; chY--) {
                            if (rays[x + chY * _arraySizeX * 10 + z * _arraySizeX * 10 * _arraySizeY * 10]) {
                                findFilled = true;
                                break;
                            }
                        }
                        if (!findFilled) continue;
                    }

                    //check z axis
                    {
                        findFilled = false;
                        for (int chZ = z; chZ >= 0; chZ--) {
                            if (rays[x + y * _arraySizeX * 10 + chZ * _arraySizeX * 10 * _arraySizeY * 10])
                            {
                                findFilled = true;
                                break;
                            }
                        }
                        if (!findFilled) continue;

                        findFilled = false;
                        for (int chZ = z; chZ < _arraySizeZ * 10; chZ++) {
                            if (rays[x + y * _arraySizeX * 10 + chZ * _arraySizeX * 10 * _arraySizeY * 10]) {
                                findFilled = true;
                                break;
                            }
                        }
                        if (!findFilled) continue;
                    }

                    //if all checks returns true, then this point is inside
                    rays[x + y * _arraySizeX * 10 + z * _arraySizeX * 10 * _arraySizeY * 10] = 2;//true; //DBG
                }
            }
        }
    }

    void countCoefs(std::vector<bool>& aBuoyancyRays) {
        volume = 0;

        float invMaxInsidePoints = 1.0f / (float)(10 * 10 * 10);
        float pointSurface = (_stepX*_stepY + _stepX * _stepZ + _stepY * _stepZ) / (10 * 10 * 3.0f);
        // go through the whole rays array and check each sub element
        for (int z = 0; z < _arraySizeZ; z++) {
            int zMin = z * 10 * _arraySizeX * 10 * _arraySizeY * 10;
            int zStep = _arraySizeX * 10 * _arraySizeY * 10;
            int zMax = zMin + zStep * 10;

            for (int y = 0; y < _arraySizeY; y++) {
                int yMin = y * 10 * _arraySizeX * 10;
                int yStep = _arraySizeX * 10;
                int yMax = yMin + yStep * 10;

                for (int x = 0; x < _arraySizeX; x++) {
                    int xMin = x * 10;
                    int xStep = 1;
                    int xMax = xMin + xStep * 10;

                    BuoyantPoint &buoyantPoint = _aBuoyancy[x + y * _arraySizeX + z * _arraySizeX*_arraySizeY];
                    float fillCoef;
                    int borderPoints;

                    {
                        int insidePoints = 0;
                        borderPoints = 0;
                        //#TODO multithreading
                        for (int zR = zMin; zR < zMax; zR += zStep) {
                            for (int yR = yMin; yR < yMax; yR += yStep) {
                                for (int xR = xMin; xR < xMax; xR += xStep) {
                                    if (aBuoyancyRays[xR + yR + zR]) {
                                        insidePoints++;

                                        // if point is at the border of geometry, or point does not have a neighbor => point is at the border
                                        // calculate free neighbors
                                        int freeNeighbors = 0;
                                        if ((x <= 0 && xR <= xMin) || !aBuoyancyRays[xR - xStep + yR + zR]) freeNeighbors++; // x-
                                        if ((y <= 0 && yR <= yMin) || !aBuoyancyRays[xR + yR - yStep + zR]) freeNeighbors++; // y-
                                        if ((z <= 0 && zR <= zMin) || !aBuoyancyRays[xR + yR + zR - zStep]) freeNeighbors++; // z-
                                        if ((x >= _arraySizeX - 1 && xR >= xMax - xStep) || !aBuoyancyRays[xR + xStep + yR + zR]) freeNeighbors++; // x+
                                        if ((y >= _arraySizeY - 1 && yR >= yMax - yStep) || !aBuoyancyRays[xR + yR + yStep + zR]) freeNeighbors++; // y+
                                        if ((z >= _arraySizeZ - 1 && zR >= zMax - zStep) || !aBuoyancyRays[xR + yR + zR + zStep]) freeNeighbors++; // z+

                                        if (freeNeighbors)
                                            borderPoints += freeNeighbors;
                                    }
                                }
                            }
                        }
                        fillCoef = (float)insidePoints*(float)invMaxInsidePoints;
                    }

                    buoyantPoint.sphereRadius = _fullSphereRadius * pow(fillCoef, 1.0f / 3.0f);
                    buoyantPoint.typicalSurface = pointSurface * borderPoints;

                    volume += fillCoef;
                }
            }
        }

        volume *= _stepX * _stepY*_stepZ;
    }



    void writeTo(std::ostream& out) const override {
        out.write(reinterpret_cast<const char*>(&_arraySizeX), sizeof(_arraySizeX));
        out.write(reinterpret_cast<const char*>(&_arraySizeY), sizeof(_arraySizeY));
        out.write(reinterpret_cast<const char*>(&_arraySizeZ), sizeof(_arraySizeZ));
        out.write(reinterpret_cast<const char*>(&_stepX), sizeof(_stepX));
        out.write(reinterpret_cast<const char*>(&_stepY), sizeof(_stepY));
        out.write(reinterpret_cast<const char*>(&_stepZ), sizeof(_stepZ));
        out.write(reinterpret_cast<const char*>(&_fullSphereRadius), sizeof(_fullSphereRadius));
        out.write(reinterpret_cast<const char*>(&_minSpheres), sizeof(_minSpheres));
        out.write(reinterpret_cast<const char*>(&_maxSpheres), sizeof(_maxSpheres));

        for (auto& it : _aBuoyancy)
            it.writeTo(out);

        Buoyant::writeTo(out);
    };




    void init(const MultiLODShape& shape) {
        //Get GeometrySimple
        //if not get GeometryPhys
        //if not get Geometry
        const mlod_lod* ld = nullptr;
        if (shape.model_info.special_lod_indices.geometry_simple.isDefined())
            ld = &shape.mlod_lods[shape.model_info.special_lod_indices.geometry_simple];
        if (!ld && shape.model_info.special_lod_indices.geometry_physx.isDefined())
            ld = &shape.mlod_lods[shape.model_info.special_lod_indices.geometry_physx];
        if (!ld && shape.model_info.special_lod_indices.geometry.isDefined())
            ld = &shape.mlod_lods[shape.model_info.special_lod_indices.geometry];
        if (!ld) return;


        auto minSegs = std::find_if(ld->properties.begin(), ld->properties.end(), [](const property& prop) {
            return prop.name == "minsegments";
        });
        auto maxSegs = std::find_if(ld->properties.begin(), ld->properties.end(), [](const property& prop) {
            return prop.name == "maxsegments";
        });


        if (minSegs != ld->properties.end() && stoi(minSegs->value) >= 1)
            _minSpheres = stoi(minSegs->value);
        else
            _minSpheres = 2;
        if (maxSegs != ld->properties.end() && stoi(maxSegs->value) >= 1)
            _maxSpheres = stoi(maxSegs->value);
        else
            _maxSpheres = 4;

        _minSpheres = std::clamp(_minSpheres, 1, 8);
        _maxSpheres = std::clamp(_maxSpheres, 1, 8);

        if (_minSpheres > 0 && _maxSpheres >= _minSpheres) {

            initBuoyancy(*ld, shape);

            std::vector<bool> rays;

            rays.resize(
                _arraySizeX * 10
                *
                _arraySizeY * 10
                *
                _arraySizeZ * 10
            );
            std::fill(rays.begin(), rays.end(), true);

            cutX(rays, *ld);
            cutY(rays, *ld);
            cutZ(rays, *ld);

            fillInsideArea(rays);
            countCoefs(rays); //rename calcCoefs

        }



    }
};



class P3DFile : public MultiLODShape { //#TODO move that inherit out of here
    
    Logger& logger;

    void finishLOD(mlod_lod& mlodLod_, uint32_t uint32_, float resolution_);
    int read_lods(std::istream &f_source, uint32_t num_lods);
    //#TODO return the boxes with a pair
    void getBoundingBox(vector3 &bbox_min, vector3 &bbox_max, bool visual_only, bool geometry_only);

    void get_mass_data();
    void optimizeLODS();
    void updateBounds();
    void build_model_info();

    void write_animations(std::ostream& output);
    void convert_lod(mlod_lod &mlod_lod, odol_lod &odol_lod);

public:
    P3DFile(Logger& logger) : logger(logger){}

    std::vector<std::string> retrieveDependencies(std::filesystem::path sourceFile);

    int readMLOD(std::filesystem::path sourceFile);
    int writeODOL(std::filesystem::path targetFile);

};
