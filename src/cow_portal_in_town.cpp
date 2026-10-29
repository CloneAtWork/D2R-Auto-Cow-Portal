// cow-portal-in-town — D2RLoader plugin
//
// WHAT IT DOES
//   Opens the secret cow level portal in the Rogue Encampment, once per game, for every
//   difficulty the player has unlocked (Baal done in LoD/RotW, Diablo done in Classic):
//     - when a game starts / the character loads directly into the Rogue Encampment, and
//     - when the player arrives in the Rogue Encampment later (waypoint, walking, portal).
//   The portal is placed at a fixed spot relative to the central Rogue bonfire (configurable
//   offset), on the nearest free (walkable, unoccupied) position.
//
// HOW (all RVAs: D2R 3.3.0 build 93847 runtime image; evidence in docs/03 + docs/05)
//   Hooks (call the original first, unchanged):
//     QUESTS_ChangeLevel(game, oldLvl, newLvl, player)          @ 0x5155C0  (level transitions)
//     CLIENTS_SpawnPlayer(client, player, game, room, x, y)     @ 0x485350  (game join / act spawn)
//     GAME_UpdateProgress(game)                                 @ 0x406550  (per server frame)
//   Silent pre-check (no "I can't" sound when locked):
//     QUESTS_GetGlobalState(game, A1Q4=4, CUSTOM7=11)           @ 0x514FD0  already opened?
//     UNITS_GetPlayerData(player) -> questFlags[difficulty]     @ 0x34B240
//     QUESTRECORD_GetQuestState(flags, A5Q6=40|A4Q2=26, 0)      @ 0x325C50
//   Placement (anchor = "bonfire", default):
//     find object class 39 (RogueBonfire) in game unit list @ game+0x2A30 (128 buckets, next @+0x158)
//     UNITS_GetCoords / UNITS_GetRoom / DUNGEON_GetLevelIdFromRoom   @ 0x34A040 / 0x34B440 / 0x2EFC10
//     QUESTS_GetFreePosition(game, room, &xy, 3, 0x400, &room, 4, 100) @ 0x517F90
//     CreatePortalObject(game, player, room, x, y, 39, 0, 60, 0, 0)    @ 0x432CE0
//     QUESTS_SetGlobalState(game, A1Q4=4, CUSTOM7=11)                   @ 0x5181C0
//   (anchor = "player": the native ACT1Q4_CreateCowPortal @ 0x5DA2A0, i.e. cube behaviour.)
//   Timing (v0.3.0): an arrival only *schedules* the portal. It is created from the per-frame
//   hook GAME_UpdateProgress(game) @ 0x406550 once (a) the local client reports it is in the
//   Rogue Encampment (LifecycleService LevelChanged -> 1) plus a short settle time, or (b) a
//   10 s timeout passes (remote TCP/IP clients), and (c) the bonfire object exists.
//   Reason: on a cross-act waypoint the server places the player while the client is still
//   loading the act; a portal created at that moment exists on the server but is never shown
//   to the client (bug report 2026-10-26, log 07:39:00).
//
// SAFETY
//   - Refuses to install unless buildName == "93847" AND every function / layout anchor matches
//     its expected bytes (CheckExpectedBytes). After a game patch the plugin stays inactive.
//   - Only acts for player units (unit->type == 0), difficulty 0..2, level 1.
//   - Role = Server (gameplay rule; host-only). Needs NativeHooks.

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// Plugin identity
// ---------------------------------------------------------------------------
constexpr const char* kPluginId       = "cow-portal-in-town";
constexpr const char* kPluginVersion  = "1.0.0";
constexpr const char* kSupportedBuild = "93847";

constexpr D2RL::PluginInfo kPluginInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = kPluginId,
	.name        = "Cow Portal in Town",
	.version     = kPluginVersion,
	.author      = "Alex",
	.description = "Opens the secret cow level portal in the Rogue Encampment for every unlocked difficulty.",
	.flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

// ---------------------------------------------------------------------------
// Default config (also embedded as resource 1002 for D2RMM's plugin importer)
// Keep in sync with cow_portal_in_town.toml.
// ---------------------------------------------------------------------------
constexpr const char* kDefaultToml =
	"# Cow Portal in Town\n"
	"# Opens the secret cow level portal in the Rogue Encampment, once per game, for every\n"
	"# difficulty where the final quest is done (Baal in LoD/RotW, Diablo in Classic).\n"
	"# Changes take effect after restarting the game.\n"
	"\n"
	"[cow_portal]\n"
	"# Master switch. false = no hook is installed at all.\n"
	"enabled = true\n"
	"# Per-difficulty switches.\n"
	"normal = true\n"
	"nightmare = true\n"
	"hell = true\n"
	"\n"
	"[placement]\n"
	"# \"bonfire\" = fixed spot relative to the central Rogue bonfire (recommended)\n"
	"# \"player\"  = next to the player, like the Horadric Cube recipe\n"
	"anchor = \"bonfire\"\n"
	"# Offset from the bonfire in game sub-tiles (1 tile = 5 sub-tiles).\n"
	"# On screen: +x = down-right, -x = up-left, +y = down-left, -y = up-right.\n"
	"# The portal goes to the nearest free spot around this point.\n"
	"offset_x = 6\n"
	"offset_y = 9\n"
	"# If the bonfire cannot be found, open the portal next to the player instead.\n"
	"fallback_to_player = true\n"
	"\n"
	"[diagnostics]\n"
	"# Log every Rogue Encampment arrival and the decision taken (plugin log file).\n"
	"# Startup lines, warnings and errors are always logged.\n"
	"enabled = false\n";

