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
////#include <unistd.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif

#include "filesystem.h"
#include "utils.h"
#include "preprocess.h"
#include "rapify.h"
#include "derapify.h"
#include <sstream>
#include <iostream>
#include <fstream>
#include <iterator>
#include <functional>
#include <algorithm>
#include <array>
//#include "rapify.tab.h"

class Logger;
__itt_domain* configDomain = __itt_domain_create("armake.config");

std::string_view ConfigClassEntry::getName() const {
    if (isClass())
        return getAsClass()->getName();
    if (isEntry())
        return getAsEntry().getName();
    return std::get<2>(value);
}


std::shared_ptr<ConfigClass> ConfigClass::findInheritedParent(std::string_view  parentName, bool skipEntries, bool walkTree) {

    if (!skipEntries) {
        auto found = order.find(parentName);
        if (found != order.end() && found->second->isClass()) //Do my subclasses contain the wanted class?
            return found->second->getAsClass();
    }

    if (inheritedParent.index() == 1) {
        auto& parentName = std::get<std::string>(inheritedParent);
        auto inhPar = treeParent.lock()->findInheritedParent(parentName, iequals(parentName, getName()));
        if (!inhPar) __debugbreak();
        inheritedParent = inhPar;
    }

    if (inheritedParent.index() == 2) {
        auto val = std::get<2>(inheritedParent).lock()->findInheritedParent(parentName, false, false);
        if (val) return val;
    }

    if (treeParent.lock() && walkTree)
        return treeParent.lock()->findInheritedParent(parentName);
    return nullptr;
}



__itt_string_handle* handle_buildParentTreeTP = __itt_string_handle_create("ConfigClass::buildParentTreeTP");
__itt_string_handle* handle_buildParentTree = __itt_string_handle_create("ConfigClass::buildParentTree");
__itt_string_handle* handle_buildParentTreeRec = __itt_string_handle_create("ConfigClass::buildParentTreeRec");

void ConfigClass::buildParentTree() {

    std::function<void(std::shared_ptr<ConfigClass>)> setTreeParents = [&](std::shared_ptr<ConfigClass> cls) {
        //Multithreading hurts here
        std::for_each(std::execution::seq, cls->entries.begin(), cls->entries.end(), [&](const ConfigClassEntry& it) {
            if (!it.isClass()) return;
            auto& c = it.getAsClass();
            c->treeParent = cls;
            setTreeParents(c);
        });
    };

    if (!hasParentTreeBeenBuilt()) {//only do once on root node
        __itt_task_begin(configDomain, __itt_null, __itt_null, handle_buildParentTreeTP);
        setTreeParents(shared_from_this());
        __itt_task_end(configDomain);
    }

    __itt_task_begin(configDomain, __itt_null, __itt_null, handle_buildParentTree);
    std::for_each(std::execution::seq, entries.begin(), entries.end(), [this](const ConfigClassEntry& it) {
        if (!it.isClass()) return;
        auto& c = it.getAsClass();
        if (c->inheritedParent.index() == 0) return; //doesn't have parent
        if (c->inheritedParent.index() == 2) return; //already resolved
        auto& parentName = std::get<std::string>(c->inheritedParent);

        if (auto par = c->treeParent.lock()->findInheritedParent(parentName, iequals(parentName,c->getName()))) {
            c->inheritedParent = par;
        } else
            __debugbreak();
    });
    __itt_task_end(configDomain);

    __itt_task_begin(configDomain, __itt_null, __itt_null, handle_buildParentTreeRec);
    std::for_each(std::execution::seq, entries.begin(), entries.end(), [](const ConfigClassEntry& it) {
        if (!it.isClass()) return;
        it.getAsClass()->buildParentTree();
    });
    __itt_task_end(configDomain);
}

