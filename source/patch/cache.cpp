#include "core.h"
#include "package.h"
#include "cJSON.h"
#include <cstring>
#include <dirent.h>
#include <map>
#include <unistd.h>

namespace np3d {

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

static Json object() { return Json(cJSON_CreateObject(), cJSON_Delete); }

static Json readJson(const std::string& path) {
    auto input = std::make_shared<Disk>(path);
    auto data = readBytes(input, 0, input->size);
    data.push_back(0);
    return Json(cJSON_ParseWithLength(reinterpret_cast<const char*>(data.data()), data.size()), cJSON_Delete);
}

static void writeJson(const std::string& path, const cJSON* json) {
    char* text = cJSON_PrintUnformatted(json);
    require(text, "Cannot serialize cache metadata");
    {
        Disk output(path + ".new", "wb");
        output.write(0, text, std::strlen(text));
        output.sync();
    }
    cJSON_free(text);
    if (exists(path))
        require(unlink(path.c_str()) == 0, "Cannot replace " + path);
    require(std::rename((path + ".new").c_str(), path.c_str()) == 0, "Cannot publish " + path);
}

static std::string text(const cJSON* json, const char* name) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(json, name);
    return cJSON_IsString(item) ? item->valuestring : "";
}

static bool number(const cJSON* json, const char* name, uint32_t expected) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(json, name);
    return cJSON_IsNumber(item) && item->valuedouble == expected;
}

static bool hexString(const std::string& value, size_t length) {
    return value.size() == length && std::all_of(value.begin(), value.end(), [](char byte) {
               return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
           });
}

static std::string cardDigest(SourcePtr card, const std::string& root, const std::string& identity) {
    std::string path = root + "/config/cards/" + identity + "-" + std::to_string(card->size) + ".json";
    if (exists(path)) {
        auto metadata = readJson(path);
        require(cJSON_IsObject(metadata.get()) && number(metadata.get(), "size", card->size),
                "Invalid card identity cache");
        auto md5 = text(metadata.get(), "md5");
        require(hexString(md5, 32), "Invalid cached ROM digest");
        return md5;
    }
    auto md5 = digest(card, false, "Reading Game Card");
    auto metadata = object();
    cJSON_AddStringToObject(metadata.get(), "md5", md5.c_str());
    cJSON_AddNumberToObject(metadata.get(), "size", card->size);
    writeJson(path, metadata.get());
    return md5;
}

void clearGameCache(const std::string& root, const std::string& cardKey) {
    for (const char* suffix : {"", "-ds"}) {
        removeTree(root + "/cache/" + cardKey + suffix);
        removeTree(root + "/config/patches/" + cardKey + suffix);
    }
    for (const char* suffix : {".json", ".json.new"}) {
        std::string path = root + "/config/cards/" + cardKey + suffix;
        if (exists(path))
            require(unlink(path.c_str()) == 0, "Cannot remove " + path);
    }
}

static bool complete(const std::string& directory, const std::string& patch, const std::string& identity,
                     uint32_t cardSize, bool checkMd5 = false, bool legacyChecked = true) {
    for (const char* name : {"complete", "manifest.json", "map.bin", "data.bin"})
        if (!exists(directory + "/" + name))
            return false;
    auto metadata = readJson(directory + "/manifest.json");
    if (!cJSON_IsObject(metadata.get()) || !number(metadata.get(), "format", FormatVersion) ||
        !number(metadata.get(), "rules", RulesVersion) || text(metadata.get(), "patchCrc32") != patch ||
        text(metadata.get(), "cardIdentity") != identity || !number(metadata.get(), "cardSize", cardSize))
        return false;
    auto checked = cJSON_GetObjectItemCaseSensitive(metadata.get(), "romMd5Checked");
    if (checkMd5 && !(checked ? cJSON_IsTrue(checked) : legacyChecked))
        return false;
    Disk map(directory + "/map.bin"), data(directory + "/data.bin");
    return number(metadata.get(), "mapSize", map.size) && number(metadata.get(), "dataSize", data.size);
}

