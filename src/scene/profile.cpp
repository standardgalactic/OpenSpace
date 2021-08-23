/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2021                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <openspace/scene/profile.h>

#include <openspace/navigation/navigationstate.h>
#include <openspace/scripting/lualibrary.h>
#include <openspace/properties/property.h>
#include <openspace/properties/propertyowner.h>
#include <ghoul/misc/assert.h>
#include <ghoul/fmt.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/misc.h>
#include <ghoul/misc/profiling.h>
#include <set>
#include <json/json.hpp>

#include "profile_lua.inl"

namespace openspace {

namespace {
    // Helper structs for the visitor pattern of the std::variant
    template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
    template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

    std::vector<properties::Property*> changedProperties(
                                                      const properties::PropertyOwner& po)
    {
        std::vector<properties::Property*> res;
        for (properties::PropertyOwner* subOwner : po.propertySubOwners()) {
            std::vector<properties::Property*> ps = changedProperties(*subOwner);
            res.insert(res.end(), ps.begin(), ps.end());
        }
        for (properties::Property* p : po.properties()) {
            if (p->hasChanged()) {
                res.push_back(p);
            }
        }
        return res;
    }

    void checkValue(const nlohmann::json& j, const std::string& key,
                    bool (nlohmann::json::*checkFunc)() const,
                    std::string_view keyPrefix, bool isOptional)
    {
        if (j.find(key) == j.end()) {
            if (!isOptional) {
                throw Profile::ParsingError(
                    Profile::ParsingError::Severity::Error,
                    fmt::format("'{}.{}' field is missing", keyPrefix, key)
                );
            }
        }
        else {
            const nlohmann::json value = j[key];
            if (!(value.*checkFunc)()) {
                std::string type = [](auto c) {
                    if (c == &nlohmann::json::is_string) { return "a string"; }
                    else if (c == &nlohmann::json::is_number) { return "a number"; }
                    else if (c == &nlohmann::json::is_object) { return "an object"; }
                    else if (c == &nlohmann::json::is_array) { return "an array"; }
                    else if (c == &nlohmann::json::is_boolean) { return "a boolean"; }
                    else {
                        throw ghoul::MissingCaseException();
                    }
                }(checkFunc);

                throw Profile::ParsingError(
                    Profile::ParsingError::Severity::Error,
                    fmt::format("'{}.{}' must be {}", keyPrefix, key, type)
                );
            }
        }
    }

