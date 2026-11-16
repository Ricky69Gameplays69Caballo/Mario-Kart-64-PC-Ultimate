#pragma once
#define CVAR_EXTRA_CHARACTERS "gMultiplayer.EnableExtraCharacters"
#define EXTRA_RACERS_RANDOM 79
#ifdef __cplusplus
extern "C" {
#endif
// Appearance IDs never enter native physics/audio tables (which remain 0..7).
int ExtraRacers_Available(void);
void ExtraRacers_OnChanged(void);
int ExtraRacers_Count(void);
int ExtraRacers_ChoiceCount(void);
int ExtraRacers_Columns(void);
int ExtraRacers_ChoiceAt(int index);
int ExtraRacers_Cycle(int id, int delta);
int ExtraRacers_ResolveRandom(int player, int reroll);
int ExtraRacers_Racer(int player);
void ExtraRacers_FillRaceBots(int humans, int racers);
int ExtraRacers_Base(int roster);
const char* ExtraRacers_Name(int roster);
const char* ExtraRacers_Portrait(int roster);
const char* ExtraRacers_AnimatedPortrait(int roster, const char* originalFrame);
int ExtraRacers_Selected(int player);
void ExtraRacers_Select(int player, int roster);
int ExtraRacers_Move(int roster, unsigned short input);
void ExtraRacers_ResetNative(void);
void ExtraRacers_Ceremony(const int* order, int count, int humans);
void ExtraRacers_CeremonyNative(void);
int ExtraRacers_CeremonyAppearance(int rank);
unsigned short ExtraRacers_NativeInput(int player, unsigned short input);
const char* ExtraRacers_Texture(int player, const char* original);
const char* ExtraRacers_PlayerTexture(const void* player, const char* original);
const char* ExtraRacers_HudPortrait(int player, const char* original);
const char* ExtraRacers_PlayerName(int player, int base);
void ExtraRacers_LoadAudio(void);
int ExtraRacers_VoiceSet(int player);
void ExtraRacers_SetVoiceChannel(const void* channel, int voiceSet);
void* ExtraRacers_AudioInstrument(const void* channel, int bank, int instrument, void* original);
int WideMenuArt_Available(void);
#ifdef __cplusplus
}
#endif