std::vector<std::shared_ptr<ConfigClass>> ConfigClass::getParents() const {
    auto parName = inheritedParent;
    std::vector<std::shared_ptr<ConfigClass>> ret;

    std::shared_ptr<const ConfigClass> curScope = shared_from_this();

    //While variant is not empty value
    while (curScope->inheritedParent.index() != 0) {
        if (parName.index() == 2) {
            auto parent = std::get<std::weak_ptr<ConfigClass>>(parName).lock();
            ret.emplace_back(parent);
            curScope = std::move(parent);
        }
        else
            __debugbreak();
    }
    return ret;
}

std::map<std::string_view, std::reference_wrapper<const ConfigEntry>> ConfigClass::getEntries() const {
    std::map<std::string_view, std::reference_wrapper<const ConfigEntry>> ret;

    for (auto& elem : entries) {
        if (elem.isEntry())
            ret.insert_or_assign(elem.getAsEntry().getName(), elem.getAsEntry());
    }

    for (auto& it : getParents()) {
        for (auto& elem : it->entries) {
            if (elem.isEntry() && ret.lower_bound(elem.getAsEntry().getName()) == ret.end())
                ret.insert_or_assign(elem.getAsEntry().getName(), elem.getAsEntry());
        }
    }

    return ret;
}

std::map<std::string_view, std::shared_ptr<ConfigClass>> ConfigClass::getSubclasses() const {
    std::map<std::string_view, std::shared_ptr<ConfigClass>> ret;


    //Yes, we are constantly overwriting the entries that are replaced by subclasses
    //But the output needs to be sorted such that the topmost class's entries come first
    for (auto& it : getParents()) {
        for (auto& elem : it->entries) {
            if (elem.isClass() && !elem.getAsClass()->isDelete())
                ret.insert_or_assign(elem.getAsClass()->getName(), elem.getAsClass());
        }
    }

    for (auto& elem : entries) {
        if (elem.isClass() && !elem.getAsClass()->isDelete())
            ret.insert_or_assign(elem.getAsClass()->getName(), elem.getAsClass());
    }


    return ret;
}

std::optional<ConfigClassEntry> ConfigClass::getEntry(ConfigPath path) const {
    const std::string_view subelement = *path.begin();

    auto found = order.find(subelement);

    if (found == order.end()) { //We don't have that value, maybe a parent has?
        if (inheritedParent.index() == 2) {
            auto subresult = std::get<2>(inheritedParent).lock()->getEntry(path);
            if (subresult)
                return subresult;
        }
        return {}; //Nope, no parent has it
    }

    if (path.size() == 1 && found != order.end())  //we are at end of path and have found the value
        return *found->second; //Found it!

    //We are not at end of path, so we must descent into the Tree

    if (found->second->isEntry()) return {}; //The value we found is not a class, we cannot descent into it

    return found->second->getAsClass()->getEntry(std::initializer_list<std::string_view>(path.begin() + 1, path.end()));
}

std::shared_ptr<ConfigClass> ConfigClass::getClass(ConfigPath path) const {
    auto entry = getEntry(path);
    if (!entry) return {};
    auto& def = *entry;

    if (entry->isClass())
        return entry->getAsClass();
    return {};
}

std::optional<int32_t> ConfigClass::getInt(ConfigPath path) const {
    auto entry = getEntry(path);
    if (!entry) return {};
    auto& def = *entry;
    if (!entry->isEntry()) return {}; //entry is not a value

    auto& var = entry->getAsEntry();
    if (var.getType() == rap_type::rap_int || var.getType() == rap_type::rap_float)
        return var.getValue().getAsInt();  //getAsInt automatically converts float to int

    return {};
}

std::optional<float> ConfigClass::getFloat(ConfigPath path) const {
    auto entry = getEntry(path);
    if (!entry) return {};
    auto& def = *entry;
    if (!entry->isEntry()) return {}; //entry is not a value

    auto& var = entry->getAsEntry();
    if (var.getType() == rap_type::rap_int || var.getType() == rap_type::rap_float)
        return var.getValue().getAsFloat();  //getAsFloat automatically converts int to float

    return {};
}

