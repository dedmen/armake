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


#include "preprocess.h"
#include <utility>
#include <variant>
#include <vector>
#include <memory>
#include <map>
#include <unordered_set>
#include <execution>
#include <utility> //std::hash

#define MAXCLASSES 4096

enum class rap_type {
    rap_class,
    rap_var,
    rap_array,
    rap_array_expansion,
    rap_string,
    rap_int,
    rap_float,
    invalid
};

class ConfigValue {
    friend class Rapifier;
    rap_type type{ rap_type::invalid };
    using ArrayValueT = std::vector<ConfigValue>;
    std::variant<int32_t, float, std::string, ArrayValueT> value;
public:
    ConfigValue() = default;
    ConfigValue(rap_type t, int32_t x) : type(t), value(x) {
        if (t != rap_type::rap_int) __debugbreak();
    }
    ConfigValue(rap_type t, float x) : type(t), value(x) {
        if (t != rap_type::rap_float) __debugbreak();
    }
    ConfigValue(rap_type t, std::string x) : type(t), value(std::move(x)) {
        if (t != rap_type::rap_string) __debugbreak();
    }
    ConfigValue(rap_type t, std::vector<ConfigValue> x) : type(t), value(std::move(x)) {
        if (t != rap_type::rap_array) __debugbreak();
    }
    ConfigValue(rap_type t, ConfigValue x) : type(t) {
        if (t == rap_type::rap_array && x.type == rap_type::rap_array) {
            value = x.value;
            return;
        }
        if (t == rap_type::rap_array) {
            addArrayElement(x);
            return;
        }
        __debugbreak(); //#TODO remove

    }

    void addArrayElement(ConfigValue exp) {
        if (std::holds_alternative<ArrayValueT>(value)) {
            std::get<ArrayValueT>(value).emplace_back(std::move(exp));
            return;
        }

        ArrayValueT newVal;

        std::visit([&newVal](auto&& arg) {

            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int>)
                newVal.emplace_back(rap_type::rap_int, arg);
            else if constexpr (std::is_same_v<T, float>)
                newVal.emplace_back(rap_type::rap_float, arg);
            else if constexpr (std::is_same_v<T, std::string>)
                newVal.emplace_back(rap_type::rap_string, arg);
        }, value);
        newVal.emplace_back(std::move(exp));
        value = newVal;
        type = rap_type::rap_array;
    }

    rap_type getType() const {
        return type;
    }

    int32_t getAsInt() const {
        if (type == rap_type::rap_int) return std::get<int32_t>(value);
        if (type == rap_type::rap_float) return static_cast<int32_t>(std::get<float>(value));
        __debugbreak(); //#TODO throw
    }
    float getAsFloat() const {
        if (type == rap_type::rap_int) return static_cast<float>(std::get<int32_t>(value));
        if (type == rap_type::rap_float) return std::get<float>(value);
        __debugbreak(); //#TODO throw
    }
    const std::string& getAsString() const {
        if (type == rap_type::rap_string) return std::get<std::string>(value);
        __debugbreak(); //#TODO throw
    }
    const ArrayValueT& getAsArray() const {
        if (type == rap_type::rap_array) return std::get<ArrayValueT>(value);
        __debugbreak(); //#TODO throw
    }
};

class ConfigEntry {
    rap_type type{ rap_type::invalid };
    std::string name;
    ConfigValue value;
public:
    ConfigEntry() = default;
    ConfigEntry(std::string_view name) : name(name) {};
    ConfigEntry(rap_type type, std::string name, ConfigValue expression)
        : type(type), name(std::move(name)), value(std::move(expression)) {

    }
    const std::string& getName() const {
        return name;
    }
    rap_type getType() const {
        return type;
    }

    const ConfigValue& getValue() const {
        return value;
    }
    auto getAsInt() const {
        return value.getAsInt();
    }
    auto getAsFloat() const {
        return value.getAsInt();
    }
    auto& getAsString() const {
        return value.getAsString();
    }
    auto& getAsArray() const {
        return value.getAsArray();
    }
};

class ConfigClass;
class ConfigClassEntry {
    std::variant<
        std::shared_ptr<ConfigClass>,
        ConfigEntry,
        std::string_view //placeholder for lookups
    > value;

    static bool iequals(std::string_view a, std::string_view b)
    {
        return std::equal(a.begin(), a.end(),
            b.begin(), b.end(),
            [](char a, char b) {
            return tolower(a) == tolower(b);
        });
    }
public:
    ConfigClassEntry(std::shared_ptr<ConfigClass> cls) : value(cls) {}
    ConfigClassEntry(ConfigEntry &&val) : value(val) {}
    ConfigClassEntry(std::string_view name) : value(name) {}

    std::string_view getName() const;

    bool operator==(const ConfigClassEntry& other) const { //Just for lookup purposes in the unordered_set of class_
        return iequals(getName(), other.getName());
    }

    bool isClass() const noexcept {
        return value.index() == 0;
    }
    bool isEntry() const noexcept {
        return value.index() == 1;
    }
    const std::shared_ptr<ConfigClass>& getAsClass() const {
        if (!isClass()) __debugbreak();
        return std::get<0>(value);
    }
    const ConfigEntry& getAsEntry() const {
        if (!isEntry()) __debugbreak();
        return std::get<1>(value);
    }
};

namespace std {
    template<>
    struct hash<ConfigClassEntry> {
        typedef ConfigClassEntry argument_type;
        typedef size_t result_type;

