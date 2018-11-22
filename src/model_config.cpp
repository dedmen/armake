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

#include "filesystem.h"
#include "rapify.h"
#include "utils.h"
#include "derapify.h"
#include "model_config.h"
#include <filesystem>
#include <fstream>
#include <sstream>


int read_animations(ConfigClass& cfg, char *config_path, struct skeleton_ *skeleton) {
    /*
     * Reads the animation subclasses of the given config path into the struct
     * array.
     *
     * Returns 0 on success, -1 if the path doesn't exist and a positive
     * integer on failure.
     */

    //#TODO is it an error if we have more animations than MAXANIMS?

    // Now go through all the animations
    for (auto& [animName, anim] : cfg.getSubclasses()) {
        //Check if a animation with same name already exists, if it does we want to overwrite.
        auto found = std::find_if(skeleton->animations.begin(), skeleton->animations.end(), [&animName](const animation& anim) {
            return anim.name == animName;
        });

        auto newAnimation = (found == skeleton->animations.end()) ?
            skeleton->animations.emplace_back(std::string(animName)) //if we don't have one to overwrite, we add a new one.
            :
            *found; //Overwrite existing one

        newAnimation.name = animName;

        // Read anim type
        auto type = anim->getString({ "type" }); //#TODO does this work?
        if (!type) {
            lwarningf(current_target, -1, "Animation type for %s could not be found.\n", animName.data());
            continue;
        }

        std::transform(type->begin(), type->end(), type->begin(), ::tolower);

        //#TODO anim name to enum func
        if (*type == "rotation") {
            newAnimation.type = AnimationType::ROTATION;
        } else if (*type == "rotationx") {
            newAnimation.type = AnimationType::ROTATION_X;
        } else if (*type == "rotationy") {
            newAnimation.type = AnimationType::ROTATION_Y;
        } else if (*type == "rotationz") {
            newAnimation.type = AnimationType::ROTATION_Z;
        } else if (*type == "translation") {
            newAnimation.type = AnimationType::TRANSLATION;
        } else if (*type == "translationx") {
            newAnimation.type = AnimationType::TRANSLATION_X;
        } else if (*type == "translationy") {
            newAnimation.type = AnimationType::TRANSLATION_Y;
        } else if (*type == "translationz") {
            newAnimation.type = AnimationType::TRANSLATION_Z;
        } else if (*type == "direct") {
            newAnimation.type = AnimationType::DIRECT;
            lwarningf(current_target, -1, "Direct animations aren't supported yet.\n");
            continue;
        } else if (*type == "hide") {
            newAnimation.type = AnimationType::HIDE;
        } else {
            lwarningf(current_target, -1, "Unknown animation type: %s\n", *type);
            continue;
        }

        // Read optional values
        newAnimation.source.clear();
        newAnimation.selection.clear();
        newAnimation.axis.clear();
        newAnimation.begin.clear();
        newAnimation.end.clear();
        newAnimation.min_value = 0.0f;
        newAnimation.max_value = 1.0f;
        newAnimation.min_phase = 0.0f;
        newAnimation.max_phase = 1.0f;
        newAnimation.junk = 953267991;
        newAnimation.always_0 = 0;
        newAnimation.source_address = AnimationSourceAddress::clamp;
        newAnimation.angle0 = 0.0f;
        newAnimation.angle1 = 0.0f;
        newAnimation.offset0 = 0.0f;
        newAnimation.offset1 = 1.0f;
        newAnimation.hide_value = 0.0f;
        newAnimation.unhide_value = -1.0f;

#define ERROR_READING(key) lwarningf(current_target, -1, "Error reading %s for %s.\n", key, animName.data())

#define TRY_GET_STRING(targ, key) auto key = anim->getString({ #key }); if (!key) ERROR_READING(#key); newAnimation.targ = *key;
#define TRY_GET_FLOAT(targ, key) auto key = anim->getFloat({ #key }); if (!key) ERROR_READING(#key); newAnimation.targ = *key;


        TRY_GET_STRING(source, source);
        TRY_GET_STRING(selection, selection);
        TRY_GET_STRING(axis, axis);
        TRY_GET_STRING(begin, begin);
        TRY_GET_STRING(end, end);

        TRY_GET_FLOAT(min_value, minValue);
        TRY_GET_FLOAT(max_value, maxValue);
        TRY_GET_FLOAT(min_phase, minPhase);
        TRY_GET_FLOAT(max_phase, maxPhase);
        TRY_GET_FLOAT(angle0, angle0);
        TRY_GET_FLOAT(angle1, angle1);
        TRY_GET_FLOAT(offset0, offset0);
        TRY_GET_FLOAT(offset1, offset1);
        TRY_GET_FLOAT(hide_value, hideValue);
        TRY_GET_FLOAT(unhide_value, unHideValue);

        auto sourceAddress = anim->getString({ "sourceAddress" });

        if (!sourceAddress) {
            ERROR_READING("sourceAddress");
        } else {
            std::transform(sourceAddress->begin(), sourceAddress->end(), sourceAddress->begin(), ::tolower);

            if (*sourceAddress ==  "clamp") {
                newAnimation.source_address = AnimationSourceAddress::clamp;
            } else if (*sourceAddress == "mirror") {
                newAnimation.source_address = AnimationSourceAddress::mirror;
            } else if (*sourceAddress == "loop") {
                newAnimation.source_address = AnimationSourceAddress::loop;
            } else {
                lwarningf(current_target, -1, "Unknown source address \"%s\" in \"%s\".\n", sourceAddress, newAnimation.name.c_str());
                continue;
            }
        }
    }

    return 0;
}