// ---------------------------------------------------------------------------
// Native game functions and layout (build 93847)
// ---------------------------------------------------------------------------
namespace Rva {
constexpr uint64_t QuestsChangeLevel     = 0x5155C0;
constexpr uint64_t ClientsSpawnPlayer    = 0x485350;
constexpr uint64_t GameUpdateProgress    = 0x406550;
constexpr uint64_t CreateCowPortal       = 0x5DA2A0;
constexpr uint64_t QuestsGetGlobalState  = 0x514FD0;
constexpr uint64_t QuestsSetGlobalState  = 0x5181C0;
constexpr uint64_t QuestRecordGetState   = 0x325C50;
constexpr uint64_t UnitsGetPlayerData    = 0x34B240;
constexpr uint64_t UnitsGetCoords        = 0x34A040;
constexpr uint64_t UnitsGetRoom          = 0x34B440;
constexpr uint64_t DungeonLevelFromRoom  = 0x2EFC10;
constexpr uint64_t QuestsGetFreePosition = 0x517F90;
constexpr uint64_t CreatePortalObject    = 0x432CE0;
constexpr uint64_t IterateUnitsOfType    = 0x490DE0;
// Layout anchors: instructions that prove the struct offsets used below.
constexpr uint64_t AnchorObjectList = 0x510E8D; // lea rsi, [rcx+0x2A30]   (object hash list)
constexpr uint64_t AnchorListNext   = 0x490031; // mov rax, [rbx+0x158]    (unit list next)
constexpr uint64_t AnchorClassId    = 0x349886; // mov eax, [rcx+4]        (UNITS_GetClassId)
} // namespace Rva

constexpr uint32_t kGameDifficultyOffset   = 0x104;  // from ACT1Q4_CreateCowPortal
constexpr uint32_t kGameVersionOffset      = 0x106;  // 1 = Classic
constexpr uint32_t kGameObjectListOffset   = 0x2A30; // 128 buckets of Unit*
constexpr uint32_t kGameUnitBuckets        = 128;
constexpr uint32_t kPlayerQuestFlagsOffset = 0x40;
constexpr uint32_t kUnitClassIdOffset      = 0x04;
constexpr uint32_t kUnitListNextOffset     = 0x158;

constexpr int32_t  kLevelRogueEncampment = 1;
constexpr int32_t  kLevelMooMooFarm      = 39;
constexpr int32_t  kObjectPermPortal     = 60;
constexpr uint32_t kObjectRogueBonfire   = 39;
constexpr uint32_t kUnitTypePlayer       = 0;
constexpr uint32_t kUnitTypeObject       = 2;
constexpr int32_t  kQuestA1Q4            = 4;
constexpr int32_t  kQuestA4Q2            = 26; // Terror's End (Diablo)
constexpr int32_t  kQuestA5Q6            = 40; // Eve of Destruction (Baal)
constexpr int32_t  kFlagRewardGranted    = 0;
constexpr int32_t  kFlagCustom7          = 11; // "cow portal already opened in this game"
constexpr uint8_t  kGameVersionClassic   = 1;
constexpr int      kScanEveryFrames      = 5;   // safety-net scan interval (~0.2 s)
constexpr int      kSettleFrames         = 12;  // ~0.5 s after the client reports "in town"
constexpr int      kReadyTimeoutFrames   = 250; // ~10 s: create anyway (remote clients, no UI signal)
constexpr int      kBonfireTimeoutFrames = 375; // ~15 s: give up waiting for the bonfire -> fallback

struct Coord {
	int32_t x;
	int32_t y;
};

using ChangeLevelFn        = void(__fastcall*)(void* game, int32_t oldLevel, int32_t newLevel, void* unit);
using UpdateProgressFn     = void(__fastcall*)(void* game);
using SpawnPlayerFn        = uint64_t(__fastcall*)(void* client, void* player, void* game, void* room, int32_t x, int32_t y);
using CreateCowPortalFn    = int32_t(__fastcall*)(void* game, void* unit, void* unused);
using GetGlobalStateFn     = int32_t(__fastcall*)(void* game, int32_t quest, int32_t flag);
using SetGlobalStateFn     = void(__fastcall*)(void* game, int32_t quest, int32_t flag);
using GetQuestStateFn      = int32_t(__fastcall*)(void* questFlags, int32_t quest, int32_t flag);
using GetPlayerDataFn      = uint8_t*(__fastcall*)(void* unit);
using GetCoordsFn          = void(__fastcall*)(void* unit, Coord* out);
using GetRoomFn            = void*(__fastcall*)(void* unit);
using LevelFromRoomFn      = int32_t(__fastcall*)(void* room);
using GetFreePositionFn    = void(__fastcall*)(void* game, void* room, Coord* coord, uint32_t size, uint32_t collisionMask,
	void** outRoom, int32_t radius, int32_t maxSteps);
using CreatePortalObjectFn = int32_t(__fastcall*)(void* game, void* unit, void* room, int32_t x, int32_t y, int32_t destLevel,
	void** outPortal, int32_t objectId, int32_t arg9, int32_t arg10);
using UnitIterateCb        = int32_t(__fastcall*)(void* game, void* unit, void* arg);
using IterateUnitsOfTypeFn = void(__fastcall*)(void* game, int32_t type, void* arg, UnitIterateCb callback);

// Expected bytes (all verified against d2r_image_93847.bin, sha256 b5615ca5...0018).
constexpr uint8_t kExpChangeLevel[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x60, 0x41, 0xBA, 0x03, 0x00, 0x00, 0x00,
	0x48, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x54, 0x24, 0x28, 0x89, 0x54 };
constexpr uint8_t kExpSpawnPlayer[] = { 0x40, 0x55, 0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24,
	0xD9, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x5C, 0x5F, 0x54, 0x02, 0x48, 0x33, 0xC4, 0x48,
	0x89, 0x45, 0xFF, 0x49 };
constexpr uint8_t kExpUpdateProgress[] = { 0x48, 0x89, 0x74, 0x24, 0x20, 0x55, 0x57, 0x41, 0x57, 0x48, 0x8D, 0x6C,
	0x24, 0xB9, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x5C, 0x4D, 0x5C, 0x02, 0x48, 0x33, 0xC4,
	0x48, 0x89, 0x45, 0x37, 0xFF, 0x81, 0x70, 0x01, 0x00, 0x00 };