std::optional<std::string> ConfigClass::getString(ConfigPath path) const {
    auto entry = getEntry(path);
    if (!entry) return {};
    auto& def = *entry;
    if (!entry->isEntry()) return {}; //entry is not a value

    auto& var = entry->getAsEntry();
    if (var.getType() == rap_type::rap_string)
        return var.getValue().getAsString();

    return {};
}

std::optional<std::vector<std::string>> ConfigClass::getArrayOfStrings(ConfigPath path) const {
    auto entry = getEntry(path);
    if (!entry) return {};
    auto& def = *entry;
    if (!entry->isEntry()) return {}; //entry is not a value

    auto& var = entry->getAsEntry();
    if (var.getType() == rap_type::rap_array) {
        auto& arr = var.getValue().getAsArray();
        std::vector<std::string> ret;
        for (auto& it : arr) {
            if (it.getType() != rap_type::rap_string) continue;
            ret.push_back(it.getAsString());
        }
        return ret;
    }

    return {};
}

std::optional<std::vector<std::string_view>> ConfigClass::getArrayOfStringViews(ConfigPath path) const {
    auto entry = getEntry(path);
    if (!entry) return {};
    auto& def = *entry;
    if (!entry->isEntry()) return {}; //entry is not a value

    auto& var = entry->getAsEntry();
    if (var.getType() == rap_type::rap_array) {
        auto& arr = var.getValue().getAsArray();
        std::vector<std::string_view> ret;
        for (auto& it : arr) {
            if (it.getType() != rap_type::rap_string) continue;
            ret.push_back(it.getAsString());
        }
        return ret;
    }

    return {};
}

std::vector<float> ConfigClass::getArrayOfFloats(ConfigPath path) const {
    auto entry = getEntry(path);
    if (!entry) return {};
    auto& def = *entry;
    if (!entry->isEntry()) return {}; //entry is not a value


    auto& var = entry->getAsEntry();
    if (var.getType() == rap_type::rap_array) {
        auto& arr = var.getValue().getAsArray();
        std::vector<float> ret;
        for (auto& it : arr) {
            if (it.getType() != rap_type::rap_float && it.getType() != rap_type::rap_int) continue;
            ret.push_back(it.getAsFloat()); //getAsFloat automatically converts int to float
        }
        return ret;
    }

    return {};
}


bool parse_file(std::istream& f, struct lineref &lineref, Logger& logger, ConfigClass &result);


__itt_string_handle* handle_fromRawText = __itt_string_handle_create("Config::fromRawText");
Config Config::fromRawText(std::istream& input, Logger& logger, bool buildParentTree) {
    __itt_task_begin(configDomain, __itt_null, __itt_null, handle_fromRawText);
    Config output;
    output.config = std::make_shared<ConfigClass>();
    input.seekg(0);
    lineref ref;
    ref.empty = true;

    auto result = parse_file(input, ref, logger, *output.config);

    if (!result) {
        logger.error("Failed to parse config.\n");
        __itt_task_end(configDomain);
        return {};
    }
    __itt_task_end(configDomain);
    if (buildParentTree) output.config->buildParentTree();
    return output;
}

__itt_string_handle* handle_fromPreprocessedText = __itt_string_handle_create("Config::fromPreprocessedText");
Config Config::fromPreprocessedText(std::istream &input, lineref& lineref, Logger& logger, bool buildParentTree) {
    __itt_task_begin(configDomain, __itt_null, __itt_null, handle_fromPreprocessedText);
    Config output;
    output.config = std::make_shared<ConfigClass>();
    input.seekg(0);
    auto result = parse_file(input, lineref, logger, *output.config);

    if (!result) {
        logger.error("Failed to parse config.\n");
        __itt_task_end(configDomain);
        return {};
    }
    __itt_task_end(configDomain);
    if (buildParentTree) output.config->buildParentTree();
    return output;
}

