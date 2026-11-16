#include "ExtraRacers.h"
#include "LocalMultiplayer.h"
#include <array>
#include <map>
#include <string>
#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>
#include <libultraship.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include "port/resource/type/AudioBank.h"
extern "C" {
#include "main.h"
#include "menus.h"
#include "defines.h"
#include "sounds.h"
#include "audio/external.h"
#include "menu_items.h"
}
namespace {
struct Racer { const char* name; int base; const char* bank; const char* kart; };
constexpr Racer roster[] = {
    {"Mario",0,nullptr,"mario"},{"Luigi",1,nullptr,"luigi"},
    {"Yoshi",2,nullptr,"yoshi"},{"Toad",3,nullptr,"toad"},
    {"Donkey Kong",4,nullptr,"donkeykong"},{"Wario",5,nullptr,"wario"},
    {"Peach",6,nullptr,"peach"},{"Bowser",7,nullptr,"bowser"},
    {"Link",1,"deluxe","luigi"},{"Sonic",2,"deluxe","yoshi"},
    {"Crash",3,"deluxe","toad"},{"Banjo",4,"deluxe","donkeykong"},
    {"Waluigi",5,"deluxe","wario"},{"Zelda",6,"deluxe","peach"},
    {"Skull Kid",7,"deluxe","bowser"},
    {"Goku",0,"dragonball","mario"},{"Frieza",1,"dragonball","luigi"},
    {"Piccolo",2,"dragonball","yoshi"},{"Krillin",3,"dragonball","toad"},
    {"Beerus",4,"dragonball","donkeykong"},{"Vegeta",5,"dragonball","wario"},
    {"Trunks",6,"dragonball","peach"},{"Cell",7,"dragonball","bowser"},
    {"Pikachu",0,"pokemon","mario"},{"Bulbasaur",1,"pokemon","luigi"},
    {"Cubone",2,"pokemon","yoshi"},{"Tangela",3,"pokemon","toad"},
    {"Slowpoke",4,"pokemon","donkeykong"},{"Porygon",5,"pokemon","wario"},
    {"Meowth",6,"pokemon","peach"},{"Gengar",7,"pokemon","bowser"},
    {"CJ",0,"gta","mario"},{"Sweet",1,"gta","luigi"},{"Ryder",2,"gta","yoshi"},{"Cesar",3,"gta","toad"},
    {"OG Loc",4,"gta","donkeykong"},{"Snakehead",5,"gta","wario"},{"Big Smoke",6,"gta","peach"},{"Tenpenny",7,"gta","bowser"},
    {"Homer",0,"simpsons","mario"},{"Bart",1,"simpsons","luigi"},{"Apu",2,"simpsons","yoshi"},{"Lisa",3,"simpsons","toad"},
    {"Wiggum",4,"simpsons","donkeykong"},{"Krusty",5,"simpsons","wario"},{"Marge",6,"simpsons","peach"},{"Flanders",7,"simpsons","bowser"},
    {"Jason",0,"slasher","mario"},{"Freddy",1,"slasher","luigi"},{"Leatherface",2,"slasher","yoshi"},{"Chucky",3,"slasher","toad"},
    {"Michael Myers",4,"slasher","donkeykong"},{"Ghostface",5,"slasher","wario"},{"Regan",6,"slasher","peach"},{"Pennywise",7,"slasher","bowser"},
    {"Shinji",0,"eva","mario"},
    {"Rei",1,"eva","luigi"},
    {"Pen Pen",2,"eva","yoshi"},
    {"Rei (chibi)",3,"eva","toad"},
    {"Asuka (chibi)",4,"eva","donkeykong"},
    {"Shinji (chibi)",5,"eva","wario"},
    {"Asuka",6,"eva","peach"},
    {"Gendo",7,"eva","bowser"},
    {"Shinji (Rebuild)",0,"evarebuild","mario"},
    {"Rei (Rebuild)",1,"evarebuild","luigi"},
    {"Pen Pen (Rebuild)",2,"evarebuild","yoshi"},
    {"Misato",3,"evarebuild","toad"},
    {"Kaworu",4,"evarebuild","donkeykong"},
    {"Mari",5,"evarebuild","wario"},
    {"Asuka (Rebuild)",6,"evarebuild","peach"},
    {"Gendo (Rebuild)",7,"evarebuild","bowser"},
    {"Red",0,"birds","mario"},
    {"Chuck",1,"birds","luigi"},
    {"Blues",2,"birds","yoshi"},
    {"Bomb",3,"birds","toad"},
    {"Terence",4,"birds","donkeykong"},
    {"Pig",5,"birds","wario"},
    {"Matilda",6,"birds","peach"},
    {"King Pig",7,"birds","bowser"},
};
std::array<int, NUM_RACERS> selected = {0,1,2,3,4,5,6,7};
std::array<int, NUM_RACERS> randomRacers = [] {
    std::array<int, NUM_RACERS> values;
    values.fill(-1);
    return values;
}();
std::array<int, NUM_RACERS> ceremony;
bool ceremonyPrepared=false;
// Map nodes keep all returned OTR path pointers stable for display-list execution.
std::map<std::pair<int,std::string>,std::string> paths;
// Original asset strings are static. Avoid allocating path keys on every kart
// draw (up to 16 racers in 16 viewports); resolve each source pointer once.
std::array<std::map<const char*,const char*,std::less<const char*>>,79> textureCache;
std::array<std::shared_ptr<SM64::AudioBank>,9> voiceBanks;
// Accessed only by the audio command consumer / sequence interpreter.
std::map<const void*,int,std::less<const void*>> voiceChannels;
constexpr int grid[] = {0,1,6,3,2,4,5,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,EXTRA_RACERS_RANDOM};
bool enabled(int id) {
    if (id < 8) return true;
    if (!ExtraRacers_Available()) return false;
    if (id < 15) return CVarGetInteger("gRacerSets.Deluxe", 1);
    if (id < 23) return CVarGetInteger("gRacerSets.DragonBall", 1);
    if (id < 31) return CVarGetInteger("gRacerSets.Pokemon", 1);
    if (id < 39) return CVarGetInteger("gRacerSets.GTA", 1);
    if (id < 47) return CVarGetInteger("gRacerSets.Simpsons", 1);
    if (id < 55) return CVarGetInteger("gRacerSets.Slasher", 1);
    if (id < 63) return CVarGetInteger("gRacerSets.Eva", 1);
    if (id < 71) {
        if (!CVarGetInteger("gRacerSets.EvaRebuild", 1)) return false;
        // Keep the original set's versions when both sets contain the same character.
        if (CVarGetInteger("gRacerSets.Eva", 1) &&
            (id == 63 || id == 64 || id == 65 || id == 69 || id == 70)) return false;
        return true;
    }
    if (id < 79) return CVarGetInteger("gRacerSets.Birds", 1);
    return id == EXTRA_RACERS_RANDOM;
}
int valid(int id) { return id >= 0 && id <= EXTRA_RACERS_RANDOM && enabled(id) ? id : 0; }
std::vector<int> choices() {
    std::vector<int> result;
    for (int id : grid) if (enabled(id)) result.push_back(id);
    return result;
}
int activeAppearance(int player) {
    if (gGamestate==ENDING && ceremonyPrepared) {
        if (player<0 || player>=NUM_RACERS) return -1;
        const int id=ceremony[player];
        return id>=8 && id<static_cast<int>(std::size(roster)) &&
            gPlayers[player].characterId==roster[id].base ? id : -1;
    }
    const int racers = LocalMP_IsActive() ? LocalMP_RacerCount() : gPlayerCountSelection1;
    if (gDemoMode || player<0 || player>=racers || player>=NUM_RACERS) return -1;
    const int id=ExtraRacers_Racer(player);
    if (!ExtraRacers_Available() || id<8 || id>=static_cast<int>(std::size(roster)) ||
        gPlayers[player].characterId!=roster[id].base) return -1;
    return id;
}
const char* path(int id, const std::string& original, bool isTexture) {
    auto key=std::make_pair(id,original);
    auto it=paths.find(key);
    if (it==paths.end()) {
        const std::string prefix="__OTR__extra_racers/"+std::string(roster[id].bank)+"/";
        it=paths.emplace(std::move(key),prefix+(isTexture ? original : std::string(roster[id].kart)+"/"+original)).first;
    }
    return it->second.c_str();
}
}
extern "C" int ExtraRacers_Available(void) {
    static const bool found=Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()
        ->HasFile("extra_racers/manifest.json");
    return found && CVarGetInteger(CVAR_EXTRA_CHARACTERS, 1);
}
extern "C" void ExtraRacers_OnChanged(void) {
    // Convert appearance IDs before the disabled roster's range is enforced.
    // Leave every native physics/menu index in the eight-character range.
    if (!CVarGetInteger(CVAR_EXTRA_CHARACTERS, 1)) {
        for (int& id : selected) {
            if (id >= 8 && id < static_cast<int>(std::size(roster))) id = roster[id].base;
            else if (id == EXTRA_RACERS_RANDOM) id = 0;
        }
    }
    for (int& id : selected) if (!enabled(id)) id = id >= 8 && id < 79 ? roster[id].base : 0;
    ceremonyPrepared = false;
    randomRacers.fill(-1);
    MultiplayerOptions_OnBotsChanged(); // Rebuild menu and animation state safely.
}
extern "C" int ExtraRacers_Count(void) { int n=0; for (int i=0;i<79;++i) n+=enabled(i); return n; }
extern "C" int ExtraRacers_ChoiceCount(void) { return (int)choices().size(); }
extern "C" float OTRGetAspectRatio(void);
extern "C" int ExtraRacers_Columns(void) {
    const int count = ExtraRacers_ChoiceCount();
    const float width = CVarGetInteger("gGraphics.WidescreenMenuBackgrounds", 1) ? 240.f * OTRGetAspectRatio() : 320.f;
    int best = 1, bestSize = 0;
    for (int columns = 1; columns <= std::min(count, 20); ++columns) {
        const int rows = (count + columns - 1) / columns;
        const int w = static_cast<int>((width - 16 - (columns - 1) * 3) / columns);
        const int h = (226 - 34 - (rows - 1) * 3) / rows;
        const int size = std::min(w, h);
        if (size > bestSize) { bestSize = size; best = columns; }
    }
    return best;
}
extern "C" int ExtraRacers_ChoiceAt(int index) { auto c=choices(); return index>=0 && index<(int)c.size() ? c[index] : 0; }
extern "C" int ExtraRacers_Cycle(int id,int delta) { auto c=choices(); auto it=std::find(c.begin(),c.end(),id); int i=it==c.end()?0:(int)(it-c.begin()); return c[(i+delta+(int)c.size())%(int)c.size()]; }
extern "C" int ExtraRacers_Base(int id) { id=valid(id); return id==EXTRA_RACERS_RANDOM ? 0 : roster[id].base; }
extern "C" const char* ExtraRacers_Name(int id) { id=valid(id); return id==EXTRA_RACERS_RANDOM ? "Random" : roster[id].name; }
extern "C" const char* ExtraRacers_Portrait(int id) {
    id=valid(id);
    if (id==EXTRA_RACERS_RANDOM) return nullptr;
    if (id>=8) return path(id,"portrait",false);
    static const char* originals[]={
        "__OTR__textures/player_selection/mario_face_00","__OTR__textures/player_selection/luigi_face_00",
        "__OTR__textures/player_selection/yoshi_face_00","__OTR__textures/player_selection/toad_face_00",
        "__OTR__textures/player_selection/donkeykong_face_00","__OTR__textures/player_selection/wario_face_00",
        "__OTR__textures/player_selection/peach_face_00","__OTR__textures/player_selection/bowser_face_00"};
    return originals[id];
}
extern "C" int ExtraRacers_Selected(int player) {
    return player>=0 && player<NUM_RACERS ? valid(selected[player]) : 0;
}
extern "C" const char* ExtraRacers_AnimatedPortrait(int id,const char* originalFrame) {
    id=valid(id);
    if (id<8) return originalFrame;
    if (id==EXTRA_RACERS_RANDOM) return nullptr;
    if (id==8) return ExtraRacers_Portrait(id); // Link's source has Luigi's menu frames.
    const std::string original=originalFrame ? originalFrame : "";
    const auto pos=original.rfind("_face_");
    if (pos==std::string::npos || pos+8>original.size()) return ExtraRacers_Portrait(id);
    const std::string filename="portrait_"+original.substr(pos+6,2);
    const char* result=path(id,filename,false);
    // Allows an older local asset pack to fall back safely until it is updated.
    static std::map<int,bool> animated;
    auto found=animated.find(id);
    if (found==animated.end()) found=animated.emplace(id,
        Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->HasFile(
            std::string(path(id,"portrait_00",false)).substr(7))).first;
    return found->second ? result : ExtraRacers_Portrait(id);
}
extern "C" void ExtraRacers_Select(int player,int id) {
    if (player>=0 && player<NUM_RACERS) {
        if (selected[player]!=valid(id)) randomRacers[player]=-1;
        selected[player]=valid(id);
        ceremonyPrepared=false;
    }
}
extern "C" int ExtraRacers_Racer(int player) {
    if (player<0 || player>=NUM_RACERS) return 0;
    if (selected[player]==EXTRA_RACERS_RANDOM)
        return valid(randomRacers[player]);
    return valid(selected[player]);
}
extern "C" int ExtraRacers_ResolveRandom(int player, int reroll) {
    if (player >= 0 && player < NUM_RACERS && selected[player] == EXTRA_RACERS_RANDOM) {
        auto c=choices();
        c.erase(std::remove(c.begin(),c.end(),EXTRA_RACERS_RANDOM),c.end());
        const int previous=randomRacers[player];
        if (!reroll && std::find(c.begin(),c.end(),previous)!=c.end()) return previous;
        if (c.size()>1) c.erase(std::remove(c.begin(),c.end(),previous),c.end());
        static std::mt19937 generator(std::random_device{}());
        randomRacers[player]=c[std::uniform_int_distribution<int>(0,(int)c.size()-1)(generator)];
    }
    return ExtraRacers_Racer(player);
}
extern "C" void ExtraRacers_FillRaceBots(int humans,int racers) {
    if (!ExtraRacers_Available() || racers>NUM_RACERS || humans<1 || humans>racers) return;
    std::array<bool,79> used{};
    for (int player = 0; player < humans; ++player) {
        const int choice = ExtraRacers_Selected(player);
        // Reserve every original needed in later rounds even if Random rolls it now.
        if (choice != EXTRA_RACERS_RANDOM) used[choice] = true;
    }
    std::vector<int> originals, extras;
    for (int id=0;id<8;++id) if (!used[id]) originals.push_back(id);
    for (int id=8;id<79;++id) if (enabled(id) && !used[id]) extras.push_back(id);
    static std::mt19937 generator(std::random_device{}());
    std::shuffle(originals.begin(),originals.end(),generator);
    std::shuffle(extras.begin(),extras.end(),generator);
    // Reserve CPU seats for every missing original first. Human picks are never
    // replaced if a mostly-human grid leaves too few CPU seats for all eight.
    const int seats=racers-humans;
    std::vector<int> candidates;
    for (int id:originals) if (static_cast<int>(candidates.size())<seats) candidates.push_back(id);
    for (int id:extras) if (static_cast<int>(candidates.size())<seats) candidates.push_back(id);
    std::shuffle(candidates.begin(),candidates.end(),generator);
    for (int player=humans;player<racers;++player)
        ExtraRacers_Select(player, candidates.empty() ? player % NUM_PLAYERS :
            candidates[(player - humans) % candidates.size()]);
}
extern "C" int ExtraRacers_Move(int id,unsigned short input) {
    const int count=ExtraRacers_ChoiceCount();
    auto c=choices();
    const auto it=std::find(c.begin(),c.end(),valid(id));
    int cell=it==c.end()?0:static_cast<int>(it-c.begin()),next=cell;
    const int columns = ExtraRacers_Columns();
    if (input&L_JPAD) { if (cell%columns) --next; }
    else if (input&R_JPAD) { if (cell%columns<columns-1 && cell+1<count) ++next; }
    else if (input&U_JPAD) { if (cell>=columns) next-=columns; }
    else if (input&D_JPAD) { if (cell+columns<count) next+=columns; }
    return c[next];
}
extern "C" void ExtraRacers_ResetNative(void) {
    ceremonyPrepared=false;
    if (!ExtraRacers_Available()) return;
    for (int player=0;player<4;++player) {
        const int cell=gCharacterGridSelections[player]-1;
        if (cell>=0 && cell<8) {
            selected[player]=grid[cell];
            randomRacers[player]=-1;
            gCharacterSelections[player]=grid[cell];
        }
    }
}
extern "C" void ExtraRacers_Ceremony(const int* order,int count,int humans) {
    ceremony.fill(-1);
    for (int rank=0;rank<std::min(count,NUM_RACERS);++rank) {
        const int player=order[rank];
        if (player>=0 && player<count && player<NUM_RACERS) ceremony[rank]=ExtraRacers_Racer(player);
    }
    ceremonyPrepared=true;
}
extern "C" int ExtraRacers_CeremonyAppearance(int rank) {
    if (!ceremonyPrepared || !ExtraRacers_Available() || rank < 0 || rank >= NUM_RACERS) return -1;
    return ceremony[rank];
}
extern "C" void ExtraRacers_CeremonyNative(void) {
    if (ceremonyPrepared || !ExtraRacers_Available()) return;
    std::array<int,NUM_RACERS> order;
    order.fill(-1);
    const int humans=std::clamp(static_cast<int>(gPlayerCountSelection1),1,4);
    for (int rank=0;rank<8;++rank) {
        for (int player=0;player<humans;++player) {
            if (ExtraRacers_Base(ExtraRacers_Racer(player))==gCharacterIdByGPOverallRank[rank]) {
                order[rank]=player;
                break;
            }
        }
    }
    ExtraRacers_Ceremony(order.data(),8,humans);
}
extern "C" unsigned short ExtraRacers_NativeInput(int player,unsigned short input) {
    if (!ExtraRacers_Available() || player<0 || player>=4 || is_screen_being_faded() ||
        gPlayerSelectMenuSelection!=PLAYER_SELECT_MENU_MAIN || !gCharacterGridSelections[player] ||
        gCharacterGridIsSelected[player]) return input;
    const int before=ExtraRacers_Selected(player),next=ExtraRacers_Move(before,input);
    ExtraRacers_Select(player,next);
    const int base=ExtraRacers_Base(next);
    gCharacterGridSelections[player]=static_cast<int>(std::find(std::begin(grid),std::begin(grid)+8,base)-std::begin(grid))+1;
    gCharacterSelections[player]=base;
    if (next!=before) { reset_cycle_flash_menu(); play_sound2(SOUND_MENU_CURSOR_MOVE); }
    return input & ~(L_JPAD|R_JPAD|U_JPAD|D_JPAD);
}
extern "C" const char* ExtraRacers_Texture(int player,const char* original) {
    const int id=activeAppearance(player);
    if (id<0 || !original) return original;
    const auto cached=textureCache[id].find(original);
    if (cached!=textureCache[id].end()) return cached->second;
    const std::string name=original;
    const std::string prefix="__OTR__textures/karts/"+std::string(roster[id].kart)+"_kart/";
    const char* result=name.compare(0,prefix.size(),prefix)==0 ? path(id,name.substr(7),true) : original;
    textureCache[id].emplace(original,result);
    return result;
}
extern "C" const char* ExtraRacers_PlayerTexture(const void* player,const char* original) {
    const auto address=reinterpret_cast<uintptr_t>(player),begin=reinterpret_cast<uintptr_t>(&gPlayers[0]);
    if (address<begin || address>=begin+sizeof(Player)*NUM_RACERS || (address-begin)%sizeof(Player)) return original;
    return ExtraRacers_Texture(static_cast<int>((address-begin)/sizeof(Player)),original);
}
extern "C" const char* ExtraRacers_HudPortrait(int player,const char* original) {
    const int id=activeAppearance(player);
    return id<0 ? original : path(id,"hud",false);
}
extern "C" const char* ExtraRacers_PlayerName(int player,int base) {
    const int id=activeAppearance(player);
    return id<0 ? ExtraRacers_Name(std::clamp(base,0,7)) : ExtraRacers_Name(id);
}
extern "C" void ExtraRacers_LoadAudio(void) {
    auto resources=Ship::Context::GetRawInstance()->GetResourceManager();
    const char* paths[]={"extra_racers/audio/deluxe/bank","extra_racers/audio/dragonball/bank","extra_racers/audio/pokemon/bank","extra_racers/audio/gta/bank","extra_racers/audio/simpsons/bank","extra_racers/audio/slasher/bank","extra_racers/audio/eva/bank","extra_racers/audio/evarebuild/bank","extra_racers/audio/birds/bank"};
    for (int i=0;i<9;++i) {
        if (resources->GetArchiveManager()->HasFile(paths[i]))
            voiceBanks[i]=std::dynamic_pointer_cast<SM64::AudioBank>(resources->LoadResourceProcess(paths[i]));
    }
}
extern "C" int ExtraRacers_VoiceSet(int player) {
    if (!ExtraRacers_Available() || player<0 || player>=NUM_RACERS || gDemoMode) return 0;
    const bool selecting=gMenuSelection==CHARACTER_SELECT_MENU || LocalMP_GameMenuIsOpen() || LocalMP_IsLobbyOpen();
    const int id=selecting ? ExtraRacers_Racer(player) : activeAppearance(player);
    if (id<8 || id>=static_cast<int>(std::size(roster))) return 0;
    const int bank=id<15 ? 1 : id<23 ? 2 : id<31 ? 3 : id<39 ? 4 : id<47 ? 5 : id<55 ? 6 : id<63 ? 7 : id<71 ? 8 : 9;
    return voiceBanks[bank-1] ? bank : 0;
}
extern "C" void ExtraRacers_SetVoiceChannel(const void* channel,int voiceSet) {
    if (channel) voiceChannels[channel]=voiceSet>=1 && voiceSet<=9 ? voiceSet : 0;
}
extern "C" void* ExtraRacers_AudioInstrument(const void* channel,int bank,int instrument,void* original) {
    if (bank!=0 || instrument<0) return original;
    const auto found=voiceChannels.find(channel);
    if (found==voiceChannels.end() || found->second<1 || found->second>9) return original;
    const auto& custom=voiceBanks[found->second-1];
    if (!custom || instrument>=static_cast<int>(custom->instruments.size()) || !custom->instruments[instrument]) return original;
    return custom->instruments[instrument];
}

extern "C" int WideMenuArt_Available(void) {
    static const bool found = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->HasFile("wide_menu_art/title_blue_left");
    return found;
}