constexpr uint8_t kExpCreateCowPortal[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x50,
	0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0xBA, 0x04, 0x00, 0x00, 0x00, 0x44, 0x8D, 0x42, 0x07, 0xE8, 0x10, 0xAD, 0xF3,
	0xFF, 0x85, 0xC0, 0x0F, 0x85, 0x48, 0x01, 0x00, 0x00, 0x80, 0xBB, 0x06, 0x01, 0x00, 0x00, 0x01, 0x75, 0x31, 0x48,
	0x8B, 0xCF, 0xE8, 0x67, 0x0F, 0xD7, 0xFF, 0x0F, 0xB6, 0x8B, 0x04, 0x01, 0x00, 0x00, 0x45, 0x33, 0xC0, 0x48, 0x8B,
	0x4C, 0xC8, 0x40, 0x41, 0x8D, 0x50, 0x1A };
constexpr uint8_t kExpGetGlobalState[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48,
	0x83, 0xEC, 0x20, 0x48, 0x8B, 0x99, 0xD8, 0x21, 0x00, 0x00 };
constexpr uint8_t kExpSetGlobalState[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48,
	0x83, 0xEC, 0x20, 0x48, 0x8B, 0x99, 0xD8, 0x21, 0x00, 0x00 };
constexpr uint8_t kExpGetQuestState[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48,
	0x83, 0xEC, 0x20, 0x41, 0x8B, 0xF0, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24,
	0x38, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xE8, 0x96, 0xFB, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x0F,
	0xC1, 0xE3, 0x04, 0x8D, 0x14, 0x1E, 0xE8, 0xC3, 0x5D };
constexpr uint8_t kExpGetPlayerData[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
	0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0xE4, 0x99, 0xFF, 0xFF, 0x84, 0xC0, 0x74,
	0x01, 0xCC, 0x83, 0x3B, 0x00, 0x74, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00, 0xE8, 0x3B,
	0xAC, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x43, 0x10, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3 };
constexpr uint8_t kExpGetCoords[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA,
	0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0xCD };
constexpr uint8_t kExpGetRoom[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75,
	0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x54, 0xA7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01 };
constexpr uint8_t kExpLevelFromRoom[] = { 0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x49, 0x18 };
constexpr uint8_t kExpGetFreePosition[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
	0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x1A, 0x33, 0x4B, 0x02, 0x48,
	0x33 };
constexpr uint8_t kExpCreatePortalObject[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x68, 0x20, 0x56, 0x57, 0x41, 0x55, 0x41,
	0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x45, 0x8B, 0xF9, 0x49, 0x8B, 0xF8, 0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xE9,
	0x48 };
constexpr uint8_t kExpIterateUnits[] = { 0x40, 0x53, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x4D,
	0x8B, 0xF9, 0x4D, 0x8B, 0xE0, 0x8B, 0xDA, 0x4C, 0x8B, 0xF1, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x58 };
constexpr uint8_t kExpAnchorObjectList[] = { 0x48, 0x8D, 0xB1, 0x30, 0x2A, 0x00, 0x00 };
constexpr uint8_t kExpAnchorListNext[]   = { 0x48, 0x8B, 0x83, 0x58, 0x01, 0x00, 0x00 };
constexpr uint8_t kExpAnchorClassId[]    = { 0x8B, 0x41, 0x04 };

struct ExpectedRange {
	const char*    name;
	uint64_t       rva;
	const uint8_t* bytes;
	uint32_t       size;
};

#define EXP(name, rva, arr) { name, rva, arr, sizeof(arr) }
constexpr ExpectedRange kExpected[] = {
	EXP("QUESTS_ChangeLevel", Rva::QuestsChangeLevel, kExpChangeLevel),
	EXP("CLIENTS_SpawnPlayer", Rva::ClientsSpawnPlayer, kExpSpawnPlayer),
	EXP("GAME_UpdateProgress", Rva::GameUpdateProgress, kExpUpdateProgress),
	EXP("ACT1Q4_CreateCowPortal", Rva::CreateCowPortal, kExpCreateCowPortal),
	EXP("QUESTS_GetGlobalState", Rva::QuestsGetGlobalState, kExpGetGlobalState),
	EXP("QUESTS_SetGlobalState", Rva::QuestsSetGlobalState, kExpSetGlobalState),
	EXP("QUESTRECORD_GetQuestState", Rva::QuestRecordGetState, kExpGetQuestState),
	EXP("UNITS_GetPlayerData", Rva::UnitsGetPlayerData, kExpGetPlayerData),
	EXP("UNITS_GetCoords", Rva::UnitsGetCoords, kExpGetCoords),
	EXP("UNITS_GetRoom", Rva::UnitsGetRoom, kExpGetRoom),
	EXP("DUNGEON_GetLevelIdFromRoom", Rva::DungeonLevelFromRoom, kExpLevelFromRoom),
	EXP("QUESTS_GetFreePosition", Rva::QuestsGetFreePosition, kExpGetFreePosition),
	EXP("CreatePortalObject", Rva::CreatePortalObject, kExpCreatePortalObject),
	EXP("SUNIT_IterateUnitsOfType", Rva::IterateUnitsOfType, kExpIterateUnits),
	EXP("anchor game+0x2A30 object list", Rva::AnchorObjectList, kExpAnchorObjectList),
	EXP("anchor unit+0x158 list next", Rva::AnchorListNext, kExpAnchorListNext),
	EXP("anchor unit+0x04 class id", Rva::AnchorClassId, kExpAnchorClassId),
};
#undef EXP

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum class Anchor { Bonfire, Player };

struct Config {
	bool    enabled          = true;
	bool    difficulty[3]    = { true, true, true };
	Anchor  anchor           = Anchor::Bonfire;
	int32_t offsetX          = 6;
	int32_t offsetY          = 9;
	bool    fallbackToPlayer = true;
	bool    diagnostics      = false;
};

struct Natives {
	ChangeLevelFn        originalChangeLevel = nullptr;
	SpawnPlayerFn        originalSpawnPlayer = nullptr;
	UpdateProgressFn     originalUpdateProgress = nullptr;
	CreateCowPortalFn    createCowPortal     = nullptr;
	GetGlobalStateFn     getGlobalState      = nullptr;
	SetGlobalStateFn     setGlobalState      = nullptr;
	GetQuestStateFn      getQuestState       = nullptr;
	GetPlayerDataFn      getPlayerData       = nullptr;
	GetCoordsFn          getCoords           = nullptr;
	GetRoomFn            getRoom             = nullptr;
	LevelFromRoomFn      levelFromRoom       = nullptr;
	GetFreePositionFn    getFreePosition     = nullptr;
	CreatePortalObjectFn createPortalObject  = nullptr;
	IterateUnitsOfTypeFn iterateUnitsOfType  = nullptr;
};