__itt_string_handle* handle_fromBinarized = __itt_string_handle_create("Config::fromBinarized");
Config Config::fromBinarized(std::istream & input, Logger& logger, bool buildParentTree) {
    if (!Rapifier::isRapified(input)) {
        logger.error("Source file is not a rapified config.\n");
        return {};
    }
    __itt_task_begin(configDomain, __itt_null, __itt_null, handle_fromBinarized);
    Config output;
    output.config = std::make_shared<ConfigClass>();
    input.seekg(0);

    try {
        Rapifier::derapify_class(input, *output.config, 0);
    } catch( Rapifier::DerapifyException& ex) {
        
        std::stringstream buf;
        buf << "Exception occured in Config::fromBinarized: \n" << ex.what();
        if (ex.getType() != 255)
            buf << " " << static_cast<unsigned>(ex.getType());
        if (!ex.getStack().empty()) {
            buf << "\nTrace: ";
            auto& stack = ex.getStack();
            std::reverse(stack.begin(), stack.end());
            for (auto& it : stack) {
                buf << "/" << it;
            }
        }

        buf << "\nFile Offset: " << input.tellg();

        logger.error(buf.str());
        return {};
    }

    __itt_task_end(configDomain);
    if (buildParentTree) output.config->buildParentTree();
    return output;
}

__itt_string_handle* handle_toBinarized = __itt_string_handle_create("Config::toBinarized");
void Config::toBinarized(std::ostream& output) {
    if (!hasConfig()) return;
    __itt_task_begin(configDomain, __itt_null, __itt_null, handle_toBinarized);
    Rapifier::rapify(*getConfig(), output);
    __itt_task_end(configDomain);
}

void Config::toPlainText(std::ostream& output, Logger& logger, std::string_view indent) {

    uint8_t indentLevel = 0;
    if(!hasConfig()) return;

    auto pushIndent = [&]() {
        for (int i = 0; i < indentLevel; ++i) {
            output.write(indent.data(), indent.length());
        }
    };

    std::function<void(const std::vector<ConfigValue>&)> printArray = [&](const std::vector<ConfigValue>& data) {
        if (data.empty()) {
            output << "{}";
            return;
        }

        output << "{";
        for (auto& it : data) {
            switch (it.getType()) {
            case rap_type::rap_string:
                output << "\"" << it.getAsString() << "\", ";
                break;
            case rap_type::rap_int:
                output << it.getAsInt() << ", ";
                break;
            case rap_type::rap_float:
                output << it.getAsFloat() << ", ";
                break;
            case rap_type::rap_array:
                printArray(it.getAsArray());
                output << ", ";
                break;
            default:
                logger.error("Unknown array element type %i.\n", static_cast<int>(it.getType()));
            }
        }
        output.seekp(-2, std::ostream::_Seekcur); //remove last ,
        output << "}";
    };

    std::function<void(const std::shared_ptr<ConfigClass>&)> printClass = [&](const std::shared_ptr<ConfigClass>& data) {
        for (auto& it : data->getEntriesNoParent()) {
            if (it.isClass()) {
                auto& c = it.getAsClass();
                pushIndent();
                if (c->isDelete() || c->isDefinition()) {
                    if (c->isDelete())
                        output << "delete " << c->getName() << ";\n";
                    else
                        output << "class " << c->getName() << ";\n";
                } else {
                    output << "class " << c->getName();
                    if (c->hasParent())
                        output << ": " << c->getInheritedParentName() << " {\n";
                    else
                        output << " {\n";
                    indentLevel++;
                    printClass(c);
                    indentLevel--;
                    pushIndent();
                    output << "};\n";
                    if (indentLevel == 0)
                        output << "\n"; //Seperate classes on root level. Just for fancyness
                }
            } else if (it.isEntry()) {
                auto& var = it.getAsEntry();
                pushIndent();
                switch (var.getType()) {
                case rap_type::rap_string:
                    output << var.getName() << " = \"" << var.getAsString() << "\";\n";
                    break;
                case rap_type::rap_int:
                    output << var.getName() << " = " << var.getAsInt() << ";\n";
                    break;
                case rap_type::rap_float:
                    output << var.getName() << " = " << var.getAsFloat() << ";\n";
                    break;
                case rap_type::rap_array:
                case rap_type::rap_array_expansion:
                    output << var.getName() << (var.getType() == rap_type::rap_array_expansion ? "[] += " : "[] = ");
                    printArray(var.getAsArray());
                    output << ";\n";
                    break;
                }
            }



        }

    };
    printClass(getConfig());
}


