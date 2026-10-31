#include "ui.h"
#include "i18n.h"
#include "resources.h"
#include "storage_repair.h"
#include "../patch/ui_data.h"
#include "FontGraphic.h"
#include "uni_algo/conv.h"
#include <cstdio>
#include <cstring>

namespace ui {
namespace {
constexpr const char* Root = "sd:/nitro-patcher";
constexpr uint16_t White = 0xffff, Black = 0x8000, Gray = 0xe739;
constexpr int Visible = 6;
enum class Page { Home, Language, Menu, PatchInfo, About, Licenses, LicenseText, Progress, Report, Empty, NoCard, Error, Details, Loading, Storage, Confirm };
enum class Command { Toggle, Start, Menu, Info, Back, Language, Original, Report, About, Licenses, ReadLicense, PickLanguage, Previous, Next, Up, Down, Details, ReadCard, ChangeCard, RepairStorage, ApPatch, SkipRomMd5 };
constexpr Text MenuLabels[] = {Text::ApPatch,Text::SkipRomMd5,Text::Original,Text::ChangeCard,Text::Report,Text::Language,Text::StorageTitle,Text::About};
constexpr Command MenuCommands[] = {Command::ApPatch,Command::SkipRomMd5,Command::Original,Command::ChangeCard,Command::Report,Command::Language,Command::RepairStorage,Command::About};
constexpr int MenuCount = sizeof(MenuCommands)/sizeof(MenuCommands[0]);
constexpr const char* Licenses[] = {"NitroPatcherDS.txt", "md4c.txt", "fmt.txt", "Gulim-OFL.txt", "Gulim-AUTHORS.txt", "liblzma.txt"};
constexpr int LicenseCount = sizeof(Licenses)/sizeof(Licenses[0]);
struct Hit { int x, y, width, height; Command command; int argument; };
struct State {
    Page page = Page::Home, languageFrom = Page::Home;
    int focus = 0, top = 0, languageBefore = 0, homeFocus = 0, homeTop = 0, textTop = 0, licenseFocus = 0;
    unsigned int ticks = 0, focusTick = 0;
    bool first = false, hasCard = false, fatal = false, mismatch = false, reportWorking = false;
    bool cardMissing = false, cardReinsert = false;
    bool defaultSkipRomMd5 = false;
    bool storageWorking = false, storageReady = false, storageWriting = false;
    int patchNumber = 1;
    uint32_t completed = 0, total = 0;
    uint64_t elapsed = 0;
    PreparationProgress preparation;
    std::string gameKey, detail, stage, progressDetail;
    np3d::Settings settings;
    np3d::PatchInfo info;
    std::vector<std::string> patches, selected;
    std::vector<std::u16string> lines;
    tNDSBanner banner = {};
    std::array<std::array<uint16_t,128>,8> titles = {};
    bool hasBanner = false;
};
State state;
std::unique_ptr<FontGraphic> font;
std::string fontPath;
std::vector<uint16_t> pixels;
std::vector<Hit> hits;
uint16_t* displays[2] = {};
Action pending = Action::None;

void useLanguage(int index) {
    static const struct { const char* language; const char* resource; } fonts[] = {
        {"ja","fonts/japanese12.nftr"}, {"ko","fonts/korean12.nftr"}
    };
    const char* path="fonts/song12.nftr";
    for (const auto& item : fonts) if (languageCodes[index]==item.language) path=item.resource;
    if (fontPath!=path) {
        font.reset();
        std::string embedded=std::string("boot:/")+path;
        font=std::make_unique<FontGraphic>(std::fopen(embedded.c_str(),"rb"));
        fontPath=path;
    }
    setLanguage(index);
}

std::string filename(const std::string& path) { return path.substr(path.find_last_of('/') + 1); }
int width(const std::u16string& text) {
    int result = 0;
    for (char16_t character : text) result += font->charWidth(character);
    return result;
}
void fill(int screen, int x, int y, int w, int h, uint16_t color) {
    auto target = pixels.data() + screen * 256 * 192;
    for (int row = std::max(0,y); row < std::min(192,y+h); row++)
        std::fill(target + row*256 + std::max(0,x), target + row*256 + std::min(256,x+w), color);
}
void present() {
    DC_FlushRange(pixels.data(),pixels.size()*sizeof(uint16_t));
    dmaCopy(pixels.data(),displays[0],256*192*2);
    dmaCopy(pixels.data()+256*192,displays[1],256*192*2);
}
void startupProgress(int percent) {
    fill(1,33,97,190*percent/100,6,Black);
    present();
}
void text16(int screen, const std::u16string& value, int x, int y, int left = 0, int right = 256) {
    for (char16_t character : value) {
        font->drawGlyph(pixels.data() + screen*256*192, character, x, y, left, right, Black);
        x += font->charWidth(character);
    }
}
void text(int screen, const std::string& value, int x, int y) { text16(screen,una::utf8to16u(value),x,y); }
std::vector<std::u16string> wrap(const std::u16string& value, int maximum) {
    std::vector<std::u16string> lines;
    std::u16string line;
    int used = 0;
    for (char16_t character : value) {
        if (character == u'\r') continue;
        if (character == u'\n') { lines.push_back(line); line.clear(); used=0; continue; }
        int advance = font->charWidth(character);
        if (used+advance>maximum && !line.empty()) { lines.push_back(line); line.clear(); used=0; }
        line += character; used += advance;
    }
    lines.push_back(line);
    return lines;
}
int paragraph(int screen, const std::string& value, int x, int y, int maximum=240) {
    for (const auto& line : wrap(una::utf8to16u(value),maximum)) {
        if (y+12>192) break;
        text16(screen,line,x,y); y+=16;
    }
    return y;
}
void heading(const std::string& value) { text(1,value,8,8); fill(1,0,26,256,1,Black); }
void separator() { fill(1,0,157,256,1,Black); }
void softkey(const std::string& label,int x,int y,int w,Command command,int argument=0) {
    text(1,label,x,y); hits.push_back({x,y-2,w,16,command,argument});
}
std::string hint(const char* key,Text action) {
    return format(Text::KeyHint,fmt::arg("key",key),fmt::arg("action",tr(action)));
}
void backKey() { separator(); softkey(hint("B",Text::Back),8,161,132,Command::Back); }
void scrollText(int screen,const std::string& value,int x,int y,int maximum,bool active) {
    auto characters=una::utf8to16u(value);
    int first=0,last=0,advance=0;
    for (char16_t character : characters) {
        int left=advance+font->charLeft(character);
        first=std::min(first,left);last=std::max(last,left+font->glyphWidth(character));
        advance+=font->charWidth(character);
    }
    int distance=std::max(0,std::max(last,advance)-first-maximum), offset=0;
    if (active && distance) {
        int travel=distance*4;
        int elapsed=(state.ticks-state.focusTick)%(travel*2+120);
        offset=elapsed<60?0:elapsed<60+travel?(elapsed-60)/4:elapsed<120+travel?distance:distance-(elapsed-120-travel)/4;
    }
    text16(screen,characters,x-first-offset,y,x,x+maximum);
}
void save() {
    if (!state.gameKey.empty()) {
        auto& selected=state.settings.games[state.gameKey]; selected.clear();
        for (const auto& patch : state.selected) selected.push_back(filename(patch));
    }
    np3d::saveSettings(Root,state.settings);
}
void go(Page page) {
    state.page=page; state.focus=0; state.top=0; state.textTop=0; state.focusTick=state.ticks;
    if (page==Page::Language) {
        state.focus=currentLanguage();state.top=std::max(0,state.focus-Visible+1);
    }
}
void upper() {
    text(0,"NitroPatcherDS by Xzonn",8,8);
    auto version=una::utf8to16u(NP3D_VERSION);
    text16(0,version,248-width(version),8);
    fill(0,0,26,256,1,Black);
    if (state.first) {
        text(0,tr("zh-hans",Text::SelectLanguage),8,60);
        text(0,tr("ja",Text::SelectLanguage),8,92);
        text(0,tr("en",Text::SelectLanguage),8,124);
    } else if (state.page==Page::Storage) {
        paragraph(0,tr(state.storageWriting?Text::StorageWriting:Text::StorageNotice),8,42);
    } else if (state.page==Page::PatchInfo) {
        scrollText(0,filename(state.patches[state.homeFocus]),8,34,240,true);
        int y=58;
        const auto& info=state.info;
        struct Field {Text key;const char* argument;std::string value;};
        const Field fields[]={
            {Text::Name,"name",info.name}, {Text::Author,"author",info.author},
            {Text::Version,"version",info.beta && !info.version.empty()?format(Text::Beta,fmt::arg("version",info.version)):info.version},
            {Text::Homepage,"homepage",info.homepage}
        };
        for (const auto& item : fields) {
            if (!item.value.empty()) y=paragraph(0,format(item.key,fmt::arg(item.argument,item.value)),8,y);
        }
        if (y==58) text(0,tr(Text::NoMetadata),8,y);
    } else if (state.page==Page::About || state.page==Page::Licenses || state.page==Page::LicenseText) {
        text(0,"NitroPatcherDS",8,44);text(0,format(Text::AboutAuthor,fmt::arg("author","Xzonn")),8,72);
        text(0,format(Text::AboutVersion,fmt::arg("version",NP3D_VERSION)),8,100);
        text(0,format(Text::License,fmt::arg("license","GPL v3")),8,128);
        if (state.page==Page::LicenseText) scrollText(0,Licenses[state.licenseFocus],8,160,240,true);
    } else if (state.hasBanner) {
        for (int y=0;y<32;y++) for (int x=0;x<32;x++) {
            int pixel=((y/8)*4+x/8)*64+(y%8)*8+x%8;
            unsigned int index=(state.banner.icon[pixel/2]>>((pixel%2)*4))&15;
            if (index) pixels[(y+42)*256+x+8]=state.banner.palette[index]|BIT(15);
        }
        static const char* const titleLanguages[]={"ja","en","fr","de","it","es","zh","ko"};
        const auto& code=languageCodes[currentLanguage()];
        int count=state.banner.version>=3?8:(state.banner.version==2?7:6);
        int language=0;
        for (int index=0;index<count;index++)
            if (code==titleLanguages[index] || (index==6 && code.compare(0,3,"zh-")==0)) language=index;
        std::u16string title;
        for (int index=0;index<128 && state.titles[language][index];index++) title+=state.titles[language][index];
        auto lines=wrap(title,200);
        int y=42;
        for (size_t index=0;index<lines.size() && y<148;index++) {
            if (index==2) y+=10;
            text16(0,lines[index],48,y); y+=16;
        }
    }
}
void range(int count) {
    if (count<=Visible) return;
    if (state.top) softkey("<",174,8,14,Command::Previous);
    text(1,format(Text::Counter,fmt::arg("current",state.focus+1),fmt::arg("total",count)),200,8);
    if (state.top+Visible<count) softkey(">",236,8,20,Command::Next);
}
void render() {
    std::fill(pixels.begin(),pixels.end(),White); hits.clear(); upper();
    switch (state.page) {
    case Page::Home:
        heading(tr(Text::Choose)); range(state.patches.size());
        for (int row=0;row<Visible && state.top+row<int(state.patches.size());row++) {
            int index=state.top+row,y=32+row*20;
            if (state.focus==index) fill(1,8,y,240,20,Gray);
            auto found=std::find(state.selected.begin(),state.selected.end(),state.patches[index]);
            std::string order=found==state.selected.end()?"[ ]":"["+std::to_string(found-state.selected.begin()+1)+"]";
            text(1,order,12,y+4);
            scrollText(1,filename(state.patches[index]),36,y+4,208,state.focus==index);
            hits.push_back({8,y,240,20,Command::Toggle,index});
        }
        separator(); softkey(hint("A",Text::Toggle),8,161,132,Command::Toggle,state.focus);
        softkey(hint("START",Text::Start),148,161,108,Command::Start);
        softkey(hint("X",Text::Menu),8,178,76,Command::Menu);
        softkey(hint("Y",Text::Info),90,178,76,Command::Info);
        text(1,"L / R  < >",180,178); break;
    case Page::Language:
        heading(tr(Text::Language)); range(LanguageCount);
        for (int row=0;row<Visible && state.top+row<LanguageCount;row++) {
            int index=state.top+row,y=32+row*20;
            if (index==state.focus) fill(1,8,y,240,20,Gray);
            std::string label=languageNames[index];
            if (!state.first && state.settings.language==languageCodes[index]) label+=" *";
            text(1,label,16,y+4); hits.push_back({8,y,240,20,Command::PickLanguage,index});
        }
        separator(); softkey(hint("A",Text::Select),8,161,132,Command::PickLanguage,state.focus);
        if (!state.first) softkey(hint("B",Text::Back),148,161,108,Command::Back);
        if (LanguageCount>Visible) text(1,"L / R  < >",180,178);
        break;
    case Page::Menu: {
        heading(tr(Text::Menu)); range(MenuCount);
        for (int row=0;row<Visible && state.top+row<MenuCount;row++) {
            int index=state.top+row,y=32+row*20;
            if (state.focus==index) fill(1,8,y,240,20,Gray);
            std::string label=MenuCommands[index]==Command::ApPatch?format(Text::ApPatch,fmt::arg("state",tr(apPatchEnabled()?Text::Enabled:Text::Disabled)))
                :MenuCommands[index]==Command::SkipRomMd5?format(Text::SkipRomMd5,fmt::arg("state",tr(skipRomMd5Enabled()?Text::Enabled:Text::Disabled))):tr(MenuLabels[index]);
            text(1,label,16,y+4); hits.push_back({8,y,240,20,MenuCommands[index],0});
        }
        backKey(); break;
    }
    case Page::PatchInfo: case Page::Details: case Page::LicenseText: case Page::Confirm: {
        heading(tr(state.page==Page::PatchInfo?Text::Intro:state.page==Page::LicenseText?Text::Licenses:Text::Details));
        for (int index=0;index<7 && state.textTop+index<int(state.lines.size());index++) text16(1,state.lines[state.textTop+index],8,34+index*16);
        if (state.lines.size()>7) {
            fill(1,247,32,2,116,Gray);
            int height=std::max(4,int(116*7/state.lines.size()));
            fill(1,247,32+(116-height)*state.textTop/(state.lines.size()-7),2,height,Black);
        }
        separator();
        if (state.page==Page::Confirm) {
            softkey(hint("A",Text::Start),8,161,132,Command::Start);
            softkey(hint("B",Text::Back),148,161,108,Command::Back);
        } else if (!state.fatal) softkey(hint("B",Text::Back),8,161,132,Command::Back);
        int arrowsY=state.page==Page::Confirm?178:161;
        softkey("↑",184,arrowsY,28,Command::Up); softkey("↓",220,arrowsY,28,Command::Down);
        if (state.page!=Page::Confirm) text(1,"L / R  < >",180,178);
        break;
    }
    case Page::About:
        heading(tr(Text::About)); text(1,tr(Text::Components),8,34);
        text(1,"nds-bootstrap",8,54);text(1,"TWiLight Menu++",8,70);text(1,"nitro-patcher / md4c / fmt",8,86);
        paragraph(1,"miniz / cJSON / xdelta / ndstool\nmbed TLS / uni-algo / liblzma",8,106);separator();
        softkey(hint("A",Text::Licenses),8,161,132,Command::Licenses);
        softkey(hint("B",Text::Back),148,161,108,Command::Back);break;
    case Page::Licenses:
        heading(tr(Text::Licenses));range(LicenseCount);
        for (int row=0;row<Visible && state.top+row<LicenseCount;row++) {
            int index=state.top+row,y=32+row*20;
            if (state.focus==index) fill(1,8,y,240,20,Gray);
            scrollText(1,Licenses[index],16,y+4,224,state.focus==index);
            hits.push_back({8,y,240,20,Command::ReadLicense,index});
        }
        separator();softkey(hint("A",Text::Select),8,161,132,Command::ReadLicense,state.focus);
        softkey(hint("B",Text::Back),148,161,108,Command::Back);break;
    case Page::Progress:
        heading(tr(Text::Preparing));text(1,format(Text::Counter,fmt::arg("current",state.patchNumber),fmt::arg("total",state.selected.size())),8,34);
        if (!state.selected.empty()) scrollText(1,filename(state.selected[std::min<size_t>(state.patchNumber-1,state.selected.size()-1)]),8,52,240,true);
        text(1,state.stage,8,76); fill(1,8,96,240,10,Gray);
        if (state.preparation.total) {
            unsigned int percent=std::min<uint64_t>(100,state.preparation.completed*100/state.preparation.total);
            fill(1,8,96,240*percent/100,10,Black);
            auto label=una::utf8to16u(format(Text::Percentage,fmt::arg("percent",percent)));
            text16(1,label,248-width(label),118);
        } else if (!state.preparation.fileCount) text(1,tr(Text::Working),8,118);
        if (state.preparation.fileCount) text(1,format(Text::FileCounter,fmt::arg("current",state.preparation.fileNumber),fmt::arg("total",state.preparation.fileCount)),8,118);
        else if (state.total && !state.preparation.bytes) text(1,format(Text::Counter,fmt::arg("current",state.completed),fmt::arg("total",state.total)),8,118);
        scrollText(1,state.progressDetail,8,138,240,false);
        separator();text(1,format(Text::Elapsed,fmt::arg("seconds",state.elapsed/1000)),8,161);
        text(1,state.preparation.remaining<0?tr(Text::Estimating):format(Text::Remaining,fmt::arg("seconds",(state.preparation.remaining+999)/1000)),8,178);break;
    case Page::Report:
        heading(tr(state.reportWorking?Text::ReportWorking:Text::ReportDone));
        separator();
        if (!state.reportWorking) {paragraph(1,"sd:/nitro-patcher/logs/\nenvironment.log",8,40);backKey();} break;
    case Page::Storage:
        heading(tr(Text::StorageTitle));
        if (state.storageWorking) {
            paragraph(1,state.stage,8,40);
            fill(1,8,96,240,10,Gray);
            unsigned int percent=state.total?std::min<uint64_t>(100,uint64_t(state.completed)*100/state.total):0;
            fill(1,8,96,240*percent/100,10,Black);
            text(1,format(Text::Percentage,fmt::arg("percent",percent)),8,120);
        } else {
            for (int index=0;index<7 && state.textTop+index<int(state.lines.size());index++)
                text16(1,state.lines[state.textTop+index],8,34+index*16);
            separator();
            if (state.storageReady) softkey(hint("A",Text::StorageConfirm),8,161,132,Command::RepairStorage);
            softkey(hint("B",Text::Back),148,161,108,Command::Back);
            if (state.lines.size()>7) {
                softkey("↑",184,178,28,Command::Up);softkey("↓",220,178,28,Command::Down);
                text(1,format(Text::Counter,fmt::arg("current",state.textTop+1),fmt::arg("total",state.lines.size())),8,178);
            }
        }
        break;
    case Page::Loading:
        heading(tr(Text::Loading));separator();break;
    case Page::Empty:
        heading(tr(Text::Empty)); paragraph(1,tr(Text::EmptyBody),8,40);
        text(1,"sd:/nitro-patcher/patches/",8,96); separator();
        softkey(hint("A",Text::Original),8,161,240,Command::Original);
        softkey(hint("X",Text::Menu),8,178,132,Command::Menu); break;
    case Page::NoCard:
        heading(tr(state.cardMissing?Text::NoCard:Text::InsertCard));
        paragraph(1,tr(state.cardReinsert?Text::ReinsertCardPrompt:Text::ReadCardPrompt),8,40);
        if (state.cardMissing) paragraph(1,tr(Text::NoCardBody),8,96);
        separator();
        softkey(hint("A",Text::IdentifyCard),8,161,132,Command::ReadCard);
        softkey(hint("X",Text::Menu),8,178,132,Command::Menu); break;
    case Page::Error:
        heading(tr(state.mismatch?Text::Mismatch:Text::Error));
        paragraph(1,tr(state.fatal?Text::FatalBody:state.mismatch?Text::MismatchBody:Text::ErrorBody),8,40);
        separator(); softkey(hint("A",Text::Details),8,161,132,Command::Details);
        if (!state.fatal) softkey(hint("B",Text::Back),148,161,108,Command::Back);
        break;
    }
    present();
}
void returnHome() { go(!state.hasCard?Page::NoCard:state.patches.empty()?Page::Empty:Page::Home); }
void scrollLines(int distance) { state.textTop=std::clamp(state.textTop+distance,0,std::max(0,int(state.lines.size())-7)); }
void move(int distance,bool page=false) {
    if (state.page==Page::PatchInfo || state.page==Page::Details || state.page==Page::LicenseText) {scrollLines(distance*(page?7:1));return;}
    int count=state.page==Page::Language?LanguageCount:state.page==Page::Menu?MenuCount:state.page==Page::Licenses?LicenseCount:state.page==Page::Home?state.patches.size():0;
    if (!count) return;
    state.focus=page?std::clamp(state.focus+distance*Visible,0,count-1):(state.focus+distance+count)%count;
    if (page) state.top=std::clamp(state.top+distance*Visible,0,std::max(0,count-Visible));
    if (state.focus<state.top) state.top=state.focus;
    if (state.focus>=state.top+Visible) state.top=state.focus-Visible+1;
    if (state.page==Page::Language) useLanguage(state.focus);
    state.focusTick=state.ticks;
}
void command(Command value,int argument=0) {
    if (storage::writeBlocked() && (value==Command::Toggle || value==Command::Start ||
        value==Command::Original || value==Command::Report || value==Command::PickLanguage || value==Command::ApPatch || value==Command::SkipRomMd5)) {
        state.fatal=false;state.mismatch=false;state.detail=tr(Text::StorageIoError);go(Page::Error);return;
    }
    switch (value) {
    case Command::Toggle: {
        if (state.page!=Page::Home) break;
        state.focus=argument;
        auto found=std::find(state.selected.begin(),state.selected.end(),state.patches[argument]);
        if (found==state.selected.end()) state.selected.push_back(state.patches[argument]); else state.selected.erase(found);
        save(); state.focusTick=state.ticks; break;
    }
    case Command::Start: pending=Action::Launch; break;
    case Command::Original: if (state.hasCard) pending=Action::Original; break;
    case Command::ApPatch:
        if (state.hasCard) {state.settings.apPatch[state.gameKey]=!apPatchEnabled();save();} break;
    case Command::SkipRomMd5:
        if (state.hasCard) {state.settings.skipRomMd5[state.gameKey]=!skipRomMd5Enabled();save();} break;
    case Command::Report: pending=Action::Report; break;
    case Command::RepairStorage: pending=Action::RepairStorage;break;
    case Command::ReadCard: pending=Action::ReadCard; break;
    case Command::ChangeCard: pending=Action::ChangeCard; break;
    case Command::Menu: go(Page::Menu); break;
    case Command::About: go(Page::About); break;
    case Command::Licenses: go(Page::Licenses); break;
    case Command::ReadLicense: {
        state.licenseFocus=argument;
        auto data=np3d::readResource((std::string("licenses/")+Licenses[argument]).c_str());
        state.lines=wrap(una::utf8to16u(std::string(data.begin(),data.end())),228);
        go(Page::LicenseText);break;
    }
    case Command::Language: state.languageFrom=state.page;state.languageBefore=currentLanguage();go(Page::Language);break;
    case Command::PickLanguage:
        useLanguage(argument);state.settings.language=languageCodes[argument];save();
        if (state.first) {state.first=false;returnHome();} else go(state.languageFrom);break;
    case Command::Info:
        if (state.patches.empty()) break;
        state.homeFocus=state.focus;state.homeTop=state.top;
        state.info=np3d::inspectPatch(state.patches[state.focus]);
        if (!state.info.error.empty()) {error(state.info.error,false);break;}
        state.lines=wrap(una::utf8to16u(state.info.readme.empty()?tr(Text::NoReadme):state.info.readme),228);
        go(Page::PatchInfo);break;
    case Command::Details: state.lines=wrap(una::utf8to16u(state.detail),228);go(Page::Details);break;
    case Command::Back:
        if (state.first || state.fatal) break;
        if (state.page==Page::Language) {useLanguage(state.languageBefore);go(state.languageFrom);}
        else if (state.page==Page::PatchInfo) {state.page=Page::Home;state.focus=state.homeFocus;state.top=state.homeTop;state.focusTick=state.ticks;}
        else if (state.page==Page::Details) go(Page::Error);
        else if (state.page==Page::LicenseText) {state.lines.clear();go(Page::Licenses);state.focus=state.licenseFocus;state.top=std::max(0,state.focus-Visible+1);}
        else if (state.page==Page::Licenses) go(Page::About);
        else if (state.page==Page::About) {go(Page::Menu);state.focus=MenuCount-1;state.top=std::max(0,state.focus-Visible+1);}
        else returnHome();
        break;
    case Command::Previous: move(-1,true);break;
    case Command::Next: move(1,true);break;
    case Command::Up: scrollLines(-1);break;
    case Command::Down: scrollLines(1);break;
    }
}
}

void showStartup() {
    pixels.resize(2*256*192);
    auto data=np3d::readResource("ui/startup.bin");
    np3d::require(data.size()==pixels.size()*sizeof(uint16_t),"Invalid embedded startup screen");
    std::memcpy(pixels.data(),data.data(),data.size());
    videoSetMode(MODE_5_2D);videoSetModeSub(MODE_5_2D);
    lcdMainOnTop();
    vramSetBankA(VRAM_A_MAIN_BG);vramSetBankC(VRAM_C_SUB_BG);
    displays[0]=bgGetGfxPtr(bgInit(3,BgType_Bmp16,BgSize_B16_256x256,0,0));
    displays[1]=bgGetGfxPtr(bgInitSub(3,BgType_Bmp16,BgSize_B16_256x256,0,0));
    bgUpdate();
    startupProgress(5);
}
void initialize() {
    startupProgress(25);
    loadLanguages();startupProgress(45);
    state.settings=np3d::loadSettings(Root);
    int language=languageIndex(state.settings.language);
    np3d::require(state.settings.language.empty() || language>=0,"Unsupported interface language in settings.json");
    state.first=language<0;useLanguage(language<0?0:language);
    startupProgress(85);
    keysSetRepeat(24,5);
    state.patches=np3d::listPatches(Root);
    startupProgress(100);
    go(state.first?Page::Language:Page::Loading);render();
}
bool ready() { return bool(font) && !pixels.empty(); }
void chooseInitialLanguage() {
    while (state.first) {swiWaitForVBlank();update();}
    go(Page::Loading);render();
}
bool setGame(np3d::SourcePtr card,const sNDSHeader& header) {
    state.hasCard=true;
    state.hasBanner=false;
    char identity[32];std::snprintf(identity,sizeof(identity),"%.4s-%04x-%02x",header.gameCode,header.headerCRC16,header.romversion);
    state.gameKey=identity;
    state.defaultSkipRomMd5=!isDSiMode() && header.unitCode==2;
    state.selected.clear();
    for (const auto& name : state.settings.games[state.gameKey]) {
        std::string path=std::string(Root)+"/patches/"+name;
        if (std::find(state.patches.begin(),state.patches.end(),path)!=state.patches.end()) state.selected.push_back(path);
    }
    if (header.bannerOffset) {
        card->read(header.bannerOffset,&state.banner,sizeof(state.banner));
        if (swiCRC16(0xffff,state.banner.icon,0x820)!=state.banner.crc) {
            noCard(true);error("Game Card banner checksum mismatch");return false;
        }
        std::memcpy(state.titles.data(),state.banner.titles,sizeof(state.banner.titles));
        if (state.banner.version>=2) card->read(header.bannerOffset+sizeof(state.banner),state.titles[6].data(),state.banner.version==2?256:512);
        state.hasBanner=true;
    }
    returnHome();render();return true;
}
void noCard(bool missing,bool reinsert) {
    state.hasCard=false;state.hasBanner=false;state.gameKey.clear();state.selected.clear();state.info={};
    state.cardMissing=missing;state.cardReinsert=reinsert;go(Page::NoCard);render();
}
Action update() {
    state.ticks++;scanKeys();unsigned int down=keysDown(),repeat=keysDownRepeat();
    pending=Action::None;bool changed=false;
    if (repeat&KEY_UP) {move(-1);changed=true;}
    else if (repeat&KEY_DOWN) {move(1);changed=true;}
    if (down&(KEY_LEFT|KEY_L)) {move(-1,true);changed=true;}
    else if (down&(KEY_RIGHT|KEY_R)) {move(1,true);changed=true;}
    if (down&KEY_B) {command(Command::Back);changed=true;}
    else if ((down&(KEY_X|KEY_SELECT)) && (state.page==Page::Home || state.page==Page::Empty || state.page==Page::NoCard)) {command(Command::Menu);changed=true;}
    else if ((down&KEY_Y) && state.page==Page::Home) {command(Command::Info);changed=true;}
    else if ((down&KEY_START) && (state.page==Page::Home || state.page==Page::Empty)) {command(Command::Start);changed=true;}
    else if (down&KEY_A) {
        if (state.page==Page::Home) command(Command::Toggle,state.focus);
        else if (state.page==Page::Language) command(Command::PickLanguage,state.focus);
        else if (state.page==Page::Menu) {
            command(MenuCommands[state.focus]);
        } else if (state.page==Page::Empty) command(Command::Original);
        else if (state.page==Page::NoCard) command(Command::ReadCard);
        else if (state.page==Page::About) command(Command::Licenses);
        else if (state.page==Page::Licenses) command(Command::ReadLicense,state.focus);
        else if (state.page==Page::Error) command(Command::Details);
        changed=true;
    }
    if (down&KEY_TOUCH) {
        touchPosition touch;touchRead(&touch);
        for (const auto& hit : hits) if (touch.px>=hit.x && touch.px<hit.x+hit.width && touch.py>=hit.y && touch.py<hit.y+hit.height) {
            Command value=hit.command;int argument=hit.argument;command(value,argument);changed=true;break;
        }
    }
    if (changed || ((state.page==Page::Home || state.page==Page::PatchInfo || state.page==Page::Licenses || state.page==Page::LicenseText) && state.ticks%4==0)) render();
    return pending;
}
const std::vector<std::string>& selectedPatches() { return state.selected; }
bool apPatchEnabled() {
    auto found=state.settings.apPatch.find(state.gameKey);
    return found==state.settings.apPatch.end() || found->second;
}
bool skipRomMd5Enabled() {
    if (!state.hasCard) return false;
    auto found=state.settings.skipRomMd5.find(state.gameKey);
    return found==state.settings.skipRomMd5.end()?state.defaultSkipRomMd5:found->second;
}
void preparing() { go(Page::Progress);state.patchNumber=1;state.completed=state.total=0;state.elapsed=0;state.preparation={};state.stage=tr(Text::Working);state.progressDetail.clear();render(); }
void progress(const char* stage,uint32_t completed,uint32_t total,const char* detail,uint64_t elapsed,const PreparationProgress& preparation) {
    if (std::strcmp(stage,"Patch sequence")==0) state.patchNumber=completed;
    state.stage=progressText(stage);state.completed=completed;state.total=total;state.elapsed=elapsed;state.preparation=preparation;state.progressDetail=detail;render();
}
void report(bool working) {go(Page::Report);state.reportWorking=working;render();}
void repairStorage() {
    go(Page::Storage);state.storageWorking=true;state.storageWriting=false;state.storageReady=false;
    auto progress=[](storage::Stage stage,unsigned int copy,uint32_t done,uint32_t total) {
        static const Text labels[]={Text::StorageDirectories,Text::StorageAllocation,Text::StorageCompare,
            Text::StorageBackup,Text::StorageRepair,Text::StorageVerify};
        state.stage=format(labels[int(stage)],fmt::arg("copy",copy+1));
        state.completed=done;state.total=total;render();
    };
    progress(storage::Stage::Directories,0,0,0);
    auto result=storage::inspect(progress);
    auto show=[&]() {
        static const Text labels[]={Text::StorageClean,Text::StorageReady,Text::StorageDamaged,Text::StorageAmbiguous,
            Text::StorageUnsupported,Text::StorageBusy,Text::StorageMemory,Text::StorageIoError,Text::StorageRepaired};
        std::string detail=tr(labels[int(result.status)]);
        for (unsigned int index=0;index<result.count;index++) {
            const auto& copy=result.copies[index];
            detail+="\n\n"+format(Text::StorageCopy,fmt::arg("copy",index+1),fmt::arg("files",copy.files),
                fmt::arg("conflicts",copy.conflicts),fmt::arg("lost",copy.lost),fmt::arg("invalid",copy.invalid));
            if (!copy.problem.empty()) detail+="\n"+copy.problem;
        }
        detail+="\n\n"+format(Text::StorageDifferences,fmt::arg("sectors",result.differentSectors));
        if (result.fsInfo) detail+="\n"+std::string(tr(Text::StorageFsInfo));
        if (!result.backup.empty()) detail+="\n\n"+format(Text::StorageBackupPath,fmt::arg("path",result.backup));
        state.lines=wrap(una::utf8to16u(detail),240);state.textTop=0;
        state.storageWorking=false;state.storageWriting=false;state.storageReady=result.status==storage::Status::Repairable;
        render();
    };
    show();
    while (true) {
        swiWaitForVBlank();scanKeys();auto down=keysDown(),repeat=keysDownRepeat();
        bool back=down&KEY_B,repair=down&KEY_A;
        if (repeat&KEY_UP) scrollLines(-1);
        if (repeat&KEY_DOWN) scrollLines(1);
        if (down&(KEY_L|KEY_LEFT)) scrollLines(-7);
        if (down&(KEY_R|KEY_RIGHT)) scrollLines(7);
        if (down&KEY_TOUCH) {
            touchPosition touch;touchRead(&touch);
            for (const auto& hit : hits) if (touch.px>=hit.x && touch.px<hit.x+hit.width && touch.py>=hit.y && touch.py<hit.y+hit.height) {
                if (hit.command==Command::Back) back=true;
                else if (hit.command==Command::RepairStorage) repair=true;
                else if (hit.command==Command::Up) scrollLines(-1);
                else if (hit.command==Command::Down) scrollLines(1);
                break;
            }
        }
        if (back) {state.lines.clear();go(Page::Menu);state.focus=MenuCount-2;state.top=std::max(0,state.focus-Visible+1);render();return;}
        if (repair && state.storageReady) {
            state.storageWorking=true;state.storageWriting=true;
            result=storage::repair(progress);show();
        } else if (down || repeat) render();
    }
}
bool confirm(const std::string& detail) {
    state.lines=wrap(una::utf8to16u(detail),228);go(Page::Confirm);render();
    while (true) {
        swiWaitForVBlank();scanKeys();auto down=keysDown(),repeat=keysDownRepeat();
        bool start=down&(KEY_A|KEY_START),back=down&KEY_B;
        if (repeat&KEY_UP) scrollLines(-1);
        if (repeat&KEY_DOWN) scrollLines(1);
        if (down&(KEY_L|KEY_LEFT)) scrollLines(-7);
        if (down&(KEY_R|KEY_RIGHT)) scrollLines(7);
        if (down&KEY_TOUCH) {
            touchPosition touch;touchRead(&touch);
            for (const auto& hit : hits) if (touch.px>=hit.x && touch.px<hit.x+hit.width && touch.py>=hit.y && touch.py<hit.y+hit.height) {
                if (hit.command==Command::Start) start=true;
                else if (hit.command==Command::Back) back=true;
                else if (hit.command==Command::Up) scrollLines(-1);
                else if (hit.command==Command::Down) scrollLines(1);
                break;
            }
        }
        if (back || start) {state.lines.clear();returnHome();render();return !back;}
        if (down || repeat) render();
    }
}
void error(const std::string& detail,bool mismatch) {state.fatal=false;state.detail=detail;state.mismatch=mismatch;go(Page::Error);render();}
[[noreturn]] void fatal(const std::string& detail) {
    state.fatal=true;state.detail=detail;state.mismatch=false;go(Page::Error);render();
    while (true) {swiWaitForVBlank();update();}
}
}