// A scheduled portal. Only touched on the authoritative game thread (hooks).
struct Pending {
	bool        active     = false;
	void*       game       = nullptr;
	void*       player     = nullptr;
	uint8_t     difficulty = 0;
	const char* reason     = "";
	int         frames     = 0;   // GAME_UpdateProgress calls for this game since scheduling
	int         readyFrame = -1;  // frame at which the client reported "in town"
	uint32_t    uiSeq      = 0;   // g_uiTownSeq when scheduled
	uint32_t    leftSeq    = 0;   // g_uiGameLeftSeq when scheduled
	bool        uiInTownAtSchedule = false; // client already reported "in town" before the server side saw it
	ULONGLONG   startTick  = 0;   // diagnostics only
};

const D2RL::PluginContext* g_ctx = nullptr;
Config                     g_cfg;
Natives                    g_n;
Pending                    g_pending;
// Written on the UI thread (lifecycle events), read on the game thread.
std::atomic<bool>          g_uiInTown { false };
std::atomic<uint32_t>      g_uiTownSeq { 0 };
std::atomic<uint32_t>      g_uiGameLeftSeq { 0 };
bool                       g_haveUiSignal = false;
// A game in which creation failed definitively (no bonfire + fallback off, or native failure).
void*                      g_gaveUpGame    = nullptr;
uint32_t                   g_gaveUpLeftSeq = 0;
uint32_t                   g_scanCounter   = 0;

constexpr const char* kDifficultyNames[3] = { "Normal", "Nightmare", "Hell" };

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
enum class Level { Info, Warn, Error };

void LogF(Level level, const char* fmt, ...) noexcept {
	const D2RL::PluginContext* ctx = g_ctx;
	if (ctx == nullptr) {
		return;
	}
	char    buffer[512];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	switch (level) {
		case Level::Info:  ctx->LogInfo(buffer); break;
		case Level::Warn:  ctx->LogWarn(buffer); break;
		case Level::Error: ctx->LogError(buffer); break;
	}
}

#define DIAG(...)                              \
	do {                                       \
		if (g_cfg.diagnostics) {               \
			LogF(Level::Info, __VA_ARGS__);    \
		}                                      \
	} while (0)

// ---------------------------------------------------------------------------
// Minimal TOML reader: [section] headers and `key = true|false|integer|"string"`.
// Unknown keys are ignored; malformed values keep the default and are reported.
// ---------------------------------------------------------------------------
void Trim(const char*& begin, const char*& end) noexcept {
	while (begin < end && (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n')) {
		++begin;
	}
	while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
		--end;
	}
}

bool Equals(const char* begin, const char* end, const char* literal) noexcept {
	const size_t n = std::strlen(literal);
	return static_cast<size_t>(end - begin) == n && std::memcmp(begin, literal, n) == 0;
}

bool ParseBool(const char* b, const char* e, bool* out) noexcept {
	if (Equals(b, e, "true")) {
		*out = true;
		return true;
	}
	if (Equals(b, e, "false")) {
		*out = false;
		return true;
	}
	return false;
}

bool ParseInt(const char* b, const char* e, int32_t* out) noexcept {
	char buf[24];
	const size_t n = static_cast<size_t>(e - b);
	if (n == 0 || n >= sizeof(buf)) {
		return false;
	}
	std::memcpy(buf, b, n);
	buf[n]      = '\0';
	char* stop  = nullptr;
	long  value = std::strtol(buf, &stop, 10);
	if (stop == buf || *stop != '\0' || value < -1000 || value > 1000) {
		return false;
	}
	*out = static_cast<int32_t>(value);
	return true;
}

// Finds the comment start, ignoring '#' inside a "quoted string".
const char* CommentStart(const char* b, const char* e) noexcept {
	bool inString = false;
	for (const char* p = b; p < e; ++p) {
		if (*p == '"') {
			inString = !inString;
		} else if (*p == '#' && !inString) {
			return p;
		}
	}
	return e;
}

void ParseConfig(const char* text, Config& cfg) noexcept {
	char        section[32] = "";
	const char* p           = text;
	if (static_cast<unsigned char>(p[0]) == 0xEF && static_cast<unsigned char>(p[1]) == 0xBB &&
		static_cast<unsigned char>(p[2]) == 0xBF) {
		p += 3; // UTF-8 BOM
	}
	while (*p != '\0') {
		const char* lineEnd = std::strchr(p, '\n');
		if (lineEnd == nullptr) {
			lineEnd = p + std::strlen(p);
		}
		const char* b = p;
		const char* e = CommentStart(p, lineEnd);
		Trim(b, e);
		if (b < e && *b == '[' && e[-1] == ']') {
			const size_t n = static_cast<size_t>(e - b - 2);
			if (n < sizeof(section)) {
				std::memcpy(section, b + 1, n);
				section[n] = '\0';
			}
		} else if (b < e) {
			const char* eq = static_cast<const char*>(std::memchr(b, '=', static_cast<size_t>(e - b)));
			if (eq != nullptr) {
				const char* kb = b;
				const char* ke = eq;
				const char* vb = eq + 1;
				const char* ve = e;
				Trim(kb, ke);
				Trim(vb, ve);
				bool ok    = true;
				bool known = true;
				if (std::strcmp(section, "cow_portal") == 0) {
					if (Equals(kb, ke, "enabled")) ok = ParseBool(vb, ve, &cfg.enabled);
					else if (Equals(kb, ke, "normal")) ok = ParseBool(vb, ve, &cfg.difficulty[0]);
					else if (Equals(kb, ke, "nightmare")) ok = ParseBool(vb, ve, &cfg.difficulty[1]);
					else if (Equals(kb, ke, "hell")) ok = ParseBool(vb, ve, &cfg.difficulty[2]);
					else known = false;
				} else if (std::strcmp(section, "placement") == 0) {
					if (Equals(kb, ke, "anchor")) {
						if (Equals(vb, ve, "\"bonfire\"")) cfg.anchor = Anchor::Bonfire;
						else if (Equals(vb, ve, "\"player\"")) cfg.anchor = Anchor::Player;
						else ok = false;
					}
					else if (Equals(kb, ke, "offset_x")) ok = ParseInt(vb, ve, &cfg.offsetX);
					else if (Equals(kb, ke, "offset_y")) ok = ParseInt(vb, ve, &cfg.offsetY);
					else if (Equals(kb, ke, "fallback_to_player")) ok = ParseBool(vb, ve, &cfg.fallbackToPlayer);
					else known = false;
				} else if (std::strcmp(section, "diagnostics") == 0) {
					if (Equals(kb, ke, "enabled")) ok = ParseBool(vb, ve, &cfg.diagnostics);
					else known = false;
				} else {
					known = false;
				}
				if (known && !ok) {
					char key[32] {};
					const size_t kn = static_cast<size_t>(ke - kb) < sizeof(key) - 1 ? static_cast<size_t>(ke - kb) : sizeof(key) - 1;
					std::memcpy(key, kb, kn);
					LogF(Level::Warn, "Config: [%s] %s has an invalid value; default kept.", section, key);
				}
			}
		}
		p = (*lineEnd == '\0') ? lineEnd : lineEnd + 1;
	}
}