std::vector<ConfigValue> Rapifier::derapify_array(std::istream &source) {
    std::vector<ConfigValue> output;
    uint32_t num_entries = read_compressed_int(source);

    for (int i = 0; i < num_entries; i++) {
        uint8_t type = source.get();

        if (type == 0) {
            std::string value;
            std::getline(source, value, '\0');

            output.emplace_back(rap_type::rap_string, escape_string(value));
        }
        else if (type == 1) {
            float float_value;
            source.read(reinterpret_cast<char*>(&float_value), sizeof(float_value));

            output.emplace_back(rap_type::rap_float, float_value);
        }
        else if (type == 2) {
            int32_t long_value;
            source.read(reinterpret_cast<char*>(&long_value), sizeof(long_value));

            output.emplace_back(rap_type::rap_int, long_value);
        }
        else if (type == 3) {
            //Result can't be invalid. If it was then the throw will step past this
            output.emplace_back(rap_type::rap_array, derapify_array(source));
        } else {
            if (source.eof()) {
                throw DerapifyException("Premature EOF", -1);
            }
            throw DerapifyException("Unknown array element type", type);
        }
    }

    return output;
}

void Rapifier::derapify_class(std::istream &source, ConfigClass &curClass, int level) {
    if (curClass.getName().empty()) {
        source.seekg(16);
    }

    uint32_t fp_tmp = source.tellg();
    std::string inherited;
    std::getline(source, inherited, '\0');

    const uint32_t num_entries = read_compressed_int(source);

    if (!curClass.getName().empty()) {
        curClass.inheritedParent = inherited;
    }

    std::vector<ConfigClassEntry> entries;

    for (int i = 0; i < num_entries; i++) {
        uint8_t type = source.get();

        if (type == 0) { //class definition
            auto subclass = std::make_shared<ConfigClass>();


            std::getline(source, subclass->name, '\0');

            uint32_t fp_class;
            source.read(reinterpret_cast<char*>(&fp_class), sizeof(uint32_t));
            fp_tmp = source.tellg();
            source.seekg(fp_class);
            try {
                derapify_class(source, *subclass, level + 1);
            } catch (DerapifyException& ex) {
                ex.addToStack(subclass->getName());
                throw;
            }

            entries.emplace_back(std::move(subclass));

            source.seekg(fp_tmp);
        } else if (type == 1) { //value
         //#TODO make enums for these
         //Var has special type
         //0 string
         //1 float
         //2 int

            type = source.get();

            std::string valueName;
            std::getline(source, valueName, '\0');

            rap_type valueType;
            ConfigValue valueContent;

            if (type == 0) {
                std::string value;
                std::getline(source, value, '\0');

                valueType = valueContent.type = rap_type::rap_string;
                valueContent.value = escape_string(value);
            } else if (type == 1) {
                float float_value;
                source.read(reinterpret_cast<char*>(&float_value), sizeof(float_value));

                valueType = valueContent.type = rap_type::rap_float;
                valueContent.value = float_value;
            } else if (type == 2) {
                int32_t long_value;
                source.read(reinterpret_cast<char*>(&long_value), sizeof(long_value));

                valueType = valueContent.type = rap_type::rap_int;
                valueContent.value = long_value;
            } else {
                throw DerapifyException("unknown valuetoken", type);
            }

            entries.emplace_back(ConfigEntry(valueType, std::move(valueName), std::move(valueContent)));
        } else if (type == 2 || type == 5) { //array or array append
            if (type == 5)
                source.seekg(4, std::istream::_Seekcur);

            std::string valueName;
            std::getline(source, valueName, '\0');

            auto rapType = type == 2 ? rap_type::rap_array : rap_type::rap_array_expansion;

            try {
                entries.emplace_back(ConfigEntry(rapType, std::move(valueName),
                    ConfigValue(rapType, derapify_array(source))
                ));
            } catch (DerapifyException& ex) {
                ex.addToStack(valueName);
                throw;
            }
        } else if (type == 3 || type == 4) { //3 is class 4 is delete class
            std::string classname;
            std::getline(source, classname, '\0');

            auto subclass = std::make_shared<ConfigClass>(std::move(classname));
            subclass->is_delete = type == 4;
            subclass->is_definition = true;

            entries.emplace_back(std::move(subclass));
        } else {
            if (source.eof()) {
                throw DerapifyException("Premature EOF", -1);
            }
            throw DerapifyException("Unknown class entry type", type);
        }
    }
    curClass.populateContent(std::move(entries));
}

