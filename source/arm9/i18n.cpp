#include "i18n.h"
#include "resources.h"
#include "../patch/core.h"
#include "cJSON.h"
#include <cstring>
#include <map>

namespace ui {
int LanguageCount = 0;
std::vector<std::string> languageCodes, languageNames;
namespace {
int language = 0;
std::vector<std::map<std::string,std::string>> catalogs;
const char* const keys[] = {
    "patch_list.title","patch_list.start","patch_list.toggle","menu.title","language_selection.title","common.back","language_selection.confirm",
    "patch_list.show_info","patch_info.description","patch_info.name","patch_info.author","patch_info.homepage","patch_info.version","patch_info.beta_version",
    "patch_info.no_metadata","patch_info.no_description","about.title","about.license","about.components","prepare.title","prepare.elapsed","prepare.working","prepare.cache_reused",
    "patch_error.mismatch.title","patch_error.mismatch.body","menu.launch_original","menu.environment_report","environment_report.preparing","environment_report.saved",
    "patch_list.empty.title","patch_list.empty.body","game_card.missing.title","game_card.missing.body","error.title","error.body","error.details","error.fatal_body",
    "game_card.loading","game_card.changed","prepare.stage.read_card_header","prepare.stage.initialize_card","prepare.stage.read_secure_area","prepare.stage.check_secure_area",
    "prepare.stage.read_card","prepare.stage.read_patch","prepare.stage.find_cache","prepare.stage.remove_unfinished","prepare.stage.read_patch_directory","prepare.stage.extract",
    "prepare.stage.preprocess","prepare.stage.read_game_directory","prepare.stage.prepare_files","prepare.stage.arrange_files","prepare.stage.apply_patch","prepare.stage.replace_file",
    "prepare.stage.write_cache","prepare.stage.remove_temporary","prepare.stage.finish_cache","prepare.stage.check_cache","prepare.stage.check_game_data","prepare.stage.prepare_dsi",
    "prepare.stage.patch_sequence","launch.starting","language_selection.prompt","about.author","about.version","common.key_hint","common.item_counter","common.percentage",
    "prepare.file_counter","prepare.remaining","prepare.estimating","prepare.stage.launch_files","about.licenses",
    "game_card.insert.title","game_card.insert.body","game_card.reinsert.body","game_card.read","menu.change_card",
    "storage_repair.title","storage_repair.confirm","storage_repair.notice","storage_repair.writing",
    "storage_repair.stage.directories","storage_repair.stage.allocation","storage_repair.stage.compare",
    "storage_repair.stage.backup","storage_repair.stage.repair","storage_repair.stage.verify",
    "storage_repair.copy","storage_repair.differences","storage_repair.fsinfo","storage_repair.clean",
    "storage_repair.ready","storage_repair.damaged","storage_repair.ambiguous","storage_repair.unsupported",
    "storage_repair.busy","storage_repair.memory","storage_repair.io_error","storage_repair.repaired","storage_repair.backup_path","prepare.stage.check_dsi_data","patch_error.ds_mode_xdelta",
    "menu.ap_patch","common.enabled","common.disabled","prepare.ds_mode_notice","menu.skip_rom_md5","patch_error.ds_mode_md5_required","menu.limit_open_files","common.unlimited",
    "cheats.title","cheats.match.patched","cheats.match.original","cheats.database_missing","cheats.not_found","cheats.selected_bytes","cheats.no_description","cheats.selection_changed","cheats.too_large","cheats.clear_all","cheats.invalid_database","cheats.prepare_confirmation",
    "menu.update_display_while_reading","common.automatic"
};
static_assert(sizeof(keys)/sizeof(keys[0])==int(Text::Count));
using Json = std::unique_ptr<cJSON,decltype(&cJSON_Delete)>;
Json read(const std::string& path) {
    auto bytes=np3d::readResource(path.c_str());bytes.push_back(0);
    Json json(cJSON_ParseWithLength(reinterpret_cast<const char*>(bytes.data()),bytes.size()),cJSON_Delete);
    np3d::require(cJSON_IsArray(json.get()),"Invalid language file: "+path);
    return json;
}
std::string value(const cJSON* json,const char* key) {
    auto item=cJSON_GetObjectItemCaseSensitive(json,key);
    np3d::require(cJSON_IsString(item),std::string("Missing language field: ")+key);
    return item->valuestring;
}
}
void loadLanguages() {
    catalogs.clear();languageCodes.clear();languageNames.clear();
    auto manifest=read("languages/languages.json");
    for (auto item=manifest->child;item;item=item->next) {
        languageCodes.push_back(value(item,"code"));languageNames.push_back(value(item,"name"));
        auto json=read("languages/"+value(item,"code")+".json");
        std::map<std::string,std::string> catalog;
        for (auto record=json->child;record;record=record->next) catalog[value(record,"key")]=value(record,"translation");
        catalogs.push_back(std::move(catalog));
    }
    LanguageCount=catalogs.size();np3d::require(LanguageCount>0,"No embedded interface languages");
}
const char* tr(Text key) {
    auto found=catalogs.at(language).find(keys[int(key)]);
    np3d::require(found!=catalogs.at(language).end(),std::string("Missing translation: ")+keys[int(key)]);
    return found->second.c_str();
}
const char* tr(const std::string& code,Text key) {
    int index=languageIndex(code);np3d::require(index>=0,"Missing interface language: "+code);
    auto found=catalogs[index].find(keys[int(key)]);
    np3d::require(found!=catalogs[index].end(),std::string("Missing translation: ")+keys[int(key)]);
    return found->second.c_str();
}
void setLanguage(int index) { language=index; }
int currentLanguage() { return language; }
int languageIndex(const std::string& code) {
    for (int index=0;index<LanguageCount;index++) if (code==languageCodes[index]) return index;
    return -1;
}
const char* progressText(const char* stage) {
    static const struct { const char* name; Text key; } stages[] = {
        {"Reading Game Card header",Text::ReadHeader}, {"Initializing Game Card",Text::InitCard},
        {"Reading secure area",Text::SecureRead}, {"Checking secure area",Text::SecureCheck},
        {"Reading Game Card",Text::ReadCard}, {"Reading patch",Text::ReadPatch},
        {"Looking for cache",Text::FindCache}, {"Removing unfinished files",Text::RemoveUnfinished},
        {"Reading patch directory",Text::PatchDirectory}, {"Extracting patch file",Text::Extract},
        {"Game preprocessing",Text::Preprocess}, {"Reading game directory",Text::ReadDirectory},
        {"Preparing game files",Text::PrepareFiles}, {"Arranging game files",Text::Arrange},
        {"Applying patch section",Text::Apply}, {"Applying replacement",Text::Replace},
        {"Writing cache",Text::WriteCache}, {"Removing temporary files",Text::RemoveTemporary},
        {"Finishing cache",Text::FinishCache}, {"Checking cache",Text::CheckCache},
        {"Checking game data",Text::CheckData}, {"Preparing DSi files",Text::PrepareDSi}, {"Checking DSi data",Text::CheckDSiData},
        {"Patch sequence",Text::Sequence}, {"Patch cache reused",Text::Reused}, {"Patch cache prepared",Text::FinishCache},
        {"Using prepared cache",Text::Reused}, {"Preparing launch files",Text::PrepareLaunch}
    };
    for (const auto& item : stages) if (std::strcmp(stage,item.name)==0) return tr(item.key);
    return stage;
}
}