static void updatePatchIndex(const std::string& root, const std::string& rom, const std::string& hash,
                             const std::string& identity) {
    makeDirectory(root + "/config/patches");
    std::string directory = root + "/config/patches/" + rom;
    makeDirectory(directory);
    std::string path = directory + "/" + digest(identity.data(), identity.size(), true) + ".json";
    std::string previous;
    if (exists(path)) {
        auto index = readJson(path);
        require(cJSON_IsObject(index.get()), "Invalid patch cache index");
        previous = text(index.get(), "patchCrc32");
        require(hexString(previous, 8), "Invalid patch cache identity");
        if (previous == hash)
            return;
    }
    auto index = object();
    cJSON_AddStringToObject(index.get(), "patchCrc32", hash.c_str());
    writeJson(path, index.get());
    if (previous.empty())
        return;
    DIR* entries = opendir(directory.c_str());
    require(entries, "Cannot read patch cache index");
    bool referenced = false;
    while (dirent* entry = readdir(entries)) {
        std::string name = entry->d_name;
        if (name.size() < 5 || name.substr(name.size() - 5) != ".json")
            continue;
        auto other = readJson(directory + "/" + name);
        require(cJSON_IsObject(other.get()), "Invalid patch cache index");
        if (text(other.get(), "patchCrc32") == previous)
            referenced = true;
    }
    require(closedir(entries) == 0, "Cannot close patch cache index");
    if (!referenced)
        removeTree(root + "/cache/" + rom + "/" + previous);
}

static void writeCache(Map& rom, Map& final) {
    std::vector<uint8_t> buffer(BufferSize);
    Cached input(rom.payload);
    uint32_t filled = 0, first = 0, pending = 0;
    auto transfer = [&](uint32_t length) {
        while (length) {
            uint32_t take = std::min(length, BufferSize - filled);
            input.read(first, buffer.data() + filled, take);
            first += take;
            pending -= take;
            filled += take;
            length -= take;
            if (filled == BufferSize) {
                final.payload->write(final.payload->size, buffer.data(), filled);
                filled = 0;
            }
        }
    };
    progress("Writing cache", 0, rom.size, true);
    for (uint32_t number = 0; number < rom.count; number++) {
        Extent item = rom.at(number);
        if (item.kind != Kind::Patch)
            final.append(item.kind, item.source, item.length);
        else {
            if (pending && item.source != first + pending)
                transfer(pending);
            if (!pending)
                first = item.source;
            final.append(Kind::Patch, final.payload->size + filled + pending, item.length);
            pending += item.length;
            while (pending >= BufferSize - filled) {
                transfer(BufferSize - filled);
                progress("Writing cache", item.start + item.length - std::min(pending, item.length), rom.size, true);
            }
        }
        progress("Writing cache", item.start + item.length, rom.size, true);
    }
    transfer(pending);
    if (filled)
        final.payload->write(final.payload->size, buffer.data(), filled);
    final.finish();
    final.payload->sync();
    final.table->sync();
}

std::vector<std::string> listPatches(const std::string& root) {
    makeDirectory(root);
    makeDirectory(root + "/patches");
    DIR* directory = opendir((root + "/patches").c_str());
    require(directory, "Cannot open patches directory");
    std::vector<std::string> result;
    while (dirent* entry = readdir(directory)) {
        std::string name = entry->d_name;
        std::string extension = name.size() >= 4 ? normalize(name.substr(name.size() - 4)) : "";
        if (extension == ".xzp" || extension == ".zip" || isDeltaPatch(name))
            result.push_back(root + "/patches/" + name);
    }
    require(closedir(directory) == 0, "Cannot close patches directory");
    std::sort(result.begin(), result.end());
    return result;
}

