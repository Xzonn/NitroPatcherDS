#include <nds.h>
#include <fat.h>
#include <nds/arm9/dldi.h>
#include "common/nitrofs.h"
#include <cstdio>
#include <cstring>
#if NP3D_DIAGNOSTIC
#include <sys/stat.h>
#endif
#include "launch_engine.h"
#include "card_source.h"
#include "progress.h"
#include "environment.h"
#include "patched_launch.h"
#include "ui.h"
#include "i18n.h"
#include "FontGraphic.h"
#include "app_path.h"

bool scfgUnlock = false;
int TWLMODE = 0;
bool TWLCLK = false;
int TWLVRAM = 0;
bool TWLTOUCH = false;
bool soundFreq = false;
bool sleepMode = true;
bool runCardEngine = false;
#if NP3D_DIAGNOSTIC
unsigned long diagnosticCluster = 0;
#endif
bool EnableSD = true;
int language = -1;

static void stopWithMessage(const char* message) {
    if (ui::ready()) ui::fatal(message);
    consoleDemoInit();
    iprintf("\n%s\n",message);
    while (true) swiWaitForVBlank();
}

#if NP3D_DIAGNOSTIC
static void prepareDiagnostic() {
    np3d::makeDirectory("sd:/nitro-patcher/logs");
    FILE* output=std::fopen("sd:/nitro-patcher/logs/NP3DTRACE.BIN","wb");
    np3d::require(output,"Unable to create NP3DTRACE.BIN.");
    static const unsigned char empty[4096]={};
    for (unsigned int offset=0;offset<0x408000;offset+=sizeof(empty))
        np3d::require(std::fwrite(empty,1,sizeof(empty),output)==sizeof(empty),"Unable to allocate NP3DTRACE.BIN.");
    np3d::require(std::fclose(output)==0,"Unable to close NP3DTRACE.BIN.");
    struct stat info;
    np3d::require(stat("sd:/nitro-patcher/logs/NP3DTRACE.BIN",&info)==0 && info.st_size==0x408000 && info.st_ino>=2,
        "Unable to locate NP3DTRACE.BIN.");
    diagnosticCluster=info.st_ino;runCardEngine=true;
}
#endif

static bool resetGameCard() {
    sysSetCardOwner(BUS_OWNER_ARM7);
    fifoSendValue32(FIFO_USER_08,1);
    fifoWaitValue32(FIFO_USER_08);
    bool ready=fifoGetValue32(FIFO_USER_08)==0;
    for (int frame=0;frame<30;frame++) swiWaitForVBlank();
    sysSetCardOwner(BUS_OWNER_ARM9);
    return ready;
}

#if !NP3D_SLOT2
[[noreturn]] static void restartFromUnlaunch(const std::string& filename) {
    std::u16string path=FontGraphic::utf8to16("sdmc:"+filename.substr(3));
    np3d::require(path.size()<0x104,"App path is too long for Unlaunch.");

    // Use TWiLight Menu++'s Unlaunch autoload layout.
    std::memcpy((void*)0x02000800,"AutoLoadInfo",12);
    *(u16*)0x0200080C=0x3F0;
    *(u16*)0x0200080E=0;
    *(u32*)0x02000810=BIT(0)|BIT(1);
    *(u16*)0x02000814=0x7FFF;
    *(u16*)0x02000816=0x7FFF;
    std::memset((void*)0x02000818,0,0x20+0x208+0x1C0);
    std::memcpy((void*)0x02000838,path.data(),path.size()*sizeof(char16_t));
    *(u16*)0x0200080E=swiCRC16(0xFFFF,(void*)0x02000810,0x3F0);
    DC_FlushAll();
    fifoSendValue32(FIFO_USER_04,0x554E4C41);
    while (true) swiWaitForVBlank();
}
#endif