void LoadConfig() noexcept {
	if (!g_ctx->EnsureConfig(kDefaultToml)) {
		LogF(Level::Warn, "Config: could not create/verify config file; using defaults.");
	}
	char     text[8192] {};
	uint32_t required = 0;
	if (g_ctx->ReadConfig(text, sizeof(text) - 1, &required)) {
		text[sizeof(text) - 1] = '\0';
		ParseConfig(text, g_cfg);
	} else {
		LogF(Level::Warn, "Config: could not read config file (required size %u); using defaults.", required);
	}
}

// ---------------------------------------------------------------------------
// Game helpers (game thread only)
// ---------------------------------------------------------------------------
int32_t UnitLevel(void* unit) noexcept {
	void* room = g_n.getRoom(unit);
	return room != nullptr ? g_n.levelFromRoom(room) : 0;
}

// Walks the game's object hash list and returns the Rogue bonfire in level 1, or nullptr.
void* FindBonfire(void* game) noexcept {
	auto* buckets = reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(game) + kGameObjectListOffset);
	for (uint32_t i = 0; i < kGameUnitBuckets; ++i) {
		int guard = 0;
		for (uint8_t* unit = buckets[i]; unit != nullptr && guard < 100000; ++guard) {
			const uint32_t type    = *reinterpret_cast<uint32_t*>(unit);
			const uint32_t classId = *reinterpret_cast<uint32_t*>(unit + kUnitClassIdOffset);
			if (type == kUnitTypeObject && classId == kObjectRogueBonfire && UnitLevel(unit) == kLevelRogueEncampment) {
				return unit;
			}
			unit = *reinterpret_cast<uint8_t**>(unit + kUnitListNextOffset);
		}
	}
	return nullptr;
}

struct PlayerSearch {
	void* target;
	bool  found;
};

int32_t __fastcall MatchPlayer(void*, void* unit, void* arg) {
	auto* s = static_cast<PlayerSearch*>(arg);
	if (unit == s->target) {
		s->found = true;
		return 1; // stop iterating
	}
	return 0;
}

bool PlayerStillInGame(void* game, void* player) noexcept {
	PlayerSearch s { player, false };
	g_n.iterateUnitsOfType(game, static_cast<int32_t>(kUnitTypePlayer), &s, &MatchPlayer);
	return s.found;
}

// ---------------------------------------------------------------------------
// Core logic (authoritative game thread)
// ---------------------------------------------------------------------------
ULONGLONG ElapsedMs() noexcept {
	return g_pending.startTick ? GetTickCount64() - g_pending.startTick : 0;
}

bool OpenNextToPlayer(void* game, void* unit, uint8_t difficulty) noexcept {
	const int32_t result = g_n.createCowPortal(game, unit, nullptr);
	if (result != 0) {
		LogF(Level::Info, "Cow portal opened next to the player (%s).", kDifficultyNames[difficulty]);
		return true;
	}
	LogF(Level::Warn, "Native cow portal function returned 0 (%s).", kDifficultyNames[difficulty]);
	return false;
}

bool OpenAtBonfire(void* game, void* unit, void* bonfire, uint8_t difficulty) noexcept {
	Coord fire {};
	g_n.getCoords(bonfire, &fire);
	Coord target { fire.x + g_cfg.offsetX, fire.y + g_cfg.offsetY };
	const Coord wanted = target;
	void* freeRoom     = nullptr;
	g_n.getFreePosition(game, g_n.getRoom(bonfire), &target, 3, 0x400, &freeRoom, 4, 100);
	if (freeRoom == nullptr || g_n.levelFromRoom(freeRoom) != kLevelRogueEncampment) {
		LogF(Level::Warn, "No free position near bonfire (%d,%d) + offset (%d,%d).", fire.x, fire.y, g_cfg.offsetX, g_cfg.offsetY);
		return false;
	}
	const int32_t ok = g_n.createPortalObject(game, unit, freeRoom, target.x, target.y, kLevelMooMooFarm, nullptr,
		kObjectPermPortal, 0, 0);
	if (ok == 0) {
		LogF(Level::Warn, "CreatePortalObject failed at (%d,%d).", target.x, target.y);
		return false;
	}
	g_n.setGlobalState(game, kQuestA1Q4, kFlagCustom7); // same per-game flag the cube recipe sets
	LogF(Level::Info, "Cow portal opened in the Rogue Encampment (%s) at (%d,%d); bonfire (%d,%d), requested (%d,%d).",
		kDifficultyNames[difficulty], target.x, target.y, fire.x, fire.y, wanted.x, wanted.y);
	return true;
}