Prepared prepare(SourcePtr card, const std::string& path, const std::string& root, bool dsOnly, bool skipRomMd5,
                 PreparationConfirm confirm) {
    require(path.compare(0, root.size() + 9, root + "/patches/") == 0 && path.find("/../") == std::string::npos,
            "Place patches in nitro-patcher/patches");
    makeDirectory(root);
    makeDirectory(root + "/cache");
    makeDirectory(root + "/config");
    makeDirectory(root + "/config/cards");
    auto rawHeader = readBytes(card, 0, 512);
    std::string identity = digest(rawHeader.data(), rawHeader.size(), true);
    std::string cardKey = identity + "-" + std::to_string(card->size);
    std::string romMd5;
    auto patch = std::make_shared<Disk>(path);
    logPreparation("CARD identity=" + identity + " md5=" + romMd5 + " size=" + std::to_string(card->size));
    std::string patchCrc = digest(patch, true, "Reading patch");
    patch.reset();
    std::unique_ptr<Package> archive;
    if (!isDeltaPatch(path)) {
        archive = std::make_unique<Package>(path);
        if (!archive->metadata.error.empty())
            return {"", nullptr, false, archive->metadata.error, false};
    }
    bool hasMd5 = archive && archive->has("md5.txt");
    bool checkMd5 = hasMd5 && !skipRomMd5, dsCache = false;
    if (dsOnly) {
        auto directory = root + "/cache/" + cardKey + "/" + patchCrc;
        if (!complete(directory + "/ready", patchCrc, identity, card->size, checkMd5) &&
            !complete(directory + "/pending", patchCrc, identity, card->size, checkMd5)) {
            if (isDeltaPatch(path) || archive->hasPreprocessing())
                return {"", nullptr, false, DsModeDeltaError, false};
            cardKey += "-ds";
            dsCache = true;
        }
    }
    makeDirectory(root + "/cache/" + cardKey);
    std::string directory = root + "/cache/" + cardKey + "/" + patchCrc;
    logPreparation("CACHE directory=" + directory + " patchCrc32=" + patchCrc);
    makeDirectory(directory);
    std::string ready = directory + "/ready", pending = directory + "/pending";
    progress("Looking for cache", 0, 0);
    bool reused = complete(ready, patchCrc, identity, card->size, checkMd5, !dsCache);
    logPreparation(std::string("CACHE ready complete=") + (reused ? "true" : "false"));
    if (!reused && complete(pending, patchCrc, identity, card->size, checkMd5, !dsCache)) {
        removeTree(ready);
        logPreparation("CACHE recover begin " + pending);
        require(std::rename(pending.c_str(), ready.c_str()) == 0, "Cannot recover completed cache");
        logPreparation("CACHE recover done " + ready);
        reused = true;
    }
    if (reused)
        progress("Using prepared cache", 0, 0);
    if (!reused) {
        if (confirm && !confirm())
            return {"", nullptr, false, "", false, true};
        if (dsOnly && checkMd5)
            return {"", nullptr, false, DsModeMd5Error, false};
        progress("Planning patch preparation", 0, 0);
        progress("Removing unfinished files", 0, 0);
        removeTree(pending);
        makeDirectory(pending);
        std::string patchId;
        std::string error;
        bool mismatch = false;
        bool matchesCard = [&]() {
            Workspace work(pending + "/work", card, dsOnly);
            std::shared_ptr<Map> virtualRom;
            if (isDeltaPatch(path))
                virtualRom = applyDelta(work, card, path, path.substr(path.find_last_of('/') + 1));
            else {
                Package& package = *archive;
                package.work = &work;
                if ((!skipRomMd5 || package.hasPreprocessing()) && package.needsMd5())
                    romMd5 = cardDigest(card, root, identity);
                if (!skipRomMd5 && package.has("md5.txt")) {
                    auto bytes = package.bytes("md5.txt");
                    std::string content(bytes.begin(), bytes.end());
                    bool matched = false;
                    for (size_t first = 0; first <= content.size();) {
                        size_t end = content.find('\n', first);
                        if (end == std::string::npos)
                            end = content.size();
                        std::string line = content.substr(first, end - first);
                        size_t begin = line.find_first_not_of(" \t\r\n");
                        if (begin != std::string::npos && line[begin] != '#') {
                            line = line.substr(begin, line.find_last_not_of(" \t\r\n") - begin + 1);
                            require(line.size() == 32, "Invalid md5.txt in XZP");
                            if (normalize(line) == romMd5) {
                                matched = true;
                                break;
                            }
                        }
                        if (end == content.size())
                            break;
                        first = end + 1;
                    }
                    if (!matched) {
                        error = "XZP does not match this Game Card (MD5 mismatch)";
                        mismatch = true;
                        return false;
                    }
                    logPreparation("MD5 matched " + romMd5);
                }
                patchId = package.metadata.id;
                SourcePtr original = card;
                std::string preprocessing = "preprocessing/" + romMd5 + ".xdelta";
                if (package.has(preprocessing))
                    original = package.delta(preprocessing, original, "Game preprocessing");
                if (!original) {
                    error = work.error;
                    return false;
                }
                virtualRom = buildRom(work, original, package);
            }
            if (!virtualRom) {
                error = work.error;
                return false;
            }
            auto payload = std::make_shared<Disk>(pending + "/data.bin", "w+b");
            Map final(pending + "/map.bin", payload, card, true);
            writeCache(*virtualRom, final);
            auto metadata = object();
            cJSON_AddNumberToObject(metadata.get(), "format", FormatVersion);
            cJSON_AddNumberToObject(metadata.get(), "rules", RulesVersion);
            cJSON_AddStringToObject(metadata.get(), "romMd5", romMd5.c_str());
            cJSON_AddBoolToObject(metadata.get(), "romMd5Checked", !skipRomMd5 || !hasMd5);
            cJSON_AddStringToObject(metadata.get(), "patchCrc32", patchCrc.c_str());
            cJSON_AddStringToObject(metadata.get(), "patchId", patchId.c_str());
            cJSON_AddStringToObject(metadata.get(), "cardIdentity", identity.c_str());
            cJSON_AddNumberToObject(metadata.get(), "cardSize", card->size);
            cJSON_AddNumberToObject(metadata.get(), "romSize", final.size);
            cJSON_AddNumberToObject(metadata.get(), "mapSize", final.table->size);
            cJSON_AddNumberToObject(metadata.get(), "dataSize", final.payload->size);
            writeJson(pending + "/manifest.json", metadata.get());
            return true;
        }();
        if (!matchesCard) {
            removeTree(pending);
            return {"", nullptr, false, error, mismatch};
        }
        progress("Removing temporary files", 0, 0);
        removeTree(pending + "/work");
        {
            Disk marker(pending + "/complete", "wb");
            marker.sync();
        }
        removeTree(ready);
        logPreparation("CACHE publish begin " + pending);
        require(std::rename(pending.c_str(), ready.c_str()) == 0, "Cannot publish prepared cache");
        logPreparation("CACHE publish done " + ready);
    }
    auto metadata = readJson(ready + "/manifest.json");
    std::string patchId = text(metadata.get(), "patchId");
    if (patchId.empty())
        patchId = normalize(path.substr(path.find_last_of('/') + 1));
    progress("Finishing cache", 0, 0);
    updatePatchIndex(root, cardKey, patchCrc, patchId);
    auto payload = std::make_shared<Disk>(ready + "/data.bin");
    auto rom = std::make_shared<Map>(ready + "/map.bin", payload, card, false);
    require(number(metadata.get(), "romSize", rom->size), "Invalid cached ROM length");
    return {ready, rom, reused};
}

