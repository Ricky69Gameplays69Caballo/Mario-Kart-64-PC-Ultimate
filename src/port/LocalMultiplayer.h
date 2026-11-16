#ifndef LOCAL_MULTIPLAYER_H
#define LOCAL_MULTIPLAYER_H
#ifdef __cplusplus
extern "C" {
#endif
// Saved PC menu options. Changes are accepted only outside gameplay.
#define CVAR_EXTRA_MAPS "gMultiplayer.EnableExtraMaps"
#define CVAR_TT_INFINITE_MUSHROOMS "gTimeTrials.InfiniteMushrooms"
#define CVAR_MULTIPLAYER_SIXTEEN "gMultiplayer.Enable16Players"
#define CVAR_MULTIPLAYER_EIGHT "gMultiplayer.Enable8Players"
#define CVAR_EXTRA_BOTS_ENABLED "gMultiplayer.ExtraBotsEnabled"
#define CVAR_BALANCED_BOTS "gMultiplayer.BalancedBots"
#define CVAR_REAL_BOT_BALANCE "gMultiplayer.RealBotBalance"
#define CVAR_EXTRA_BOT_PLAYERS "gMultiplayer.ExtraBotPlayers"
#define CVAR_MULTIPLAYER_BOTS "gMultiplayer.Enable3P4PBots"
#define CVAR_SOLO_VS "gMultiplayer.EnableSoloVS"
#define CVAR_MULTIPLAYER_SCREENS "gMultiplayer.Screens"
#define CVAR_MULTIPLAYER_RACE_BOMBS "gMultiplayer.EnableRaceBombKarts"
#define CVAR_MINIMAP_SIZE "gMultiplayer.MinimapSizePercent"
#define CVAR_MINIMAP_TRANSPARENCY "gMultiplayer.MinimapTransparencyPercent"
#define CVAR_AFK_BOTS "gMultiplayer.AfkBots"
#define CVAR_AFK_SECONDS "gMultiplayer.AfkSeconds"
void ExtraMaps_OnChanged(void);
int ExtraMaps_Available(void);
int ExtraMaps_Page(void);
void ExtraMaps_SetPage(int enabled);
const char* ExtraMaps_CourseName(int cup, int row);
void LocalMP_ResetAfk(void);
void LocalMP_UpdateAfk(void);
void LocalMP_RestoreAfkInput(void);
int LocalMP_IsAfkBot(int player);
int LocalMP_ExtraBotUsesItemBoxes(int player);
int MultiplayerOptions_DisplayCount(void);
#define CVAR_RACER_RENDER_DISTANCE "gMultiplayer.RacerRenderDistance"
#define CVAR_LARGE_GRID_FAST_RANKING "gMultiplayer.LargeGridFastRanking"
#define CVAR_LARGE_GRID_LOW_HZ_BOTS "gMultiplayer.LargeGridLowHzBots"
#define CVAR_LARGE_GRID_BOT_PATH_BATCHING "gMultiplayer.LargeGridBotPathBatching"
#define CVAR_LARGE_GRID_MINIMAP "gMultiplayer.LargeGridMinimapBatching"
#define CVAR_LARGE_GRID_PALETTES "gMultiplayer.LargeGridKartTextureReuse"
#define CVAR_LARGE_GRID_CULLING "gMultiplayer.LargeGridOffscreenKartCulling"
#define CVAR_LARGE_GRID_COLLISION_FILTER "gMultiplayer.LargeGridKartCollisionFiltering"
#define CVAR_LARGE_GRID_SKIP_BOT_CONTACTS "gMultiplayer.LargeGridSkipBotContacts"
#define CVAR_LARGE_GRID_DX11_QUEUE "gMultiplayer.LargeGridDX11PresentationQueue"
#define CVAR_LARGE_GRID_DISTANT_BOTS "gMultiplayer.LargeGridLimitDistantBotDrawing"
#define LARGE_GRID_OPTIMIZATION_MIN_RACERS 500
// Live graphics setting: available in every racing mode, independent of toggles.
float RacerRenderDistance_Multiplier(void);
float RacerRenderDistance_FarPlane(float normalFar);
int MultiplayerOptions_SixteenEnabled(void);
void MultiplayerOptions_OnSixteenChanged(void);
int MultiplayerOptions_EightEnabled(void);
int MultiplayerOptions_BotsEnabled(void);
int MultiplayerOptions_CanChange(void);
int MultiplayerOptions_ExtraBotBaseGrid(int extra);
void MultiplayerOptions_OnEightChanged(void);
void MultiplayerOptions_OnBotsChanged(void);
void MultiplayerOptions_UpdateMenus(void);
int LocalMP_IsActive(void);
int LocalMP_IsLobbyOpen(void);
int LocalMP_GameMenuIsOpen(void);
int LocalMP_GameMenuTick(void);
void LocalMP_DrawGameMenu(void);
void LocalMP_DrawGameMenuHint(void);
void LocalMP_DrawPauseMenu(void);
int LocalMP_BattleResultsOpen(void);
int LocalMP_RaceResultsOpen(void);
void LocalMP_DrawRaceResults(void);
void LocalMP_DrawBattleResults(void);
void LocalMP_DrawRacePositions(void);
void LocalMP_DrawItemWindows(void);
int LocalMP_Count(void);
int LocalMP_RacerCount(void);
int LocalMP_IsFinished(int player);
int LocalMP_CanCoastAfterFinish(int player);
// Consume the expanded cup's slot-based ceremony result during ending setup.
int LocalMP_CeremonyResult(int* character, int* rank);
// Podium slots plus spectators, retained after the ceremony result is consumed.
int LocalMP_CeremonyRacerCount(void);
// One-shot ceremony preview using the current GP standings for 3-8 humans.
int LocalMP_CanSkipGrandPrixFinale(void);
void LocalMP_SkipGrandPrixFinale(void);
void LocalMP_ProcessRequest(void);
void LocalMP_OnInput(void);
void LocalMP_ResetRace(void);
void LocalMP_Layout(void);
void LocalMP_UpdateLaps(void);
void LocalMP_UpdateBattle(void);
int LocalMP_FinishTick(void);
void LocalMP_DrawUI(void);
#ifdef __cplusplus
}
#endif
#endif