// Checks everything that does not depend on timing. Returns the difficulty, or -1 = do nothing.
int Qualifies(void* game, void* unit, const char* reason, bool verbose) noexcept {
#define QDIAG(...) do { if (verbose) DIAG(__VA_ARGS__); } while (0)
	auto* g = static_cast<uint8_t*>(game);
	// unit->dwUnitType at offset 0; 0 = player. UNITS_GetPlayerData asserts on anything else.
	if (*static_cast<const uint32_t*>(unit) != kUnitTypePlayer) {
		return -1;
	}
	const uint8_t difficulty = g[kGameDifficultyOffset];
	const uint8_t version    = g[kGameVersionOffset];
	if (difficulty > 2) {
		QDIAG("[%s] unexpected difficulty byte %u, skipped.", reason, difficulty);
		return -1;
	}
	if (!g_cfg.difficulty[difficulty]) {
		QDIAG("[%s] %s disabled in config, skipped.", reason, kDifficultyNames[difficulty]);
		return -1;
	}
	if (g_n.getGlobalState(game, kQuestA1Q4, kFlagCustom7) != 0) {
		QDIAG("[%s] %s: cow portal already opened in this game.", reason, kDifficultyNames[difficulty]);
		return -1;
	}
	uint8_t* playerData = g_n.getPlayerData(unit);
	if (playerData == nullptr) {
		QDIAG("[%s] no player data, skipped.", reason);
		return -1;
	}
	void* questFlags = *reinterpret_cast<void**>(playerData + kPlayerQuestFlagsOffset + difficulty * sizeof(void*));
	if (questFlags == nullptr) {
		QDIAG("[%s] no quest flags for difficulty %u, skipped.", reason, difficulty);
		return -1;
	}
	const int32_t requiredQuest = (version == kGameVersionClassic) ? kQuestA4Q2 : kQuestA5Q6;
	if (g_n.getQuestState(questFlags, requiredQuest, kFlagRewardGranted) == 0) {
		QDIAG("[%s] %s (version %u): difficulty not unlocked (quest %d not completed), no portal.", reason,
			kDifficultyNames[difficulty], version, requiredQuest);
		return -1;
	}
	const int32_t level = UnitLevel(unit);
	if (level != kLevelRogueEncampment) {
		QDIAG("[%s] player is in level %d, not the Rogue Encampment; nothing to do.", reason, level);
		return -1;
	}
	return difficulty;
#undef QDIAG
}

// Called when a player arrives in the Rogue Encampment (join, act change, or level change).
void ScheduleCowPortal(void* game, void* unit, const char* reason, bool verbose = true) noexcept {
	if (g_gaveUpGame == game && g_gaveUpLeftSeq == g_uiGameLeftSeq.load()) {
		return; // this game already failed definitively; do not retry every frame
	}
	const int difficulty = Qualifies(game, unit, reason, verbose);
	if (difficulty < 0) {
		return;
	}
	if (g_pending.active && g_pending.leftSeq == g_uiGameLeftSeq.load() && g_pending.game == game &&
		PlayerStillInGame(game, g_pending.player) &&
		UnitLevel(g_pending.player) == kLevelRogueEncampment) {
		if (verbose) {
			DIAG("[%s] portal already scheduled for this game.", reason);
		}
		return;
	}
	g_pending = Pending {};
	g_pending.active     = true;
	g_pending.game       = game;
	g_pending.player     = unit;
	g_pending.difficulty = static_cast<uint8_t>(difficulty);
	g_pending.reason     = reason;
	g_pending.uiSeq      = g_uiTownSeq.load();
	g_pending.leftSeq    = g_uiGameLeftSeq.load();
	g_pending.uiInTownAtSchedule = g_uiInTown.load();
	g_pending.startTick  = GetTickCount64();
	DIAG("[%s] %s: portal scheduled; waiting until the client is in town.", reason, kDifficultyNames[difficulty]);
}

// Runs once per server frame for every game (after the original GAME_UpdateProgress).
void ProcessPending(void* game) noexcept {
	if (!g_pending.active || g_pending.game != game) {
		return;
	}
	Pending& p = g_pending;
	if (g_uiGameLeftSeq.load() != p.leftSeq) {
		DIAG("[%s] client left the game; schedule dropped.", p.reason);
		p.active = false;
		return;
	}
	// Client signal is sampled before counting this frame, so "event + 12 frames" = created.
	// Ready = the client's latest level is the Rogue Encampment, reported after scheduling, or
	// already before it (cross-act waypoint: the client event arrives before the scan sees the
	// player in level 1 - log 2026-10-29 16:04:44, which otherwise waited for the 10 s timeout).
	const bool uiSignal = p.readyFrame < 0 && g_uiInTown.load() &&
		(g_uiTownSeq.load() != p.uiSeq || p.uiInTownAtSchedule);
	if (uiSignal) {
		p.readyFrame = p.frames;
	}
	++p.frames;
	if (uiSignal) {
		DIAG("[%s] client reports in town after %d frames (%llu ms).", p.reason, p.frames,
			static_cast<unsigned long long>(ElapsedMs()));
	}
	if (!PlayerStillInGame(game, p.player)) {
		DIAG("[%s] player left the game; schedule dropped.", p.reason);
		p.active = false;
		return;
	}
	if (g_n.getGlobalState(game, kQuestA1Q4, kFlagCustom7) != 0) {
		DIAG("[%s] portal opened meanwhile (e.g. by the cube); schedule dropped.", p.reason);
		p.active = false;
		return;
	}
	if (UnitLevel(p.player) != kLevelRogueEncampment) {
		DIAG("[%s] player left the Rogue Encampment before the portal was created; schedule dropped.", p.reason);
		p.active = false;
		return;
	}
	const bool clientReady = p.readyFrame >= 0 && p.frames - p.readyFrame >= kSettleFrames;
	const bool timedOut    = p.frames >= kReadyTimeoutFrames;
	if (!clientReady && !timedOut) {
		return;
	}
	if (!clientReady && p.frames == kReadyTimeoutFrames) {
		DIAG("[%s] no client signal after %d frames (%llu ms); creating anyway.", p.reason, p.frames,
			static_cast<unsigned long long>(ElapsedMs()));
	}

	if (g_cfg.anchor == Anchor::Player) {
		if (!OpenNextToPlayer(game, p.player, p.difficulty) && g_n.getGlobalState(game, kQuestA1Q4, kFlagCustom7) == 0) {
			g_gaveUpGame    = game; // never call the native again in this game (it would replay the "I can't" sound)
			g_gaveUpLeftSeq = p.leftSeq;
		}
		p.active = false;
		return;
	}
	void* bonfire = FindBonfire(game);
	if (bonfire == nullptr) {
		if (p.frames < kBonfireTimeoutFrames) {
			return; // keep waiting; town rooms may still be populating
		}
		if (g_cfg.fallbackToPlayer) {
			LogF(Level::Warn, "[%s] bonfire not found after %d frames (%llu ms); opening next to the player instead.", p.reason,
				p.frames, static_cast<unsigned long long>(ElapsedMs()));
			OpenNextToPlayer(game, p.player, p.difficulty);
		} else {
			LogF(Level::Warn, "[%s] bonfire not found after %d frames (%llu ms); no portal (fallback disabled).", p.reason,
				p.frames, static_cast<unsigned long long>(ElapsedMs()));
		}
		if (g_n.getGlobalState(game, kQuestA1Q4, kFlagCustom7) == 0) {
			g_gaveUpGame    = game;
			g_gaveUpLeftSeq = p.leftSeq;
		}
		p.active = false;
		return;
	}
	DIAG("[%s] creating portal after %d frames (%llu ms).", p.reason, p.frames, static_cast<unsigned long long>(ElapsedMs()));
	if (!OpenAtBonfire(game, p.player, bonfire, p.difficulty) && g_cfg.fallbackToPlayer) {
		OpenNextToPlayer(game, p.player, p.difficulty);
	}
	if (g_n.getGlobalState(game, kQuestA1Q4, kFlagCustom7) == 0) {
		g_gaveUpGame    = game;
		g_gaveUpLeftSeq = p.leftSeq;
	}
	p.active = false;
}