void sort_bones(const std::vector<bone>& src, std::vector<bone>& tgt, const char *parent) {
    int i;
    int j;
    //#TODO optimize this
    /*
    sorting based on parent
    bone 1
     - bone 2
      - bone 3
     - bone 4
      - bone 5
    bone 7
      -bone 8
    */
    for (i = 0; i < src.size(); i++) {
        if (src[i].name.empty())
            continue;
        if (src[i].parent != parent)
            continue;

        tgt.emplace_back(src[i]);
        sort_bones(src, tgt, src[i].name.c_str());
    }

    if (strlen(parent) > 0)
        return;

    // copy the remaining bones
    for (i = 0; i < src.size(); i++) {
        if (src[i].name.empty())
            break;
        for (j = 0; j < src.size(); j++) {
            if (src[i].parent == tgt[j].name)
                break;
        }
        if (j == src.size()) {
            tgt.emplace_back(src[i]);
        }
    }
}


int read_model_config(const char *path, struct skeleton_ *skeleton) {
    /*
     * Reads the model config information for the given model path. If no
     * model config is found, -1 is returned. 0 is returned on success
     * and a positive integer on failure.
     */

    extern const char *current_target;
    FILE *f;
    int i;
    int success;
    char model_config_path[2048];
    char rapified_path[2048];
    char config_path[2048];
    char model_name[512];
    char buffer[512]; //#TODO use one buffer for all string reads and then save them into std::string
    struct bone *bones_tmp;

    current_target = path;

    // Extract model.cfg path
    strncpy(model_config_path, path, sizeof(model_config_path));
    if (strrchr(model_config_path, PATHSEP) != NULL)
        strcpy(strrchr(model_config_path, PATHSEP) + 1, "model.cfg");
    else
        strcpy(model_config_path, "model.cfg");

    strcpy(rapified_path, model_config_path);
    strcat(rapified_path, ".armake.bin"); // it is assumed that this doesn't exist

    if (!std::filesystem::exists(model_config_path))
        return -1;

    // Rapify file

    Preprocessor p;
    std::stringstream buf;
    p.preprocess(model_config_path, std::ifstream(model_config_path), buf, Preprocessor::ConstantMapType());
    buf.seekg(0);
    auto cfg = Config::fromPreprocessedText(buf, p.getLineref());

    current_target = path;

    // Extract model name and convert to lower case
    if (strrchr(path, PATHSEP) != NULL)
        strcpy(model_name, strrchr(path, PATHSEP) + 1);
    else
        strcpy(model_name, path);
    *strrchr(model_name, '.') = 0;

    lower_case(model_name);

    // Check if model entry even exists
    auto modelConfig = cfg->getClass({ "CfgModels", model_name });
    if (!modelConfig) {
        errorf("Failed to find model config entry.\n");
        __debugbreak();
        return 1; //#TODO fix this error code. That's not the correct one
    }

    if (strchr(model_name, '_') == NULL)
        lnwarningf(path, -1, "model-without-prefix", "Model has a model config entry but doesn't seem to have a prefix (missing _).\n");

    // Read name
    auto skeletonName = cfg->getString({ "CfgModels", model_name , "skeletonName"});
    if (!skeletonName) {
        errorf("Failed to read skeleton name.\n");
        return 1;//#TODO fix this error code. That's not the correct one
    }
    skeleton->name = std::move(*skeletonName);

    // Read bones
    if (!skeleton->name.empty()) {
        auto skeletonInherit = cfg->getString({ "CfgSkeletons", skeleton->name , "skeletonInherit" });
        if (!skeletonInherit) {
            __debugbreak();
            errorf("Failed to read bones.\n"); //#TODO fix this wrong error message
            return success;//#TODO fix this error code. That's not the correct one
        }

        auto isDiscrete = cfg->getInt({ "CfgSkeletons", skeleton->name, "isDiscrete" });
        if (isDiscrete)
            skeleton->is_discrete = (*isDiscrete > 0);
        else
            skeleton->is_discrete = false;

        if (skeletonInherit->length() > 1) { // @todo: more than 1 parent
            auto skeletonBones = cfg->getArrayOfStrings({ "CfgSkeletons", *skeletonInherit , "skeletonBones" });
            if (!skeletonBones) { //#TODO differentitate between empty and not found
                __debugbreak();
                errorf("Failed to read bones.\n");
                return success;//#TODO fix this error code. That's not the correct one
            }
            for (i = 0; i < skeletonBones->size(); i += 2) {
                skeleton->bones.emplace_back((*skeletonBones)[i], (*skeletonBones)[i + 1]);
                skeleton->num_bones++;
            }
        }

        auto skeletonBones = cfg->getArrayOfStrings({ "CfgSkeletons", skeleton->name , "skeletonBones" });
        if (!skeletonBones) { //#TODO differentitate between empty and not found
            errorf("Failed to read bones.\n");
            return success;//#TODO fix this error code. That's not the correct one
        }

        for (i = 0; i < skeletonBones->size(); i += 2) {
            skeleton->bones.emplace_back((*skeletonBones)[i], (*skeletonBones)[i + 1]);
            skeleton->num_bones++;
        }

        // Sort bones by parent
        std::vector<bone> sortedBones;
        sort_bones(skeleton->bones, sortedBones, "");
        skeleton->bones = sortedBones;

        // Convert to lower case
        for (auto& bone : skeleton->bones) {
            std::transform(bone.name.begin(), bone.name.end(), bone.name.begin(), tolower);
            std::transform(bone.parent.begin(), bone.parent.end(), bone.parent.begin(), tolower);
        }

        if (skeleton->bones.size() > MAXBONES) { //https://discordapp.com/channels/105462288051380224/105462541215358976/515129500124839956
            errorf("max bone limit reached! %u out of 255 bones being used.", skeleton->bones.size());
        }
    }

    // Read sections
    auto sectionsInherit = cfg->getString({ "CfgModels", model_name, "sectionsInherit" });
    if (!sectionsInherit) {
        errorf("Failed to read sections.\n");
        return success;
    }

    if (sectionsInherit->length() > 0) {
        auto sections = cfg->getArrayOfStrings({ "CfgModels", *sectionsInherit, "sections" });
        if (!sections) { //#TODO differentiate between empty and not found
            errorf("Failed to read sections.\n");
            return success;
        }
        for (auto&& it : *sections)
            if (!it.empty())
                skeleton->sections.emplace_back(it);
    }

    auto sections = cfg->getArrayOfStrings({ "CfgModels", model_name, "sections" });
    if (!sections) {
        errorf("Failed to read sections.\n");
        return success;
    }

    for (auto&& it : *sections)
        if (!it.empty())
            skeleton->sections.emplace_back(it);

    skeleton->num_sections = skeleton->sections.size();

    // Read animations
    skeleton->num_animations = 0;
    auto animations = cfg->getClass({ "CfgModels", model_name, "Animations" });
    if (animations) {
        //#TODO inheritance?
        success = read_animations(*animations, config_path, skeleton);
        if (success > 0) {
            errorf("Failed to read animations.\n");
            return success;
        }
    }


    if (skeleton->name.empty() && skeleton->num_animations > 0)
        lwarningf(path, -1, "animated-without-skeleton", "Model doesn't have a skeleton but is animated.\n");

    // Read thermal stuff
    skeleton->ht_min = *cfg->getFloat({ "CfgModels",model_name, "htMin" }); //#TODO error checking. Should use exceptions
    skeleton->ht_max = *cfg->getFloat({ "CfgModels",model_name, "htMax" }); //#TODO error checking. Should use exceptions
    skeleton->af_max = *cfg->getFloat({ "CfgModels",model_name, "afMax" }); //#TODO error checking. Should use exceptions
    skeleton->mf_max = *cfg->getFloat({ "CfgModels",model_name, "mfMax" }); //#TODO error checking. Should use exceptions
    skeleton->mf_act = *cfg->getFloat({ "CfgModels",model_name, "mfAct" }); //#TODO error checking. Should use exceptions
    skeleton->t_body = *cfg->getFloat({ "CfgModels",model_name, "tBody" }); //#TODO error checking. Should use exceptions

    return 0;
}