void Rapifier::rapify_expression(const ConfigValue &expr, std::ostream &f_target) {

    if (expr.getType() == rap_type::rap_array) {
        auto& elements = expr.getAsArray();
        const uint32_t num_entries = elements.size();

        write_compressed_int(num_entries, f_target);

        for (auto& exp : elements) {
            switch (exp.getType()) {
                case rap_type::rap_string: f_target.put(0); break;
                case rap_type::rap_float: f_target.put(1); break;
                case rap_type::rap_int: f_target.put(2); break;
                case rap_type::rap_array: f_target.put(3); break;
                default: __debugbreak();
            }
            rapify_expression(exp, f_target);
        }
    } else if (expr.getType() == rap_type::rap_int) {
        auto val = expr.getAsInt();
        f_target.write(reinterpret_cast<char*>(&val), 4);
    } else if (expr.getType() == rap_type::rap_float) {
        auto val = expr.getAsFloat();
        f_target.write(reinterpret_cast<char*>(&val), 4);
    } else {
        f_target.write(
            expr.getAsString().c_str(),
            expr.getAsString().length() + 1
        );
    }
}

void Rapifier::rapify_variable(const ConfigEntry &var, std::ostream &f_target) {
    switch (var.getType()) {
        case rap_type::rap_string: f_target.put(1); f_target.put(0); break;
        case rap_type::rap_float: f_target.put(1); f_target.put(1); break;
        case rap_type::rap_int: f_target.put(1); f_target.put(2); break;
        case rap_type::rap_array: f_target.put(2); break;
        case rap_type::rap_array_expansion:
            f_target.put(5);
            f_target.write("\x01\0\0\0", 4);//last 3 bytes are unused by engine
            break;
    }

    f_target.write(var.getName().c_str(), var.getName().length() + 1);
    rapify_expression(var.getValue(), f_target);
}

void Rapifier::rapify_class(const ConfigClass &cfg, std::ostream &f_target) {
    uint32_t fp_temp;

    if (cfg.isDefinition() || cfg.isDelete()) {
        // extern or delete class
        f_target.put(static_cast<char>(cfg.isDelete() ? 4 : 3));
        f_target.write(cfg.getName().data(), cfg.getName().length() + 1);
        return;
    }
    
    if (cfg.hasParent())
        f_target.write(cfg.getInheritedParentName().data(), cfg.getInheritedParentName().length() + 1);
    else
        f_target.put(0);

    auto& entries = cfg.getEntriesNoParent();
    const uint32_t num_entries = entries.size();

    write_compressed_int(num_entries, f_target);
    for (auto& def : entries) {
        if (def.isEntry()) {
            auto& c = def.getAsEntry();
            rapify_variable(c, f_target);
        } else {
            auto& c = def.getAsClass();
            if (c->isDefinition()) {
                rapify_class(*c, f_target); //Write isDef/isDelete flag and return
            } else {
                f_target.put(0);
                f_target.write(c->getName().data(),
                    c->getName().length() + 1);
                c->setOffsetLocation(f_target.tellp());
                f_target.write("\0\0\0\0", 4);
            }
        }
    }
    //#TODO mikero writes a filesize marker here. 4 bytes
    //Arma won't read cuz num_entries

    for (auto& def : entries) {
        if (!def.isClass()) continue;
        auto& c = def.getAsClass();
        if (c->isDefinition() || c->isDelete())  continue;
        
        fp_temp = f_target.tellp();
        f_target.seekp(c->getOffsetLocation());
        
        f_target.write(reinterpret_cast<char*>(&fp_temp), sizeof(uint32_t));
        f_target.seekp(0, std::ostream::end);

        rapify_class(*c, f_target);
    }
}

