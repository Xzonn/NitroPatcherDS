#pragma once
#include <string>
#include <vector>
#include "fmt/format.h"

namespace ui {
enum class Text {
    Choose, Start, Toggle, Menu, Language, Back, Select, Info, Intro, Name, Author, Homepage, Version, Beta,
    NoMetadata, NoReadme, About, License, Components, Preparing, Elapsed, Working, Reused,
    Mismatch, MismatchBody, Original, Report, ReportWorking, ReportDone, Empty, EmptyBody, NoCard, NoCardBody,
    Error, ErrorBody, Details, FatalBody, Loading, ChangedCard,
    ReadHeader, InitCard, SecureRead, SecureCheck, ReadCard, ReadPatch, FindCache, RemoveUnfinished,
    PatchDirectory, Extract, Preprocess, ReadDirectory, PrepareFiles, Arrange, Apply, Replace, WriteCache,
    RemoveTemporary, FinishCache, CheckCache, CheckData, PrepareDSi, Sequence, Launch, SelectLanguage,
    AboutAuthor, AboutVersion, KeyHint, Counter, Percentage, FileCounter, Remaining, Estimating, PrepareLaunch, Licenses,
    InsertCard, ReadCardPrompt, ReinsertCardPrompt, IdentifyCard, ChangeCard,
    StorageTitle, StorageConfirm, StorageNotice, StorageWriting, StorageDirectories, StorageAllocation,
    StorageCompare, StorageBackup, StorageRepair, StorageVerify, StorageCopy, StorageDifferences,
    StorageFsInfo, StorageClean, StorageReady, StorageDamaged, StorageAmbiguous, StorageUnsupported,
    StorageBusy, StorageMemory, StorageIoError, StorageRepaired, StorageBackupPath, CheckDSiData, DSModeXdelta, ApPatch, Enabled, Disabled, DSModeNotice, SkipRomMd5, DSModeMd5Required, LimitOpenFiles, Unlimited, Count
};
extern int LanguageCount;
extern std::vector<std::string> languageCodes;
extern std::vector<std::string> languageNames;
void loadLanguages();
const char* tr(Text key);
const char* tr(const std::string& language, Text key);
template<typename... Args> std::string format(Text key,Args&&... args) {
    return fmt::vformat(tr(key),fmt::make_format_args(args...));
}
void setLanguage(int index);
int currentLanguage();
int languageIndex(const std::string& code);
const char* progressText(const char* stage);
}