// Safety net for arrival paths that neither hook sees (e.g. a cross-act waypoint, bug 2026-10-29):
// every few server frames, look for a qualifying player standing in the Rogue Encampment.
struct ScanState {
	void* game;
	void* found;
};

int32_t __fastcall ScanPlayer(void* game, void* unit, void* arg) {
	auto* st = static_cast<ScanState*>(arg);
	if (UnitLevel(unit) == kLevelRogueEncampment && Qualifies(game, unit, "scan", false) >= 0) {
		st->found = unit;
		return 1; // stop
	}
	return 0;
}

void ScanForArrivals(void* game) noexcept {
	if (g_pending.active && g_pending.game == game) {
		return;
	}
	if (g_gaveUpGame == game && g_gaveUpLeftSeq == g_uiGameLeftSeq.load()) {
		return;
	}
	if (g_n.getGlobalState(game, kQuestA1Q4, kFlagCustom7) != 0) {
		return; // already open in this game: nothing to do (cheap early exit every scan)
	}
	ScanState st { game, nullptr };
	g_n.iterateUnitsOfType(game, static_cast<int32_t>(kUnitTypePlayer), &st, &ScanPlayer);
	if (st.found != nullptr) {
		DIAG("[scan] player found in the Rogue Encampment without an arrival event.");
		ScheduleCowPortal(game, st.found, "scan");
	}
}

// ---------------------------------------------------------------------------
// Hooks (authoritative game thread)
// ---------------------------------------------------------------------------
void __fastcall HookChangeLevel(void* game, int32_t oldLevel, int32_t newLevel, void* unit) {
	g_n.originalChangeLevel(game, oldLevel, newLevel, unit); // normal quest callbacks first, unchanged
	if (newLevel == kLevelRogueEncampment && game != nullptr && unit != nullptr) {
		DIAG("Arrival %d->1.", oldLevel);
		ScheduleCowPortal(game, unit, "arrival");
	}
}

uint64_t __fastcall HookSpawnPlayer(void* client, void* player, void* game, void* room, int32_t x, int32_t y) {
	const uint64_t result = g_n.originalSpawnPlayer(client, player, game, room, x, y); // places the player
	if (game != nullptr && player != nullptr) {
		DIAG("Player spawned in game (level %d).", UnitLevel(player));
		ScheduleCowPortal(game, player, "spawn");
	}
	return result;
}

void __fastcall HookUpdateProgress(void* game) {
	g_n.originalUpdateProgress(game); // the normal server frame first
	if (game != nullptr) {
		if ((++g_scanCounter % kScanEveryFrames) == 0) {
			ScanForArrivals(game);
		}
		ProcessPending(game);
	}
}

// ---------------------------------------------------------------------------
// Client signal (UI thread): "the local player is now in the Rogue Encampment"
// ---------------------------------------------------------------------------
void __cdecl OnGameplayEvent(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	if (!D2RL::Lifecycle::HasGameplayEventField(event, D2RL::Lifecycle::GameplayEventRequiredSize)) {
		return;
	}
	if (event->kind == D2RL::Lifecycle::GameplayEventKind::LevelChanged) {
		DIAG("[client] level %d -> %d.", event->previousValue, event->currentValue);
		if (event->currentValue == kLevelRogueEncampment) {
			g_uiInTown.store(true);
			g_uiTownSeq.fetch_add(1);
		} else {
			g_uiInTown.store(false);
		}
	} else if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) {
		g_uiInTown.store(false);
		g_uiGameLeftSeq.fetch_add(1);
	}
}

bool RegisterClientSignal(const D2RL::PluginContext* context) noexcept {
	const D2RL::LifecycleService* lifecycle = nullptr;
	if (context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success ||
		!D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize) ||
		lifecycle->registerGameplayEventListener == nullptr) {
		return false;
	}
	bool ok = true;
	for (const auto kind : { D2RL::Lifecycle::GameplayEventKind::LevelChanged, D2RL::Lifecycle::GameplayEventKind::GameLeft }) {
		const D2RL::Lifecycle::GameplayEventListener listener {
			.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
			.kind       = kind,
			.callback   = OnGameplayEvent,
		};
		D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
		ok = ok && lifecycle->registerGameplayEventListener(context, &listener, &handle) == D2RL::Lifecycle::Result::Success;
	}
	return ok;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------
