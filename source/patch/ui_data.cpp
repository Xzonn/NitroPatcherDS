#include "ui_data.h"
#include "package.h"
#include "miniz.h"
#include "cJSON.h"
#include "md4c.h"
extern "C" {
#include "entity.h"
}
#include "uni_algo/conv.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <unistd.h>

namespace np3d {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

std::string text(const cJSON* json, const char* key) {
    auto item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

void newline(std::string& output) {
    if (!output.empty() && output.back() != '\n') output += '\n';
}
int leaveBlock(MD_BLOCKTYPE type, void*, void* opaque) {
    if (type != MD_BLOCK_DOC) newline(*static_cast<std::string*>(opaque));
    return 0;
}
int markdownText(MD_TEXTTYPE type, const char* data, MD_SIZE size, void* opaque) {
    auto& output = *static_cast<std::string*>(opaque);
    if (type == MD_TEXT_HTML) return 0;
    if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) { newline(output); return 0; }
    if (type == MD_TEXT_ENTITY) {
        if (const ENTITY* entity = entity_lookup(data, size)) {
            std::u32string codepoints(1, entity->codepoints[0]);
            if (entity->codepoints[1]) codepoints += entity->codepoints[1];
            output += una::utf32to8(codepoints);
            return 0;
        }
    }
    output.append(data, size);
    return 0;
}
std::string plainMarkdown(const std::string& input) {
    std::string output;
    MD_PARSER parser = {};
    parser.enter_block = [](MD_BLOCKTYPE, void*, void*) { return 0; };
    parser.leave_block = leaveBlock;
    parser.enter_span = [](MD_SPANTYPE, void*, void*) { return 0; };
    parser.leave_span = [](MD_SPANTYPE, void*, void*) { return 0; };
    parser.text = markdownText;
    require(md_parse(input.data(), input.size(), &parser, &output) == 0, "Cannot read patch README");
    return output;
}

}

PatchInfo readPatchMetadata(const std::string& data) {
    una::error error;
    una::strict::utf8to32u(data, error);
    if (error) return {};
    const char* end = nullptr;
    Json json(cJSON_ParseWithLengthOpts(data.c_str(), data.size() + 1, &end, true), cJSON_Delete);
    if (!cJSON_IsObject(json.get()) || end != data.c_str() + data.size()) return {};
    PatchInfo result;
    auto property = [&](const char* name) -> const cJSON* {
        const cJSON* found = nullptr;
        for (auto item = json->child; item; item = item->next)
            if (item->string && std::strcmp(item->string, name) == 0) found = item;
        return found;
    };
    auto string = [&](const char* name) {
        auto item = property(name);
        return std::string(cJSON_IsString(item) ? item->valuestring : "");
    };
    auto version = property("formatVersion");
    if (version) {
        double value = version->valuedouble;
        if (!cJSON_IsNumber(version) || !std::isfinite(value) || value < 1 || value > 9007199254740991.0 || std::floor(value) != value)
            result.error = "metadata.json formatVersion must be a positive safe integer";
        else if (value != 1 && value != 2) result.error = "Unsupported XZP format version";
        else result.formatVersion = uint32_t(value);
    }
    result.id = string("id");
    result.name = string("name");
    result.author = string("author");
    result.homepage = string("homepage");
    result.version = string("version");
    result.beta = cJSON_IsTrue(property("isBeta"));
    return result;
}

PatchInfo inspectPatch(const std::string& path) {
    if (isDeltaPatch(path)) return {};
    Package package(path);
    PatchInfo result = package.metadata;
    if (!result.error.empty()) return result;
    for (const char* name : {"README.md", "README.txt"}) {
        if (!package.has(name)) continue;
        auto bytes = package.bytes(name);
        std::string content(bytes.begin(), bytes.end());
        una::error error;
        una::strict::utf8to32u(content, error);
        require(!error, "Patch information is not UTF-8");
        result.readme = name[7] == 'm' ? plainMarkdown(content) : content;
        break;
    }
    return result;
}

Settings loadSettings(const std::string& root) {
    Settings settings;
    std::string path = root + "/config/settings.json";
    if (!exists(path)) return settings;
    auto input = std::make_shared<Disk>(path);
    auto bytes = readBytes(input, 0, input->size);
    bytes.push_back(0);
    Json json(cJSON_ParseWithLength(reinterpret_cast<const char*>(bytes.data()), bytes.size()), cJSON_Delete);
    require(cJSON_IsObject(json.get()), "Invalid settings.json");
    settings.language = text(json.get(), "language");
    auto games = cJSON_GetObjectItemCaseSensitive(json.get(), "games");
    for (auto game = games ? games->child : nullptr; game; game = game->next) {
        require(cJSON_IsArray(game), "Invalid game patch selection");
        for (auto item = game->child; item; item = item->next) {
            require(cJSON_IsString(item), "Invalid selected patch filename");
            settings.games[game->string].push_back(item->valuestring);
        }
    }
    auto apPatch = cJSON_GetObjectItemCaseSensitive(json.get(), "apPatch");
    for (auto game = apPatch ? apPatch->child : nullptr; game; game = game->next) {
        require(cJSON_IsBool(game), "Invalid game anti-piracy setting");
        settings.apPatch[game->string] = cJSON_IsTrue(game);
    }
    auto skipRomMd5 = cJSON_GetObjectItemCaseSensitive(json.get(), "skipRomMd5");
    for (auto game = skipRomMd5 ? skipRomMd5->child : nullptr; game; game = game->next) {
        require(cJSON_IsBool(game), "Invalid game ROM MD5 setting");
        settings.skipRomMd5[game->string] = cJSON_IsTrue(game);
    }
    return settings;
}

void saveSettings(const std::string& root, const Settings& settings) {
    makeDirectory(root + "/config");
    Json json(cJSON_CreateObject(), cJSON_Delete);
    require(bool(json), "Cannot allocate settings");
    cJSON_AddStringToObject(json.get(), "language", settings.language.c_str());
    auto games = cJSON_AddObjectToObject(json.get(), "games");
    for (const auto& game : settings.games) {
        auto array = cJSON_AddArrayToObject(games, game.first.c_str());
        for (const auto& patch : game.second) cJSON_AddItemToArray(array, cJSON_CreateString(patch.c_str()));
    }
    auto apPatch = cJSON_AddObjectToObject(json.get(), "apPatch");
    for (const auto& game : settings.apPatch) cJSON_AddBoolToObject(apPatch, game.first.c_str(), game.second);
    auto skipRomMd5 = cJSON_AddObjectToObject(json.get(), "skipRomMd5");
    for (const auto& game : settings.skipRomMd5) cJSON_AddBoolToObject(skipRomMd5, game.first.c_str(), game.second);
    char* data = cJSON_PrintUnformatted(json.get());
    require(data, "Cannot serialize settings");
    std::string path = root + "/config/settings.json";
    { Disk output(path + ".new", "wb"); output.write(0, data, std::strlen(data)); output.sync(); }
    cJSON_free(data);
    if (exists(path)) require(unlink(path.c_str()) == 0, "Cannot replace settings.json");
    require(std::rename((path + ".new").c_str(), path.c_str()) == 0, "Cannot publish settings.json");
}
}
