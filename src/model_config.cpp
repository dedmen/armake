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


int read_animations(FILE *f, char *config_path, struct skeleton_ *skeleton) {
    /*
     * Reads the animation subclasses of the given config path into the struct
     * array.
     *
     * Returns 0 on success, -1 if the path doesn't exist and a positive
     * integer on failure.
     */

    int i;
    int j;
    int k;
    int success;
    char parent[2048];
    char containing[2048];
    char value_path[2048];
    char value[2048];

    // Run the function for the parent class first
    fseek(f, 16, SEEK_SET);
    success = seek_config_path(f, config_path);
    if (success > 0) {
        return success;
    } else if (success == 0) {
        success = find_parent(f, config_path, parent, sizeof(parent));
        if (success > 0) {
            return 2;
        } else if (success == 0) {
            success = read_animations(f, parent, skeleton);
            if (success > 0)
                return success;
        }
    }

    // Check parent CfgModels entry
    strcpy(containing, config_path);
    *(strrchr(containing, '>') - 2) = 0;
    success = find_parent(f, containing, parent, sizeof(parent));
    if (success > 0) {
        return 2;
    } else if (success == 0) {
        strcat(parent, " >> Animations");
        success = read_animations(f, parent, skeleton);
        if (success > 0)
            return success;
    }

    fseek(f, 16, SEEK_SET);
    success = seek_config_path(f, config_path);
    if (success < 0)
        return -1;

    // Now go through all the animations
    std::vector<std::string> anim_names;

    success = read_classes(f, config_path, anim_names, 512);
    if (success)
        return success;
    //#TODO is it an error if we have more animations than MAXANIMS?

    for (auto& animName : anim_names) {
        //Check if a animation with same name already exists, if it does we want to overwrite.
        auto found = std::find_if(skeleton->animations.begin(), skeleton->animations.end(), [&animName](const animation& anim) {
            return anim.name == animName;
        });

        auto newAnimation = (found == skeleton->animations.end()) ?
            skeleton->animations.emplace_back(animName) //if we don't have one to overwrite, we add a new one.
            :
            *found; //Overwrite existing one

        newAnimation.name = animName;

        // Read anim type
        sprintf(value_path, "%s >> %s >> type", config_path, animName.c_str());
        if (read_string(f, value_path, value, sizeof(value))) {
            lwarningf(current_target, -1, "Animation type for %s could not be found.\n", animName.c_str());
            continue;
        }

        lower_case(value);

        if (strcmp(value, "rotation") == 0) {
            newAnimation.type = AnimationType::ROTATION;
        } else if (strcmp(value, "rotationx") == 0) {
            newAnimation.type = AnimationType::ROTATION_X;
        } else if (strcmp(value, "rotationy") == 0) {
            newAnimation.type = AnimationType::ROTATION_Y;
        } else if (strcmp(value, "rotationz") == 0) {
            newAnimation.type = AnimationType::ROTATION_Z;
        } else if (strcmp(value, "translation") == 0) {
            newAnimation.type = AnimationType::TRANSLATION;
        } else if (strcmp(value, "translationx") == 0) {
            newAnimation.type = AnimationType::TRANSLATION_X;
        } else if (strcmp(value, "translationy") == 0) {
            newAnimation.type = AnimationType::TRANSLATION_Y;
        } else if (strcmp(value, "translationz") == 0) {
            newAnimation.type = AnimationType::TRANSLATION_Z;
        } else if (strcmp(value, "direct") == 0) {
            newAnimation.type = AnimationType::DIRECT;
            lwarningf(current_target, -1, "Direct animations aren't supported yet.\n");
            continue;
        } else if (strcmp(value, "hide") == 0) {
            newAnimation.type = AnimationType::HIDE;
        } else {
            lwarningf(current_target, -1, "Unknown animation type: %s\n", value);
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

#define ERROR_READING(key) lwarningf(current_target, -1, "Error reading %s for %s.\n", key, anim_names[i]);
        char buffer[512];
        sprintf(value_path, "%s >> %s >> source", config_path, anim_names[i]);
        if (read_string(f, value_path, buffer, sizeof(buffer)) > 0)
            ERROR_READING("source")
        newAnimation.source = buffer;

        sprintf(value_path, "%s >> %s >> selection", config_path, anim_names[i]);
        if (read_string(f, value_path, buffer, sizeof(buffer)) > 0)
            ERROR_READING("selection")
        newAnimation.selection = buffer;

        sprintf(value_path, "%s >> %s >> axis", config_path, anim_names[i]);
        if (read_string(f, value_path, buffer, sizeof(buffer)) > 0)
            ERROR_READING("axis")
        newAnimation.axis = buffer;

        sprintf(value_path, "%s >> %s >> begin", config_path, anim_names[i]);
        if (read_string(f, value_path, buffer, sizeof(buffer)) > 0)
            ERROR_READING("begin")
        newAnimation.begin = buffer;

        sprintf(value_path, "%s >> %s >> end", config_path, anim_names[i]);
        if (read_string(f, value_path, buffer, sizeof(buffer)) > 0)
            ERROR_READING("end")
        newAnimation.end = buffer;

        sprintf(value_path, "%s >> %s >> minValue", config_path, anim_names[i]);
        if (read_float(f, value_path, &newAnimation.min_value) > 0)
            ERROR_READING("minValue")

        sprintf(value_path, "%s >> %s >> maxValue", config_path, anim_names[i]);
        if (read_float(f, value_path, &newAnimation.max_value) > 0)
            ERROR_READING("maxValue")

        sprintf(value_path, "%s >> %s >> minPhase", config_path, anim_names[i]);
        if (read_float(f, value_path, &newAnimation.min_phase) > 0)
            ERROR_READING("minPhase")

        sprintf(value_path, "%s >> %s >> maxPhase", config_path, anim_names[i]);
        if (read_float(f, value_path, &newAnimation.max_phase) > 0)
            ERROR_READING("maxPhase")

        sprintf(value_path, "%s >> %s >> angle0", config_path, anim_names[i]);
        if (read_float(f, value_path, &newAnimation.angle0) > 0)
            ERROR_READING("angle0")

        sprintf(value_path, "%s >> %s >> angle1", config_path, anim_names[i]);
        if (read_float(f, value_path, &newAnimation.angle1) > 0)
            ERROR_READING("angle1")

        sprintf(value_path, "%s >> %s >> offset0", config_path, anim_names[i]);
        if (read_float(f, value_path, &newAnimation.offset0) > 0)
            ERROR_READING("offset0")

        sprintf(value_path, "%s >> %s >> offset1", config_path, anim_names[i]);
        if (read_float(f, value_path, &newAnimation.offset1) > 0)
            ERROR_READING("offset1")

        sprintf(value_path, "%s >> %s >> hideValue", config_path, anim_names[i]);
        if (read_float(f, value_path, &newAnimation.hide_value) > 0)
            ERROR_READING("hideValue")

        sprintf(value_path, "%s >> %s >> unHideValue", config_path, anim_names[i]);
        if (read_float(f, value_path, &newAnimation.unhide_value) > 0)
            ERROR_READING("unHideValue")

        sprintf(value_path, "%s >> %s >> sourceAddress", config_path, anim_names[i]);
        success = read_string(f, value_path, value, sizeof(value));
        if (success > 0) {
            ERROR_READING("sourceAddress")
        } else if (success == 0) {
            lower_case(value);

            if (strcmp(value, "clamp") == 0) {
                newAnimation.source_address = AnimationSourceAddress::clamp;
            } else if (strcmp(value, "mirror") == 0) {
                newAnimation.source_address = AnimationSourceAddress::mirror;
            } else if (strcmp(value, "loop") == 0) {
                newAnimation.source_address = AnimationSourceAddress::loop;
            } else {
                lwarningf(current_target, -1, "Unknown source address \"%s\" in \"%s\".\n", value, newAnimation.name.c_str());
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


int read_model_config(char *path, struct skeleton_ *skeleton) {
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
    std::vector<std::string> bones;
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
    success = Rapifier::rapify_file(model_config_path, rapified_path);
    if (success) {
        errorf("Failed to rapify model config.\n");
        return 1;
    }

    current_target = path;

    // Extract model name and convert to lower case
    if (strrchr(path, PATHSEP) != NULL)
        strcpy(model_name, strrchr(path, PATHSEP) + 1);
    else
        strcpy(model_name, path);
    *strrchr(model_name, '.') = 0;

    lower_case(model_name);

    // Open rapified file
    f = fopen(rapified_path, "rb");
    if (!f) {
        errorf("Failed to open model config.\n");
        return 2;
    }

    // Check if model entry even exists
    sprintf(config_path, "CfgModels >> %s", model_name);
    fseek(f, 16, SEEK_SET);
    success = seek_config_path(f, config_path);
    if (success > 0) {
        errorf("Failed to find model config entry.\n");
        return success;
    } else if (success < 0) {
        goto clean_up;
    }

    if (strchr(model_name, '_') == NULL)
        lnwarningf(path, -1, "model-without-prefix", "Model has a model config entry but doesn't seem to have a prefix (missing _).\n");

    // Read name
    sprintf(config_path, "CfgModels >> %s >> skeletonName", model_name);
    success = read_string(f, config_path, buffer, sizeof(buffer));
    if (success > 0) {
        errorf("Failed to read skeleton name.\n");
        return success;
    }
    skeleton->name = buffer;

    // Read bones
    if (!skeleton->name.empty()) {
        sprintf(config_path, "CfgSkeletons >> %s >> skeletonInherit", skeleton->name.c_str());
        success = read_string(f, config_path, buffer, sizeof(buffer));
        if (success > 0) {
            errorf("Failed to read bones.\n");
            return success;
        }

        int32_t temp;
        sprintf(config_path, "CfgSkeletons >> %s >> isDiscrete", skeleton->name.c_str());
        success = read_int(f, config_path, &temp);
        if (success == 0)
            skeleton->is_discrete = (temp > 0);
        else
            skeleton->is_discrete = false;

        if (strlen(buffer) > 0) { // @todo: more than 1 parent
            sprintf(config_path, "CfgSkeletons >> %s >> skeletonBones", buffer);
            success = read_string_array(f, config_path,bones, 512);
            if (success > 0) {
                errorf("Failed to read bones.\n");
                return success;
            } else if (success == 0) {
            }
        }

        sprintf(config_path, "CfgSkeletons >> %s >> skeletonBones", skeleton->name.c_str());
        success = read_string_array(f, config_path, bones, 512);
        if (success > 0) {
            errorf("Failed to read bones.\n");
            return success;
        }

        for (i = 0; i < bones.size(); i += 2) {
            skeleton->bones.emplace_back(bones[i], bones[i + 1]);
            skeleton->num_bones++;
        }
        bones.clear();

        // Sort bones by parent
        std::vector<bone> sortedBones;
        sort_bones(skeleton->bones, sortedBones, "");
        skeleton->bones = sortedBones;

        // Convert to lower case
        for (auto& bone : skeleton->bones) {
            std::transform(bone.name.begin(), bone.name.end(), bone.name.begin(), tolower);
            std::transform(bone.parent.begin(), bone.parent.end(), bone.parent.begin(), tolower);
        }
    }

    // Read sections
    sprintf(config_path, "CfgModels >> %s >> sectionsInherit", model_name);
    success = read_string(f, config_path, buffer, sizeof(buffer));
    if (success > 0) {
        errorf("Failed to read sections.\n");
        return success;
    }

    if (strlen(buffer) > 0) {
        sprintf(config_path, "CfgModels >> %s >> sections", buffer);
        success = read_string_array(f, config_path, skeleton->sections, 512);
        if (success > 0) {
            errorf("Failed to read sections.\n");
            return success;
        }
    }

    sprintf(config_path, "CfgModels >> %s >> sections", model_name);
    success = read_string_array(f, config_path, skeleton->sections, 512);
    if (success > 0) {
        errorf("Failed to read sections.\n");
        return success;
    }

    skeleton->num_sections = skeleton->sections.size();

    // Read animations
    skeleton->num_animations = 0;
    sprintf(config_path, "CfgModels >> %s >> Animations", model_name);
    success = read_animations(f, config_path, skeleton);
    if (success > 0) {
        errorf("Failed to read animations.\n");
        return success;
    }

    if (skeleton->name.empty() && skeleton->num_animations > 0)
        lwarningf(path, -1, "animated-without-skeleton", "Model doesn't have a skeleton but is animated.\n");

    // Read thermal stuff
    sprintf(config_path, "CfgModels >> %s >> htMin", model_name);
    read_float(f, config_path, &skeleton->ht_min);
    sprintf(config_path, "CfgModels >> %s >> htMax", model_name);
    read_float(f, config_path, &skeleton->ht_max);
    sprintf(config_path, "CfgModels >> %s >> afMax", model_name);
    read_float(f, config_path, &skeleton->af_max);
    sprintf(config_path, "CfgModels >> %s >> mfMax", model_name);
    read_float(f, config_path, &skeleton->mf_max);
    sprintf(config_path, "CfgModels >> %s >> mfAct", model_name);
    read_float(f, config_path, &skeleton->mf_act);
    sprintf(config_path, "CfgModels >> %s >> tBody", model_name);
    read_float(f, config_path, &skeleton->t_body);

clean_up:
    // Clean up
    fclose(f);
    if (remove_file(rapified_path)) {
        errorf("Failed to remove temporary model config.\n");
        return 3;
    }

    return 0;
}