bool VerifyBuildAndBytes() noexcept {
	const char* build = g_ctx->buildName;
	if (build == nullptr || std::strcmp(build, kSupportedBuild) != 0) {
		LogF(Level::Error, "Unsupported game build '%s' (this version supports %s only). Plugin inactive.",
			build != nullptr ? build : "<null>", kSupportedBuild);
		return false;
	}
	bool allOk = true;
	for (const ExpectedRange& r : kExpected) {
		if (!g_ctx->CheckExpectedBytes(r.rva, r.bytes, r.size)) {
			LogF(Level::Error, "Expected bytes mismatch at %s (RVA 0x%llX, %u bytes). Game patched or another plugin hooked it.",
				r.name, static_cast<unsigned long long>(r.rva), r.size);
			allOk = false;
		}
	}
	return allOk;
}

template <typename Fn>
Fn At(uintptr_t base, uint64_t rva) noexcept {
	return reinterpret_cast<Fn>(base + rva);
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &kPluginInfo; }

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr || !D2RL::HasContext(context) || context->GetApi() == nullptr) {
		return false;
	}
	g_ctx     = context;
	g_cfg     = Config {};
	g_n       = Natives {};
	g_pending = Pending {};
	g_uiInTown.store(false);
	g_gaveUpGame = nullptr;
	g_scanCounter = 0;
	LoadConfig();
	LogF(Level::Info, "%s %s: enabled=%d normal=%d nightmare=%d hell=%d anchor=%s offset=(%d,%d) fallback=%d diagnostics=%d build=%s",
		kPluginId, kPluginVersion, g_cfg.enabled, g_cfg.difficulty[0], g_cfg.difficulty[1], g_cfg.difficulty[2],
		g_cfg.anchor == Anchor::Bonfire ? "bonfire" : "player", g_cfg.offsetX, g_cfg.offsetY, g_cfg.fallbackToPlayer,
		g_cfg.diagnostics, context->buildName != nullptr ? context->buildName : "<null>");

	if (!g_cfg.enabled) {
		LogF(Level::Info, "Disabled in config; no hooks installed.");
		return true;
	}
	if (!VerifyBuildAndBytes()) {
		return false; // report the requirement and fail cleanly (SDK guidance)
	}

	g_haveUiSignal = RegisterClientSignal(context);
	if (!g_haveUiSignal) {
		LogF(Level::Warn, "Lifecycle service unavailable; the portal will be created %d frames (~10 s) after arrival.",
			kReadyTimeoutFrames);
	}

	const uintptr_t base   = context->exeBase;
	g_n.createCowPortal    = At<CreateCowPortalFn>(base, Rva::CreateCowPortal);
	g_n.getGlobalState     = At<GetGlobalStateFn>(base, Rva::QuestsGetGlobalState);
	g_n.setGlobalState     = At<SetGlobalStateFn>(base, Rva::QuestsSetGlobalState);
	g_n.getQuestState      = At<GetQuestStateFn>(base, Rva::QuestRecordGetState);
	g_n.getPlayerData      = At<GetPlayerDataFn>(base, Rva::UnitsGetPlayerData);
	g_n.getCoords          = At<GetCoordsFn>(base, Rva::UnitsGetCoords);
	g_n.getRoom            = At<GetRoomFn>(base, Rva::UnitsGetRoom);
	g_n.levelFromRoom      = At<LevelFromRoomFn>(base, Rva::DungeonLevelFromRoom);
	g_n.getFreePosition    = At<GetFreePositionFn>(base, Rva::QuestsGetFreePosition);
	g_n.createPortalObject = At<CreatePortalObjectFn>(base, Rva::CreatePortalObject);
	g_n.iterateUnitsOfType = At<IterateUnitsOfTypeFn>(base, Rva::IterateUnitsOfType);

	// The per-frame hook is installed first: without it nothing scheduled could ever be created.
	UpdateProgressFn originalUpdateProgress = nullptr;
	if (!context->InstallInlineHook(Rva::GameUpdateProgress, kExpUpdateProgress, sizeof(kExpUpdateProgress),
			&HookUpdateProgress, &originalUpdateProgress) ||
		originalUpdateProgress == nullptr) {
		LogF(Level::Error, "Could not install the GAME_UpdateProgress hook. Plugin inactive.");
		return false;
	}
	g_n.originalUpdateProgress = originalUpdateProgress;

	ChangeLevelFn originalChangeLevel = nullptr;
	if (!context->InstallInlineHook(Rva::QuestsChangeLevel, kExpChangeLevel, sizeof(kExpChangeLevel), &HookChangeLevel,
			&originalChangeLevel) ||
		originalChangeLevel == nullptr) {
		LogF(Level::Error, "Could not install the QUESTS_ChangeLevel hook; portal will not open on in-act arrivals.");
	} else {
		g_n.originalChangeLevel = originalChangeLevel;
	}

	SpawnPlayerFn originalSpawnPlayer = nullptr;
	if (!context->InstallInlineHook(Rva::ClientsSpawnPlayer, kExpSpawnPlayer, sizeof(kExpSpawnPlayer), &HookSpawnPlayer,
			&originalSpawnPlayer) ||
		originalSpawnPlayer == nullptr) {
		LogF(Level::Error, "Could not install the CLIENTS_SpawnPlayer hook; portal will not open on game start / act change.");
	} else {
		g_n.originalSpawnPlayer = originalSpawnPlayer;
	}
	LogF(Level::Info, "Hooks installed (UpdateProgress 0x%llX, ChangeLevel 0x%llX, SpawnPlayer 0x%llX); client signal %s. Ready.",
		static_cast<unsigned long long>(Rva::GameUpdateProgress), static_cast<unsigned long long>(Rva::QuestsChangeLevel),
		static_cast<unsigned long long>(Rva::ClientsSpawnPlayer), g_haveUiSignal ? "on" : "off");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	// D2RLoader removes the hooks and lifecycle listeners on unload.
	g_pending.active = false;
	g_ctx            = nullptr;
}