        size_t operator()(const ConfigClassEntry& x) const {
            auto toLowerCpy = std::string(x.getName());
            std::transform(toLowerCpy.begin(), toLowerCpy.end(), toLowerCpy.begin(), ::tolower);
            return std::hash<std::string>()(toLowerCpy);
        }
    };
}

class ConfigClass : public std::enable_shared_from_this<ConfigClass> {
    friend class Rapifier;
    std::string name;
    std::variant<std::monostate, std::string, std::weak_ptr<ConfigClass>> inheritedParent;
    std::weak_ptr<ConfigClass> treeParent;
    std::unordered_set<ConfigClassEntry> entries;
    bool is_delete{ false };
    bool is_definition{ false };
    long offset_location{ 0 };

    void populateContent(std::vector<ConfigClassEntry> &content) {
        for (auto& it : content) {
            entries.emplace(std::move(it));
        }
    }
public:
    struct definitionT {};
    struct deleteT {};
    ConfigClass() = default;
    ConfigClass(std::string name, definitionT) : name(std::move(name)), is_delete(true), is_definition(true) {}
    ConfigClass(std::string name, deleteT) : name(std::move(name)), is_definition(true) {}
    ConfigClass(std::string name, std::string parent) : name(std::move(name)) {
        if (!parent.empty()) this->inheritedParent = parent;
    }
    ConfigClass(std::string name, std::string parent, std::vector<ConfigClassEntry> content)
        : name(std::move(name)) {
        if (!parent.empty()) this->inheritedParent = parent;
        populateContent(content);
    }
    ConfigClass(std::string name, std::vector<ConfigClassEntry> content)
        : name(std::move(name)) {
        populateContent(content);
    }
    ConfigClass(std::vector<ConfigClassEntry> content) { populateContent(content); }
    ConfigClass(std::string_view name) : name(name) {}


    std::string_view getName() const {
        return name;
    }
    std::string_view getInheritedParentName() const {
        if (inheritedParent.index() == 0) return {};
        if (inheritedParent.index() == 1) return std::get<1>(inheritedParent);
        if (inheritedParent.index() == 2) return std::get<2>(inheritedParent).lock()->getName();
        return "";
    }
    bool hasParent() const {
        return inheritedParent.index() != 0;
    }
    bool isDefinition() const {
        return is_definition;
    }
    bool isDelete() const {
        return is_delete;
    }
    long getOffsetLocation() const {
        return offset_location;
    }
    void setOffsetLocation(long v) {
        offset_location = v;
    }


    std::shared_ptr<ConfigClass> findInheritedParent(const ConfigClassEntry &parentName) const {
        auto found = entries.find(parentName);
        if (found != entries.end() && found->isClass()) //Do my subclasses contain the wanted class?
            return found->getAsClass();
        if (inheritedParent.index() == 2)
            return std::get<2>(inheritedParent).lock()->findInheritedParent(parentName);

        if (treeParent.lock())
            return treeParent.lock()->findInheritedParent(parentName);
        return nullptr;
    }

    void buildParentTree();

    bool hasParentTreeBeenBuilt() const noexcept {
        using wt = std::weak_ptr<ConfigClass>;
        const bool isUninitialized = !treeParent.owner_before(wt{}) && !wt{}.owner_before(treeParent);
        return !isUninitialized;
    }

    using ConfigPath = std::initializer_list<std::string_view>;

    std::vector<std::shared_ptr<ConfigClass>> getParents() const;
    std::map<std::string_view, std::reference_wrapper<const ConfigEntry>> getEntries() const;
    const std::unordered_set<ConfigClassEntry>& getEntriesNoParent() const {
        return entries;
    }

    std::map<std::string_view, std::shared_ptr<ConfigClass>> getSubclasses() const;
    std::optional<ConfigClassEntry> getEntry(ConfigPath path) const;
    //can return nullptr if not found
    std::shared_ptr<ConfigClass> getClass(ConfigPath path) const;
    std::optional<int32_t> getInt(ConfigPath path) const;
    std::optional<float> getFloat(ConfigPath path) const;
    std::optional<std::string> getString(ConfigPath path) const;
    std::optional<std::vector<std::string>> getArrayOfStrings(ConfigPath path) const;
    //This variant doesn't copy the strings. It should be used instead of getArrayOfStrings wherever possible
    std::optional<std::vector<std::string_view>> getArrayOfStringViews(ConfigPath path) const;
    std::vector<float> getArrayOfFloats(ConfigPath path) const;

};





class Config {
public:
    static Config fromPreprocessedText(std::istream &input, struct lineref &lineref);
    static Config fromBinarized(std::istream &input);
    void toBinarized(std::ostream &output);
    void toPlainText(std::ostream &output, std::string_view indent = "    ");

    bool hasConfig() { return static_cast<bool>(config); }
    std::shared_ptr<ConfigClass> getConfig() { return config; }

    operator ConfigClass&(){
        return *config;
    }
    std::shared_ptr<ConfigClass> operator->() {
        return config;
    }

private:
    std::shared_ptr<ConfigClass> config;
};


class Rapifier {
    friend class Config;
    static void rapify_expression(const ConfigValue &expr, std::ostream &f_target);
    static void rapify_variable(const ConfigEntry &var, std::ostream &f_target);
    static void rapify_class(const ConfigClass &cfg, std::ostream &f_target);

    static std::optional<std::vector<ConfigValue>> derapify_array(std::istream &source);
    static int derapify_class(std::istream &source, ConfigClass &curClass, int level);

public:
    static bool isRapified(std::istream &input);
    static int rapify_file(const char* source, const char* target);
    static int rapify_file(std::istream &source, std::ostream &target, const char* sourceFileName);

    static int rapify(const ConfigClass& cls, std::ostream& output);
};