    void checkExtraKeys(const nlohmann::json& j, std::string_view prefix,
                        const std::set<std::string>& allowedKeys)
    {
        for (auto& [key, _] : j.items()) {
            if (allowedKeys.find(key) == allowedKeys.end()) {
                LINFOC(
                    "Profile",
                    fmt::format("Key '{}' not supported in '{}'", key, prefix)
                );
            }
        }
    }
} // namespace


//
// Current version:
//

void to_json(nlohmann::json& j, const Profile::Version& v) {
    j["major"] = v.major;
    j["minor"] = v.minor;
}

void from_json(const nlohmann::json& j, Profile::Version& v) {
    checkValue(j, "major", &nlohmann::json::is_number, "version", false);
    checkValue(j, "minor", &nlohmann::json::is_number, "version", false);
    checkExtraKeys(j, "version", { "major", "minor" });

    j["major"].get_to(v.major);
    j["minor"].get_to(v.minor);
}

void to_json(nlohmann::json& j, const Profile::Module& v) {
    j["name"] = v.name;
    if (v.loadedInstruction.has_value()) {
        j["loadedInstruction"] = *v.loadedInstruction;
    }
    if (v.notLoadedInstruction.has_value()) {
        j["notLoadedInstruction"] = *v.notLoadedInstruction;
    }
}

void from_json(const nlohmann::json& j, Profile::Module& v) {
    checkValue(j, "name", &nlohmann::json::is_string, "module", false);
    checkValue(j, "loadedInstruction", &nlohmann::json::is_string, "module", true);
    checkValue(j, "notLoadedInstruction", &nlohmann::json::is_string, "module", true);
    checkExtraKeys(j, "module", { "name", "loadedInstruction", "notLoadedInstruction" });

    j["name"].get_to(v.name);
    if (j.find("loadedInstruction") != j.end()) {
        v.loadedInstruction = j["loadedInstruction"].get<std::string>();
    }
    if (j.find("notLoadedInstruction") != j.end()) {
        v.notLoadedInstruction = j["notLoadedInstruction"].get<std::string>();
    }
}

void to_json(nlohmann::json& j, const Profile::Meta& v) {
    if (v.name.has_value()) {
        j["name"] = *v.name;
    }
    if (v.version.has_value()) {
        j["version"] = *v.version;
    }
    if (v.description.has_value()) {
        j["description"] = *v.description;
    }
    if (v.author.has_value()) {
        j["author"] = *v.author;
    }
    if (v.url.has_value()) {
        j["url"] = *v.url;
    }
    if (v.license.has_value()) {
        j["license"] = *v.license;
    }
}

void from_json(const nlohmann::json& j, Profile::Meta& v) {
    checkValue(j, "name", &nlohmann::json::is_string, "meta", true);
    checkValue(j, "version", &nlohmann::json::is_string, "meta", true);
    checkValue(j, "description", &nlohmann::json::is_string, "meta", true);
    checkValue(j, "author", &nlohmann::json::is_string, "meta", true);
    checkValue(j, "url", &nlohmann::json::is_string, "meta", true);
    checkValue(j, "license", &nlohmann::json::is_string, "meta", true);
    checkExtraKeys(
        j,
        "meta",
        { "name", "version", "description", "author", "url", "license" }
    );

    if (j.find("name") != j.end()) {
        v.name = j["name"].get<std::string>();
    }
    if (j.find("version") != j.end()) {
        v.version = j["version"].get<std::string>();
    }
    if (j.find("description") != j.end()) {
        v.description = j["description"].get<std::string>();
    }
    if (j.find("author") != j.end()) {
        v.author = j["author"].get<std::string>();
    }
    if (j.find("url") != j.end()) {
        v.url = j["url"].get<std::string>();
    }
    if (j.find("license") != j.end()) {
        v.license = j["license"].get<std::string>();
    }
}

void to_json(nlohmann::json& j, const Profile::Property::SetType& v) {
    j = [](Profile::Property::SetType t) {
        switch (t) {
            case Profile::Property::SetType::SetPropertyValue:
                return "setPropertyValue";
            case Profile::Property::SetType::SetPropertyValueSingle:
                return "setPropertyValueSingle";
            default:
                throw ghoul::MissingCaseException();
        }
    }(v);
}

void from_json(const nlohmann::json& j, Profile::Property::SetType& v) {
    std::string value = j.get<std::string>();
    if (value == "setPropertyValue") {
        v = Profile::Property::SetType::SetPropertyValue;
    }
    else if (value == "setPropertyValueSingle") {
        v = Profile::Property::SetType::SetPropertyValueSingle;
    }
    else {
        throw Profile::ParsingError(
            Profile::ParsingError::Severity::Error, "Unknown property set type"
        );
    }
}

void to_json(nlohmann::json& j, const Profile::Property& v) {
    j["type"] = v.setType;
    j["name"] = v.name;
    j["value"] = v.value;
}

void from_json(const nlohmann::json& j, Profile::Property& v) {
    checkValue(j, "type", &nlohmann::json::is_string, "property", false);
    checkValue(j, "name", &nlohmann::json::is_string, "property", false);
    checkValue(j, "value", &nlohmann::json::is_string, "property", false);
    checkExtraKeys(j, "property", { "type", "name", "value" });

    j["type"].get_to(v.setType);
    j["name"].get_to(v.name);
    j["value"].get_to(v.value);
}

void to_json(nlohmann::json& j, const Profile::Action& v) {
    j["identifier"] = v.identifier;
    j["documentation"] = v.documentation;
    j["name"] = v.name;
    j["gui_path"] = v.guiPath;
    j["is_local"] = v.isLocal;
    j["script"] = v.script;
}

void from_json(const nlohmann::json& j, Profile::Action& v) {
    checkValue(j, "identifier", &nlohmann::json::is_string, "action", false);
    checkValue(j, "documentation", &nlohmann::json::is_string, "action", false);
    checkValue(j, "name", &nlohmann::json::is_string, "action", false);
    checkValue(j, "gui_path", &nlohmann::json::is_string, "action", false);
    checkValue(j, "is_local", &nlohmann::json::is_boolean, "action", false);
    checkValue(j, "script", &nlohmann::json::is_string, "action", false);
    checkExtraKeys(
        j,
        "action",
        { "identifier", "documentation", "name", "gui_path", "is_local", "script" }
    );

    j["identifier"].get_to(v.identifier);
    j["documentation"].get_to(v.documentation);
    j["name"].get_to(v.name);
    j["gui_path"].get_to(v.guiPath);
    j["is_local"].get_to(v.isLocal);
    j["script"].get_to(v.script);
}

void to_json(nlohmann::json& j, const Profile::Keybinding& v) {
    j["key"] = keyToString(v.key);
    j["action"] = v.action;
}

void from_json(const nlohmann::json& j, Profile::Keybinding& v) {
    checkValue(j, "key", &nlohmann::json::is_string, "keybinding", false);
    checkValue(j, "action", &nlohmann::json::is_string, "keybinding", false);
    checkExtraKeys(j, "keybinding", { "key", "action" });

    v.key = stringToKey(j.at("key").get<std::string>());
    j["action"].get_to(v.action);
}

void to_json(nlohmann::json& j, const Profile::Time::Type& v) {
    j = [](Profile::Time::Type t) {
        switch (t) {
            case Profile::Time::Type::Absolute: return "absolute";
            case Profile::Time::Type::Relative: return "relative";
            default:                            throw ghoul::MissingCaseException();
        }
    }(v);
}

void from_json(const nlohmann::json& j, Profile::Time::Type& v) {
    std::string value = j.get<std::string>();
    if (value == "absolute") {
        v = Profile::Time::Type::Absolute;
    }
    else if (value == "relative") {
        v = Profile::Time::Type::Relative;
    }
    else {
        throw Profile::ParsingError(
            Profile::ParsingError::Severity::Error, "Unknown time type"
        );
    }
}

void to_json(nlohmann::json& j, const Profile::Time& v) {
    j["type"] = v.type;
    j["value"] = v.value;
}

void from_json(const nlohmann::json& j, Profile::Time& v) {
    checkValue(j, "type", &nlohmann::json::is_string, "time", false);
    checkValue(j, "value", &nlohmann::json::is_string, "time", false);
    checkExtraKeys(j, "time", { "type", "value" });

    j["type"].get_to(v.type);
    j["value"].get_to(v.value);
}

void to_json(nlohmann::json& j, const Profile::CameraNavState& v) {
    j["type"] = Profile::CameraNavState::Type;
    j["anchor"] = v.anchor;
    if (v.aim.has_value()) {
        j["aim"] = *v.aim;
    }
    j["frame"] = v.referenceFrame;
    nlohmann::json p {
        { "x", v.position.x },
        { "y", v.position.y },
        { "z", v.position.z }
    };
    j["position"] = p;
    if (v.up.has_value()) {
        nlohmann::json u {
            { "x", v.up->x },
            { "y", v.up->y },
            { "z", v.up->z }
        };
        j["up"] = u;
    }
    if (v.yaw.has_value()) {
        j["yaw"] = *v.yaw;
    }
    if (v.pitch.has_value()) {
        j["pitch"] = *v.pitch;
    }
}

void from_json(const nlohmann::json& j, Profile::CameraNavState& v) {
    ghoul_assert(
        j.at("type").get<std::string>() == Profile::CameraNavState::Type,
        "Wrong type for Camera"
    );

    checkValue(j, "anchor", &nlohmann::json::is_string, "camera", false);
    checkValue(j, "aim", &nlohmann::json::is_string, "camera", true);
    checkValue(j, "frame", &nlohmann::json::is_string, "camera", false);
    checkValue(j, "position", &nlohmann::json::is_object, "camera", false);
    checkValue(j["position"], "x", &nlohmann::json::is_number, "camera.position", false);
    checkValue(j["position"], "y", &nlohmann::json::is_number, "camera.position", false);
    checkValue(j["position"], "z", &nlohmann::json::is_number, "camera.position", false);
    checkExtraKeys(j["position"], "camera.position", { "x", "y", "z" });
    checkValue(j, "up", &nlohmann::json::is_object, "camera", true);
    if (j.find("up") != j.end()) {
        checkValue(j["up"], "x", &nlohmann::json::is_number, "camera.up", false);
        checkValue(j["up"], "y", &nlohmann::json::is_number, "camera.up", false);
        checkValue(j["up"], "z", &nlohmann::json::is_number, "camera.up", false);
        checkExtraKeys(j["up"], "camera.up", { "x", "y", "z" });
    }
    checkValue(j, "yaw", &nlohmann::json::is_number, "camera", true);
    checkValue(j, "pitch", &nlohmann::json::is_number, "camera", true);
    checkExtraKeys(
        j,
        "camera",
        { "type", "anchor", "aim", "frame", "position", "up", "yaw", "pitch" }
    );

    j["anchor"].get_to(v.anchor);
    if (j.find("aim") != j.end()) {
        v.aim = j["aim"].get<std::string>();
    }
    j["frame"].get_to(v.referenceFrame);
    nlohmann::json p = j["position"];
    p["x"].get_to(v.position.x);
    p["y"].get_to(v.position.y);
    p["z"].get_to(v.position.z);

    if (j.find("up") != j.end()) {
        nlohmann::json u = j["up"];
        glm::dvec3 up;
        u["x"].get_to(up.x);
        u["y"].get_to(up.y);
        u["z"].get_to(up.z);
        v.up = up;
    }

    if (j.find("yaw") != j.end()) {
        v.yaw = j["yaw"].get<double>();
    }

    if (j.find("pitch") != j.end()) {
        v.pitch = j["pitch"].get<double>();
    }
}

void to_json(nlohmann::json& j, const Profile::CameraGoToGeo& v) {
    j["type"] = Profile::CameraGoToGeo::Type;
    j["anchor"] = v.anchor;
    j["latitude"] = v.latitude;
    j["longitude"] = v.longitude;
    if (v.altitude.has_value()) {
        j["altitude"] = *v.altitude;
    }
}

void from_json(const nlohmann::json& j, Profile::CameraGoToGeo& v) {
    ghoul_assert(
        j.at("type").get<std::string>() == Profile::CameraGoToGeo::Type,
        "Wrong type for Camera"
    );

    checkValue(j, "anchor", &nlohmann::json::is_string, "camera", false);
    checkValue(j, "latitude", &nlohmann::json::is_number, "camera", false);
    checkValue(j, "longitude", &nlohmann::json::is_number, "camera", false);
    checkValue(j, "altitude", &nlohmann::json::is_number, "camera", true);
    checkExtraKeys(
        j,
        "camera",
        { "type", "anchor", "latitude", "longitude", "altitude" }
    );

    j["anchor"].get_to(v.anchor);
    j["latitude"].get_to(v.latitude);
    j["longitude"].get_to(v.longitude);

    if (j.find("altitude") != j.end()) {
        v.altitude = j["altitude"].get<double>();
    }
}

// In these namespaces we defined the structs as they used to be defined in the older
// versions. That way, we can keep the from_json files as they were originally written too
namespace version10 {

struct Keybinding {
    KeyWithModifier key;
    std::string documentation;
    std::string name;
    std::string guiPath;
    bool isLocal = true;
    std::string script;
};

void from_json(const nlohmann::json& j, version10::Keybinding& v) {
    checkValue(j, "key", &nlohmann::json::is_string, "keybinding", false);
    checkValue(j, "documentation", &nlohmann::json::is_string, "keybinding", false);
    checkValue(j, "name", &nlohmann::json::is_string, "keybinding", false);
    checkValue(j, "gui_path", &nlohmann::json::is_string, "keybinding", false);
    checkValue(j, "is_local", &nlohmann::json::is_boolean, "keybinding", false);
    checkValue(j, "script", &nlohmann::json::is_string, "keybinding", false);
    checkExtraKeys(
        j,
        "keybinding",
        { "key", "documentation", "name", "gui_path", "is_local", "script" }
    );

    v.key = stringToKey(j.at("key").get<std::string>());
    j["documentation"].get_to(v.documentation);
    j["name"].get_to(v.name);
    j["gui_path"].get_to(v.guiPath);
    j["is_local"].get_to(v.isLocal);
    j["script"].get_to(v.script);
}

void convertVersion10to11(nlohmann::json& profile) {
    // Version 1.1 introduced actions and remove Lua function calling from keybindings

    if (profile.find("keybindings") == profile.end()) {
        // We didn't find any keybindings, so there is nothing to do
        return;
    }

    // This needs to be changed if there is another version for any of these types later
    using Action = Profile::Action;
    using Keybinding = Profile::Keybinding;

    std::vector<Action> actions;
    std::vector<Keybinding> keybindings;

    std::vector<version10::Keybinding> kbs =
        profile.at("keybindings").get<std::vector<version10::Keybinding>>();
    for (size_t i = 0; i < kbs.size(); ++i) {
        version10::Keybinding& kb = kbs[i];
        std::string identifier = fmt::format("profile.keybind.{}", i);

        Action action;
        action.identifier = identifier;
        action.documentation = std::move(kb.documentation);
        action.name = std::move(kb.name);
        action.guiPath = std::move(kb.guiPath);
        action.isLocal = std::move(kb.isLocal);
        action.script = std::move(kb.script);
        actions.push_back(std::move(action));

        Keybinding keybinding;
        keybinding.key = kb.key;
        keybinding.action = identifier;
        keybindings.push_back(keybinding);
    }

    profile["actions"] = actions;
    profile["keybindings"] = keybindings;

    profile["version"] = Profile::Version{ 1, 1 };
}

} // namespace version10



Profile::ParsingError::ParsingError(Severity severity_, std::string msg)
    : ghoul::RuntimeError(std::move(msg), "profile")
    , severity(severity_)
{}

void Profile::saveCurrentSettingsToProfile(const properties::PropertyOwner& rootOwner,
                                           std::string currentTime,
                                           interaction::NavigationState navState)
{
    version = Profile::CurrentVersion;

    // Update properties
    std::vector<properties::Property*> ps = changedProperties(rootOwner);

    for (properties::Property* prop : ps) {
        Property p;
        p.setType = Property::SetType::SetPropertyValueSingle;
        p.name = prop->fullyQualifiedIdentifier();
        p.value = prop->getStringValue();
        properties.push_back(std::move(p));
    }

    // Add current time to profile file
    Time t;
    t.value = std::move(currentTime);
    t.type = Time::Type::Absolute;
    time = t;

    // Delta times
    std::vector<double> dts = global::timeManager->deltaTimeSteps();
    deltaTimes = std::move(dts);

    // Camera
    CameraNavState c;
    c.anchor = navState.anchor;
    c.aim = navState.aim;
    c.referenceFrame = navState.referenceFrame;
    c.position = navState.position;
    c.up = navState.up;
    c.yaw = navState.yaw;
    c.pitch = navState.pitch;
    camera = std::move(c);
}

void Profile::addAsset(const std::string& path) {
    ZoneScoped

    if (ignoreUpdates) {
        return;
    }

    const auto it = std::find(assets.cbegin(), assets.cend(), path);
    if (it == assets.end()) {
        assets.push_back(path);
    }
}

void Profile::removeAsset(const std::string& path) {
    ZoneScoped

    if (ignoreUpdates) {
        return;
    }

    const auto it = std::find(assets.cbegin(), assets.cend(), path);
    if (it == assets.end()) {
        throw ghoul::RuntimeError(fmt::format(
            "Tried to remove non-existing asset '{}'", path
        ));
    }

    assets.erase(it);
}

std::string Profile::serialize() const {
    nlohmann::json r;
    r["version"] = version;
    if (!modules.empty()) {
        r["modules"] = modules;
    }
    if (meta.has_value()) {
        r["meta"] = *meta;
    }
    if (!assets.empty()) {
        r["assets"] = assets;
    }
    if (!properties.empty()) {
        r["properties"] = properties;
    }
    if (!actions.empty()) {
        r["actions"] = actions;
    }
    if (!keybindings.empty()) {
        r["keybindings"] = keybindings;
    }
    if (time.has_value()) {
        r["time"] = *time;
    }
    if (!deltaTimes.empty()) {
        r["delta_times"] = deltaTimes;
    }
    if (camera.has_value()) {
        r["camera"] = std::visit(
            overloaded {
                [](const CameraNavState& c) { return nlohmann::json(c); },
                [](const Profile::CameraGoToGeo& c) { return nlohmann::json(c); }
            },
            *camera
        );
    }

    if (!markNodes.empty()) {
        r["mark_nodes"] = markNodes;
    }
    if (!additionalScripts.empty()) {
        r["additional_scripts"] = additionalScripts;
    }

    return r.dump(2);
}

Profile::Profile(const std::string& content) {
    try {
        nlohmann::json profile = nlohmann::json::parse(content);
        profile.at("version").get_to(version);

        // Update the file format in steps
        if (version.major == 1 && version.minor == 0) {
            version10::convertVersion10to11(profile);
            profile.at("version").get_to(version);
        }


        if (profile.find("modules") != profile.end()) {
            profile["modules"].get_to(modules);
        }
        if (profile.find("meta") != profile.end()) {
            meta = profile["meta"].get<Meta>();
        }
        if (profile.find("assets") != profile.end()) {
            profile["assets"].get_to(assets);
        }
        if (profile.find("properties") != profile.end()) {
            profile["properties"].get_to(properties);
        }
        if (profile.find("actions") != profile.end()) {
            profile["actions"].get_to(actions);
        }
        if (profile.find("keybindings") != profile.end()) {
            profile["keybindings"].get_to(keybindings);
        }
        if (profile.find("time") != profile.end()) {
            time = profile["time"].get<Time>();
        }
        if (profile.find("delta_times") != profile.end()) {
            profile["delta_times"].get_to(deltaTimes);
        }
        if (profile.find("camera") != profile.end()) {
            nlohmann::json c = profile.at("camera");
            if (c["type"] == CameraNavState::Type) {
                camera = c.get<CameraNavState>();
            }
            else if (c["type"] == CameraGoToGeo::Type) {
                camera = c.get<CameraGoToGeo>();
            }
            else {
                throw ParsingError(ParsingError::Severity::Error, "Unknown camera type");
            }
        }
        if (profile.find("mark_nodes") != profile.end()) {
            profile["mark_nodes"].get_to(markNodes);
        }
        if (profile.find("additional_scripts") != profile.end()) {
            profile["additional_scripts"].get_to(additionalScripts);
        }
    }
    catch (const nlohmann::json::exception& e) {
        std::string err = e.what();
        throw ParsingError(ParsingError::Severity::Error, err);
    }
}

scripting::LuaLibrary Profile::luaLibrary() {
    return {
        "",
        {
            {
                "saveSettingsToProfile",
                &luascriptfunctions::saveSettingsToProfile,
                {},
                "[string, bool]",
                "Collects all changes that have been made since startup, including all "
                "property changes and assets required, requested, or removed. All "
                "changes will be added to the profile that OpenSpace was started with, "
                "and the new saved file will contain all of this information. If the "
                "arugment is provided, the settings will be saved into new profile with "
                "that name. If the argument is blank, the current profile will be saved "
                "to a backup file and the original profile will be overwritten. The "
                "second argument determines if a file that already exists should be "
                "overwritten, which is 'false' by default"
            }
        }
    };
}

void convertToSeparatedAssets(const std::string filePre, const Profile& p) {
    convertSectionToAssetFile(filePre, p, "_meta", convertToAsset_meta);
    convertSectionToAssetFile(filePre, p, "_addedAssets", convertToAsset_addedAssets);
    convertSectionToAssetFile(filePre, p, "_modules", convertToAsset_modules);
    convertSectionToAssetFile(filePre, p, "_actions", convertToAsset_actions);
    convertSectionToAssetFile(filePre, p, "_keybinds", convertToAsset_keybinds);
    convertSectionToAssetFile(filePre, p, "_time", convertToAsset_time);
    convertSectionToAssetFile(filePre, p, "_deltaTimes", convertToAsset_deltaTimes);
    convertSectionToAssetFile(filePre, p, "_markNodes", convertToAsset_markNodes);
    convertSectionToAssetFile(filePre, p, "_properties", convertToAsset_properties);
    convertSectionToAssetFile(filePre, p, "_camera", convertToAsset_camera);
    convertSectionToAssetFile(filePre, p, "_addedScripts", convertToAsset_addedScripts);
}

void convertSectionToAssetFile(const std::string profilePrefix, const Profile& p,
                               const std::string profileSectionName,
                               std::function<std::string(const Profile&)> func)
{
    std::ofstream converted(fmt::format("{}_{}{}", profilePrefix,
        profileSectionName, p.assetFileExtension));
    converted << func(p);
}

std::string convertToAsset_meta(const Profile& p) {
    ZoneScoped

    std::string output;

    if (p.meta.has_value()) {
        output += "asset.meta = {\n";

        if (p.meta->name.has_value()) {
            output += fmt::format("  Name = [[{}]],\n", *p.meta->name);
        }
        if (p.meta->version.has_value()) {
            output += fmt::format("  Version = [[{}]],\n", *p.meta->version);
        }
        if (p.meta->description.has_value()) {
            output += fmt::format("  Description = [[{}]],\n", *p.meta->description);
        }
        if (p.meta->author.has_value()) {
            output += fmt::format("  Author = [[{}]],\n", *p.meta->author);
        }
        if (p.meta->url.has_value()) {
            output += fmt::format("  URL = [[{}]],\n", *p.meta->url);
        }
        if (p.meta->license.has_value()) {
            output += fmt::format("  License = [[{}]]\n", *p.meta->license);
        }

        output += "}\n\n";
    }

    return output;
}

std::string convertToAsset_addedAssets(const Profile& p) {
    ZoneScoped

    std::string output;

    // Assets
    for (const std::string& asset : p.assets) {
        output += fmt::format("asset.require(\"{}\");\n", asset);
    }

    return output;
}

std::string convertToAsset_modules(const Profile& p) {
    ZoneScoped

    std::string output;
    for (const Profile::Module& m : p.modules) {
        output += fmt::format(
            "if openspace.modules.isLoaded(\"{}\") then {} else {} end\n",
            m.name, *m.loadedInstruction, *m.notLoadedInstruction
        );
    }

    return output;
}

std::string convertToAsset_actions(const Profile& p) {
    ZoneScoped

    std::string output = "asset.onInitialize(function()\n";
    for (const Profile::Action& action : p.actions) {
        const std::string name = action.name.empty() ? action.identifier : action.name;
        output += fmt::format(
            "  openspace.action.registerAction({{"
            "Identifier=[[{}]], Command=[[{}]], Name=[[{}]], Documentation=[[{}]], "
            "GuiPath=[[{}]], IsLocal={}"
            "}})\n",
            action.identifier, action.script, name, action.documentation, action.guiPath,
            action.isLocal ? "true" : "false"
        );
    }
    output += "end)\n";

    return output;
}

std::string convertToAsset_keybinds(const Profile& p) {
    ZoneScoped

    std::string output = "asset.onInitialize(function()\n";
    for (size_t i = 0; i < p.keybindings.size(); ++i) {
        const Profile::Keybinding& k = p.keybindings[i];
        const std::string key = keyToString(k.key);
        output += fmt::format("  openspace.bindKey([[{}]], [[{}]])\n", key, k.action);
    }
    output += "end)\n";

    return output;
}

std::string convertToAsset_time(const Profile& p) {
    ZoneScoped

    std::string output = "asset.onInitialize(function()\n";
    switch (p.time->type) {
    case Profile::Time::Type::Absolute:
        output += fmt::format("  openspace.time.setTime(\"{}\")\n", p.time->value);
        break;
    case Profile::Time::Type::Relative:
        output += "  local now = openspace.time.currentWallTime();\n";
        output += fmt::format(
            "  local prev = openspace.time.advancedTime(now, \"{}\");\n", p.time->value
        );
        output += "  openspace.time.setTime(prev);\n";
        break;
    default:
        throw ghoul::MissingCaseException();
    }
    output += "end)\n";

    return output;
}

std::string convertToAsset_deltaTimes(const Profile& p) {
    ZoneScoped

    std::string output = "asset.onInitialize(function()\n";
    {
        std::string times;
        for (double d : p.deltaTimes) {
            times += fmt::format("{}, ", d);
        }
        output += fmt::format("  openspace.time.setDeltaTimeSteps({{ {} }});\n", times);
    }
    output += "end)\n";

    return output;
}

std::string convertToAsset_markNodes(const Profile& p) {
    ZoneScoped

    std::string output = "asset.onInitialize(function()\n";
    {
        std::string nodes;
        for (const std::string& n : p.markNodes) {
            nodes += fmt::format("[[{}]],", n);
        }
        output += fmt::format("  openspace.markInterestingNodes({{ {} }});\n", nodes);
    }
    output += "end)\n";

    return output;
}

std::string convertToAsset_properties(const Profile& p) {
    ZoneScoped

    std::string output = "asset.onInitialize(function()\n";
    for (const Profile::Property& prop : p.properties) {
        switch (prop.setType) {
        case Profile::Property::SetType::SetPropertyValue:
            output += fmt::format(
                "  openspace.setPropertyValue(\"{}\", {});\n", prop.name, prop.value
            );
            break;
        case Profile::Property::SetType::SetPropertyValueSingle:
            output += fmt::format(
                "  openspace.setPropertyValueSingle(\"{}\", {});\n",
                prop.name, prop.value
            );
            break;
        default:
            throw ghoul::MissingCaseException();
        }
    }
    output += "end)\n";

    return output;
}

std::string convertToAsset_camera(const Profile& p) {
    ZoneScoped

    std::string output = "asset.onInitialize(function()\n";
    if (p.camera.has_value()) {
        output += std::visit(
            overloaded {
                [](const Profile::CameraNavState& c) {
                    std::string result;
                    result += "  openspace.navigation.setNavigationState({";
                    result += fmt::format("Anchor = [[{}]], ", c.anchor);
                    if (c.aim.has_value()) {
                        result += fmt::format("Aim = [[{}]], ", *c.aim);
                    }
                    if (!c.referenceFrame.empty()) {
                        result += fmt::format(
                            "ReferenceFrame = [[{}]], ", c.referenceFrame
                        );
                    }
                    result += fmt::format(
                        "Position = {{ {}, {}, {} }}, ",
                        c.position.x, c.position.y, c.position.z
                    );
                    if (c.up.has_value()) {
                        result += fmt::format(
                            "Up = {{ {}, {}, {} }}, ", c.up->x, c.up->y, c.up->z
                        );
                    }
                    if (c.yaw.has_value()) {
                        result += fmt::format("Yaw = {}, ", *c.yaw);
                    }
                    if (c.pitch.has_value()) {
                        result += fmt::format("Pitch = {} ", *c.pitch);
                    }
                    result += "})\n";
                    return result;
                },
                [](const Profile::CameraGoToGeo& c) {
                    if (c.altitude.has_value()) {
                        return fmt::format(
                            "  openspace.globebrowsing.goToGeo([[{}]], {}, {}, {});\n",
                            c.anchor, c.latitude, c.longitude, *c.altitude
                        );
                    }
                    else {
                        return fmt::format(
                            "  openspace.globebrowsing.goToGeo([[{}]], {}, {});\n",
                            c.anchor, c.latitude, c.longitude
                        );
                    }
                }
            },
            *p.camera
                    );
    }
    output += "end)\n";

    return output;
}

std::string convertToAsset_addedScripts(const Profile& p) {
    ZoneScoped

    std::string output = "asset.onInitialize(function()\n";
    for (const std::string& a : p.additionalScripts) {
        output += fmt::format("  {}\n", a);
    }
    output += "end)\n";

    return output;
}

}  // namespace openspace