Prepared prepare(SourcePtr card, const std::vector<std::string>& paths, const std::string& root, bool dsOnly,
                 bool skipRomMd5, PreparationConfirm confirm) {
    require(!paths.empty(), "No patches selected");
    if (paths.size() == 1)
        return prepare(card, paths.front(), root, dsOnly, skipRomMd5, confirm);
    makeDirectory(root + "/cache");
    makeDirectory(root + "/config");
    auto rawHeader = readBytes(card, 0, 512);
    std::string identity = digest(rawHeader.data(), rawHeader.size(), true);
    makeDirectory(root + "/config/cards");
    std::string cardKey = identity + "-" + std::to_string(card->size);
    std::string romMd5;
    std::string hashes = "np3d-chain-v1\n", names;
    bool hasMd5 = false, dsCache = false;
    for (const auto& path : paths) {
        require(path.compare(0, root.size() + 9, root + "/patches/") == 0 && path.find("/../") == std::string::npos,
                "Place patches in nitro-patcher/patches");
        hashes += digest(std::make_shared<Disk>(path), true, "Reading patch") + "\n";
        names += path.substr(path.find_last_of('/') + 1) + "\n";
        if (!isDeltaPatch(path)) {
            Package package(path);
            if (!package.metadata.error.empty())
                return {"", nullptr, false, package.metadata.error, false};
            hasMd5 = hasMd5 || package.has("md5.txt");
        }
    }
    std::string patchCrc = digest(hashes.data(), hashes.size(), true);
    bool checkMd5 = hasMd5 && !skipRomMd5;
    if (dsOnly) {
        auto directory = root + "/cache/" + cardKey + "/" + patchCrc;
        if (!complete(directory + "/ready", patchCrc, identity, card->size, checkMd5) &&
            !complete(directory + "/pending", patchCrc, identity, card->size, checkMd5)) {
            for (const auto& path : paths) {
                if (isDeltaPatch(path))
                    return {"", nullptr, false, DsModeDeltaError, false};
                Package package(path);
                if (package.hasPreprocessing())
                    return {"", nullptr, false, DsModeDeltaError, false};
            }
            cardKey += "-ds";
            dsCache = true;
        }
    }
    makeDirectory(root + "/cache/" + cardKey);
    std::string directory = root + "/cache/" + cardKey + "/" + patchCrc;
    makeDirectory(directory);
    std::string ready = directory + "/ready", pending = directory + "/pending";
    progress("Looking for cache", 0, 0);
    bool reused = complete(ready, patchCrc, identity, card->size, checkMd5, !dsCache);
    if (!reused && complete(pending, patchCrc, identity, card->size, checkMd5, !dsCache)) {
        removeTree(ready);
        require(std::rename(pending.c_str(), ready.c_str()) == 0, "Cannot recover completed cache");
        reused = true;
    }
    if (reused)
        progress("Using prepared cache", 0, 0);
    if (!reused) {
        if (confirm && !confirm())
            return {"", nullptr, false, "", false, true};
        if (dsOnly && checkMd5)
            return {"", nullptr, false, DsModeMd5Error, false};
        progress("Planning patch preparation", 0, 0);
        removeTree(pending);
        makeDirectory(pending);
        std::string error;
        bool mismatch = false;
        bool matched = [&]() {
            Workspace work(pending + "/work", card, dsOnly);
            SourcePtr input = card;
            for (size_t index = 0; index < paths.size(); index++) {
                progress("Patch sequence", index + 1, paths.size(), false, paths[index].c_str());
                if (isDeltaPatch(paths[index])) {
                    input =
                        applyDelta(work, input, paths[index], paths[index].substr(paths[index].find_last_of('/') + 1));
                    if (!input) {
                        error = work.error;
                        return false;
                    }
                    continue;
                }
                Package package(paths[index], work);
                std::string inputMd5;
                if ((!skipRomMd5 || package.hasPreprocessing()) && package.needsMd5()) {
                    if (index)
                        inputMd5 = digest(input, false, "Checking game data");
                    else
                        inputMd5 = romMd5 = cardDigest(card, root, identity);
                }
                if (!skipRomMd5 && package.has("md5.txt")) {
                    auto bytes = package.bytes("md5.txt");
                    std::string content(bytes.begin(), bytes.end());
                    bool found = false;
                    for (size_t first = 0; first <= content.size();) {
                        size_t end = content.find('\n', first);
                        if (end == std::string::npos)
                            end = content.size();
                        std::string line = content.substr(first, end - first);
                        size_t begin = line.find_first_not_of(" \t\r\n");
                        if (begin != std::string::npos && line[begin] != '#') {
                            line = line.substr(begin, line.find_last_not_of(" \t\r\n") - begin + 1);
                            require(line.size() == 32, "Invalid md5.txt in XZP");
                            if (normalize(line) == inputMd5) {
                                found = true;
                                break;
                            }
                        }
                        if (end == content.size())
                            break;
                        first = end + 1;
                    }
                    if (!found) {
                        error = "XZP does not match the current input in the selected patch sequence (MD5 mismatch)";
                        mismatch = true;
                        return false;
                    }
                }
                std::string preprocessing = "preprocessing/" + inputMd5 + ".xdelta";
                if (package.has(preprocessing))
                    input = package.delta(preprocessing, input, "Game preprocessing");
                if (!input) {
                    error = work.error;
                    return false;
                }
                input = buildRom(work, input, package);
                if (!input) {
                    error = work.error;
                    return false;
                }
            }
            auto virtualRom = std::static_pointer_cast<Map>(input);
            auto payload = std::make_shared<Disk>(pending + "/data.bin", "w+b");
            Map final(pending + "/map.bin", payload, card, true);
            writeCache(*virtualRom, final);
            auto metadata = object();
            cJSON_AddNumberToObject(metadata.get(), "format", FormatVersion);
            cJSON_AddNumberToObject(metadata.get(), "rules", RulesVersion);
            cJSON_AddStringToObject(metadata.get(), "romMd5", romMd5.c_str());
            cJSON_AddBoolToObject(metadata.get(), "romMd5Checked", !skipRomMd5 || !hasMd5);
            cJSON_AddStringToObject(metadata.get(), "patchCrc32", patchCrc.c_str());
            cJSON_AddStringToObject(metadata.get(), "cardIdentity", identity.c_str());
            cJSON_AddNumberToObject(metadata.get(), "cardSize", card->size);
            cJSON_AddNumberToObject(metadata.get(), "romSize", final.size);
            cJSON_AddNumberToObject(metadata.get(), "mapSize", final.table->size);
            cJSON_AddNumberToObject(metadata.get(), "dataSize", final.payload->size);
            writeJson(pending + "/manifest.json", metadata.get());
            return true;
        }();
        if (!matched) {
            removeTree(pending);
            return {"", nullptr, false, error, mismatch};
        }
        removeTree(pending + "/work");
        {
            Disk marker(pending + "/complete", "wb");
            marker.sync();
        }
        removeTree(ready);
        require(std::rename(pending.c_str(), ready.c_str()) == 0, "Cannot publish prepared cache");
    }
    updatePatchIndex(root, cardKey, patchCrc, "chain:" + names);
    auto metadata = readJson(ready + "/manifest.json");
    auto payload = std::make_shared<Disk>(ready + "/data.bin");
    auto rom = std::make_shared<Map>(ready + "/map.bin", payload, card, false);
    require(number(metadata.get(), "romSize", rom->size), "Invalid cached ROM length");
    return {ready, rom, reused};
}

}