bool Rapifier::isRapified(std::istream& input) {
    auto sourcePos = input.tellg();
    char buffer[4];
    input.read(buffer, 4);
    input.seekg(sourcePos);
    return strncmp(buffer, "\0raP", 4) == 0;
}

int Rapifier::rapify_file(const char* source, const char* target, Logger& logger) {

    std::ifstream sourceFile(source, std::ifstream::in | std::ifstream::binary);

    if (strcmp(target, "-") == 0) {
        return rapify_file(sourceFile, std::cout, source, logger);
    }
    else {
        std::ofstream targetFile(target, std::ofstream::out | std::ofstream::binary);

        //#TODO grab exceptions in case disk is full
        //targetFile.exceptions(std::ostream::failbit | std::ostream::badbit);
        //try {
        //    
        //}catch (std::ostream::failure e) {
        //    __debugbreak();
        //}

        return rapify_file(sourceFile, targetFile, source, logger);
    }


}

int Rapifier::rapify_file(std::istream &source, std::ostream &target, const char* sourceFileName, Logger& logger) {
    /*
     * Resolves macros/includes and rapifies the given file. If source and
     * target are identical, the target is overwritten.
     *
     * Returns 0 on success and a positive integer on failure.
     */
    current_target = sourceFileName;

    // Check if the file is already rapified
    if (isRapified(source)) {

        //#TODO we might want to throw an exception instead.
        //The caller of this can handle it more efficiently and maybe just get rid of target file

        //copy source to target
        std::array<char, 4096> buf;
        do {
            source.read(buf.data(), buf.size());
            target.write(buf.data(), source.gcount());
        } while (source.gcount() == buf.size()); //if gcount is not full buffer, we reached EOF before filling the buffer till end
        return 0;
    }

    Preprocessor preproc(logger);
    Preprocessor::ConstantMapType constants;
    std::stringstream fileToPreprocess;
    int success = preproc.preprocess(sourceFileName, source, fileToPreprocess, constants);

    current_target = sourceFileName;

    if (success) {
        logger.error("Failed to preprocess %s.\n", sourceFileName);
        return success;
    }

#if 0
    char dump_name[2048];
    sprintf(dump_name, "armake_preprocessed_%u.dump", (unsigned)time(NULL));

    std::array<char, 4096> buf;
    do {
        fileToPreprocess.read(buf.data(), buf.size());
        dump_name.write(buf.data(), fileToPreprocess.gcount());
    } while (fileToPreprocess.gcount() > 0);
#endif

    auto parsedConfig = Config::fromPreprocessedText(fileToPreprocess, preproc.getLineref(), logger, false);

    if (!parsedConfig.hasConfig()) {
        logger.error("Failed to parse config.\n");
        return 1;
    }

    rapify(*parsedConfig.getConfig(), target);

    return 0;
}


__itt_string_handle* handle_rapify = __itt_string_handle_create("Rapifier::rapify");
int Rapifier::rapify(const ConfigClass& cls, std::ostream& output) {
    __itt_task_begin(configDomain, __itt_null, __itt_null, handle_rapify);
    uint32_t enum_offset = 0;
    output.write("\0raP", 4);
    //4 byte int is a version number which has to be 0 or 1 to be rejected by OFP
    //4 byte int is the version number
    output.write("\0\0\0\0\x08\0\0\0", 8);
    output.write(reinterpret_cast<char*>(&enum_offset), 4); // this is replaced later

    rapify_class(cls, output);

    enum_offset = output.tellp();
    output.write("\0\0\0\0", 4); // fuck enums
    output.seekp(12);
    output.write(reinterpret_cast<char*>(&enum_offset), 4);
    __itt_task_end(configDomain);
    return 0; //#TODO this can't fail...
}