int main(int argc,char** argv) {
    np3d::setFatalHandler(stopWithMessage);
    fifoSendValue32(FIFO_PM,PM_REQ_SLEEP_DISABLE);
#if NP3D_SLOT2
    (void)argc;(void)argv;
    np3d::require(!isDSiMode() && (dldiGetInternal()->features & FEATURE_SLOT_GBA),
        "Launch this build in DS mode from a Slot-2 flashcard.");
    np3d::require(fatMountSimple("sd",dldiGetInternal()),"Unable to access Slot-2 storage.");
    np3d::require(bootFSInit("sd:/BOOT.NDS"),"Cannot open BOOT.NDS embedded resources.");
#else
    if (!isDSiMode()) stopWithMessage("Launch in DSi mode on a DSi or 3DS.");
#endif
    fifoWaitValue32(FIFO_USER_05);
    int consoleModel=fifoGetValue32(FIFO_USER_05);
    fifoWaitValue32(FIFO_USER_06);
    uint32_t arm7Scfg=fifoGetValue32(FIFO_USER_06);
#if !NP3D_SLOT2
    std::string filename=np3d::appPath(argc,argv);
    if (consoleModel==0 && arm7Scfg==0) restartFromUnlaunch(filename);
#else
    (void)arm7Scfg;
#endif
    if (isDSiMode() && !fatInitDefault()) stopWithMessage("Unable to access the SD card.");
#if !NP3D_SLOT2
    np3d::require(bootFSInit(filename.c_str()),"Cannot open app resources.");
#endif
    ui::showStartup();
    np3d::makeDirectory("sd:/nitro-patcher");
    ui::initialize();
    ui::chooseInitialLanguage();

    alignas(4) unsigned char header[0x200];
    bool hasCard=false, cardInterfaceReady=false, reinsert=false, readCard=true;
    const auto* gameHeader=reinterpret_cast<const sNDSHeader*>(header);
    np3d::SourcePtr persistentCard;
    uint32_t persistentChipId=0;

    while (true) {
        swiWaitForVBlank();
        bool automatic=readCard;
        ui::Action action=automatic?ui::Action::ReadCard:ui::update();
        readCard=false;
        if (action==ui::Action::None) continue;
        if (action==ui::Action::Report) {
            ui::report(true);dumpEnvironment();ui::report(false);continue;
        }
        if (action==ui::Action::RepairStorage) {ui::repairStorage();continue;}
        if (action==ui::Action::ChangeCard) {
            persistentCard.reset();persistentChipId=0;hasCard=false;reinsert=true;
            readCard=true;continue;
        }
        if (action==ui::Action::ReadCard) {
            persistentCard.reset();persistentChipId=0;hasCard=false;
            if (isDSiMode()) {
                if (REG_SCFG_MC & BIT(0)) {ui::noCard(!automatic,reinsert);continue;}
                if (!cardInterfaceReady) {
                    fifoSendValue32(FIFO_USER_02,1);
                    fifoSendValue32(FIFO_USER_07,1);
                    fifoSendValue32(FIFO_USER_01,1);
                    fifoWaitValue32(FIFO_USER_03);
                    for (int frame=0;frame<30;frame++) swiWaitForVBlank();
                    sysSetCardOwner(BUS_OWNER_ARM9);cardInterfaceReady=true;
                } else if (!resetGameCard()) {
                    ui::noCard(true,reinsert);ui::error("Cannot reset the Game Card");continue;
                }
            } else sysSetCardOwner(BUS_OWNER_ARM9);
            std::memset(header,0xff,sizeof(header));
            cardReadHeader(header);
            if (swiCRC16(0xffff,header,0x15e)!=gameHeader->headerCRC16) {
                ui::noCard(!automatic,reinsert);continue;
            }
            if (gameHeader->unitCode==3) {
                ui::noCard(true,reinsert);ui::error("This Game Card requires DSi mode.");continue;
            }
#if NP3D_DIAGNOSTIC
            np3d::require(std::memcmp(gameHeader->gameCode,"IPGJ",4)==0 && gameHeader->headerCRC16==0x3c60,
                "This diagnostic build requires IPGJ-3C60.");
#endif
            std::string error;
            auto card=openGameCard(&persistentChipId,error);
            if (!card) {persistentChipId=0;ui::noCard(true,reinsert);ui::error(error);continue;}
            hasCard=ui::setGame(card,*gameHeader);reinsert=false;
            if (hasCard && !isDSiMode()) persistentCard=card;
            if (!hasCard) persistentChipId=0;
            continue;
        }
        if (!hasCard) continue;
        if (isDSiMode() && !resetGameCard()) {
            persistentCard.reset();persistentChipId=0;hasCard=false;reinsert=true;
            ui::noCard(true,true);ui::error("Cannot reset the Game Card");continue;
        }
        alignas(4) unsigned char currentHeader[0x200];
        if (persistentCard) {
            if (!gameCardMatches(persistentCard)) {
                persistentCard.reset();persistentChipId=0;hasCard=false;reinsert=true;
                ui::noCard(true,true);continue;
            }
        } else {
            cardReadHeader(currentHeader);
            if (std::memcmp(header,currentHeader,sizeof(header))!=0) {
                hasCard=false;reinsert=true;ui::noCard(true,true);continue;
            }
        }
        if ((action==ui::Action::Launch && !ui::selectedPatches().empty()) || !isDSiMode()) {
            auto paths=action==ui::Action::Launch?ui::selectedPatches():std::vector<std::string>();
            if (!isDSiMode() && gameHeader->unitCode==2 && !paths.empty()) {
                ui::error(ui::tr(ui::Text::DSiModeRequired));continue;
            }
            std::string description;
            for (const auto& path : paths) {if (!description.empty()) description+=" -> ";description+=path;}
            np3d::beginPreparationLog("sd:/nitro-patcher",description);
            beginPatchProgress();
            bool reused=false;
            bool mismatch=false;
            std::string error;
            {
                uint32_t chipId=persistentChipId;
                auto card=persistentCard?persistentCard:openGameCard(&chipId,error);
                if (!card) {
                    np3d::setProgressHandler(nullptr);np3d::finishPreparationLog(false,error);
                    persistentCard.reset();persistentChipId=0;hasCard=false;
                    ui::noCard(true);ui::error(error);continue;
                }
                planPatchProgress(card,paths);
                np3d::Prepared prepared;
                if (paths.empty()) {
                    prepared.directory="sd:/nitro-patcher/runtime/original";
                    np3d::makeDirectory("sd:/nitro-patcher/runtime");
                    np3d::makeDirectory(prepared.directory);
                    auto payload=std::make_shared<np3d::Disk>(prepared.directory+"/data.bin","w+b");
                    prepared.rom=std::make_shared<np3d::Map>(prepared.directory+"/map.bin",payload,card,true);
                    prepared.rom->append(np3d::Kind::Card,0,card->size);
                    prepared.rom->finish();payload->sync();
                } else prepared=np3d::prepare(card,paths,"sd:/nitro-patcher");
                error=prepared.error;reused=prepared.reused;mismatch=prepared.mismatch;
                if (error.empty()) {writePatchedLaunch(prepared,card,chipId,consoleModel,ui::apPatchEnabled());finishPatchProgress(reused);}
                else np3d::setProgressHandler(nullptr);
            }
            np3d::finishPreparationLog(reused,error);
            if (!error.empty()) {ui::error(error,mismatch);continue;}
            startPatchedGame();
        }
#if NP3D_DIAGNOSTIC
        prepareDiagnostic();
#endif
        auto settings=gameLaunchSettings(isDSiMode(),gameHeader->unitCode,gameHeader->gameCode,ui::languageCodes[ui::currentLanguage()]);
        TWLMODE=settings.dsiMode;TWLCLK=settings.boostCpu;
        if (isDSiMode() && !TWLCLK && (gameHeader->unitCode==0 || !TWLMODE)) {
            fifoSendValue32(FIFO_USER_04,1);REG_SCFG_CLK=0x80;swiWaitForVBlank();
        }
        const bool browser=std::memcmp(gameHeader->gameCode,"UBRP",4)==0;
        const bool alternate=browser || std::memcmp(gameHeader->gameCode,"AMFE",4)==0 || std::memcmp(gameHeader->gameCode,"ALXX",4)==0;
        runLaunchEngine(alternate,browser);
    }
}
