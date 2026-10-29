// Test harness for cow-portal-in-town.dll v0.3.x (runs under Wine/Windows, NOT inside D2R).
//
// Simulates D2RLoader + the game server + the local client:
//   - exeBase = private RWX buffer; at every native RVA the plugin calls, a 14-byte absolute jump
//     to a mock is written (a mock being hit proves the plugin computed base+RVA correctly).
//   - CheckExpectedBytes / InstallInlineHook compare against the REAL dump d2r_image_93847.bin,
//     so every expected-byte constant in the plugin is validated against the actual game image.
//   - A fake game object (0x4000 bytes) with the object hash list at +0x2A30 holds fake units
//     (+0 type, +4 classId, +0x158 next), so the plugin's own list walk is exercised for real.
//   - Server frames = calls of the hooked GAME_UpdateProgress; the client signal = LifecycleService
//     LevelChanged events fired by the test, like D2RLoader does on the UI thread.
#include <D2RLPlugin/api.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg)                                                        \
	do {                                                                        \
		if (cond) { ++g_pass; std::printf("PASS  %s\n", msg); }                 \
		else { ++g_fail; std::printf("FAIL  %s  (line %d)\n", msg, __LINE__); } \
		std::fflush(stdout);                                                    \
	} while (0)

struct Coord { int32_t x, y; };

// ------------------------------------------------------------------ fake game world
struct FakeQuestFlags { bool a4q2, a5q6; };
struct FakeRoom { int32_t level; };
struct UnitInfo { Coord pos; FakeRoom* room; };

alignas(16) static uint8_t g_game[0x4000];
alignas(16) static uint8_t g_otherGame[0x4000];
alignas(16) static uint8_t g_player[0x200];
alignas(16) static uint8_t g_bonfire[0x200];
alignas(16) static uint8_t g_decoyWrongLevel[0x200];   // classId 39 but level 2
alignas(16) static uint8_t g_decoyOtherObject[0x200];  // object, classId 119 (waypoint)
static uint8_t        g_playerData[0x80];
static FakeQuestFlags g_flags[3];
static FakeRoom       g_townRoomA { 1 }, g_townRoomB { 1 }, g_moorRoom { 2 }, g_act5Room { 109 };
static std::map<void*, UnitInfo> g_units;
static std::vector<void*> g_playersInGame;
static bool g_globalCowOpened = false;
static bool g_freePosFails = false;

static void SetUnit(uint8_t* u, uint32_t type, uint32_t classId) {
	std::memset(u, 0, 0x200); std::memcpy(u, &type, 4); std::memcpy(u + 4, &classId, 4);
}
static void ObjectListClear() { std::memset(g_game + 0x2A30, 0, 128 * 8); }
static void ObjectListAdd(int bucket, uint8_t* unit) {
	uint8_t** head = reinterpret_cast<uint8_t**>(g_game + 0x2A30) + bucket;
	std::memcpy(unit + 0x158, head, 8);   // unit->next = head
	*head = unit;
}

// ------------------------------------------------------------------ mocks + counters
static int n_origChange = 0, n_origSpawn = 0, n_origUpdate = 0, n_cow = 0, n_create = 0, n_setGlobal = 0;
static Coord last_create_xy; static void* last_create_room; static int last_create_dest, last_create_obj, last_create_a9, last_create_a10;
static void* last_create_unit; static void* last_create_out; static Coord last_freepos_in;
static int last_set_q, last_set_f;
static void* spawn_args[4];

static void __fastcall MockOrigChangeLevel(void*, int32_t, int32_t, void*) { ++n_origChange; }
static uint64_t __fastcall MockOrigSpawn(void* c, void* p, void* g, void* r, int32_t, int32_t) {
	++n_origSpawn; spawn_args[0] = c; spawn_args[1] = p; spawn_args[2] = g; spawn_args[3] = r; return 0xC0FFEEULL;
}
static void __fastcall MockOrigUpdate(void*) { ++n_origUpdate; }
static int32_t __fastcall MockCreateCowPortal(void* game, void* unit, void* unused) {
	++n_cow; if (game != g_game || unit != g_player || unused) std::printf("  !! cow bad args\n");
	g_globalCowOpened = true; return 1;
}
static int32_t __fastcall MockGetGlobal(void* g, int32_t q, int32_t f) { return (g == g_game && q == 4 && f == 11 && g_globalCowOpened) ? 1 : 0; }
static void __fastcall MockSetGlobal(void* g, int32_t q, int32_t f) {
	++n_setGlobal; last_set_q = q; last_set_f = f; if (g == g_game && q == 4 && f == 11) g_globalCowOpened = true;
}
static int32_t __fastcall MockGetQuestState(void* flags, int32_t q, int32_t f) {
	auto* fl = static_cast<FakeQuestFlags*>(flags); if (f != 0) return 0;
	return q == 26 ? fl->a4q2 : q == 40 ? fl->a5q6 : 0;
}
static uint8_t* __fastcall MockGetPlayerData(void* unit) {
	if (*static_cast<uint32_t*>(unit) != 0) std::printf("  !! GetPlayerData on non-player\n");
	return g_playerData;
}
static void __fastcall MockGetCoords(void* unit, Coord* out) {
	auto it = g_units.find(unit); *out = it != g_units.end() ? it->second.pos : Coord { 0, 0 };
}
static void* __fastcall MockGetRoom(void* unit) { auto it = g_units.find(unit); return it != g_units.end() ? it->second.room : nullptr; }
static int32_t __fastcall MockLevelFromRoom(void* room) { return room ? static_cast<FakeRoom*>(room)->level : 0; }
static void __fastcall MockGetFreePosition(void* game, void* room, Coord* c, uint32_t size, uint32_t mask, void** out, int32_t radius, int32_t max) {
	last_freepos_in = *c;
	if (game != g_game || size != 3 || mask != 0x400 || radius != 4 || max != 100) std::printf("  !! freepos bad args\n");
	if (g_freePosFails) { *out = nullptr; return; }
	c->x += 1;
	*out = (room == &g_townRoomA) ? static_cast<void*>(&g_townRoomB) : room;
}
static int32_t __fastcall MockCreatePortal(void* game, void* unit, void* room, int32_t x, int32_t y, int32_t dest, void** outP, int32_t obj, int32_t a9, int32_t a10) {
	++n_create; last_create_unit = unit; last_create_room = room; last_create_xy = { x, y }; last_create_dest = dest;
	last_create_out = outP; last_create_obj = obj; last_create_a9 = a9; last_create_a10 = a10;
	if (game != g_game) std::printf("  !! create bad game\n");
	return 1;
}
using IterCb = int32_t(__fastcall*)(void*, void*, void*);
static void __fastcall MockIterate(void* game, int32_t type, void* arg, IterCb cb) {
	if (type != 0 || game != g_game) return;
	for (void* p : g_playersInGame) if (cb(game, p, arg) == 1) return;
}

// ------------------------------------------------------------------ fake host
static std::vector<uint8_t> g_dump;
static uint8_t* g_base = nullptr;
static std::string g_configText; static bool g_configExists = false;
static std::map<uint64_t, void*> g_hooks; static uint64_t g_corruptRva = 0;
struct Listener { D2RL::Lifecycle::GameplayEventKind kind; D2RL::Lifecycle::GameplayEventCallback cb; void* ud; };
static std::vector<Listener> g_listeners;
static bool g_lifecycleAvailable = true;
static D2RL::PluginContext* g_pctx = nullptr;

static void __cdecl HostLog(const D2RL::PluginContext*, const char* m) noexcept { std::printf("  [log] %s\n", m); }
static bool __cdecl HostEnsureConfig(const D2RL::PluginContext*, const char* def) noexcept {
	if (!g_configExists) { g_configText = def; g_configExists = true; } return true;
}
static bool __cdecl HostReadConfig(const D2RL::PluginContext*, char* out, uint32_t size, uint32_t* req) noexcept {
	if (req) *req = static_cast<uint32_t>(g_configText.size() + 1);
	if (g_configText.size() + 1 > size) return false;
	std::memcpy(out, g_configText.c_str(), g_configText.size() + 1); return true;
}
static bool DumpMatches(uint64_t rva, const void* exp, uint32_t size) {
	if (rva + size > g_dump.size()) return false;
	std::vector<uint8_t> real(g_dump.begin() + rva, g_dump.begin() + rva + size);
	if (rva == g_corruptRva) real[0] ^= 0xFF;
	return std::memcmp(real.data(), exp, size) == 0;
}
static bool __cdecl HostCheck(const D2RL::PluginContext*, uint64_t rva, const void* e, uint32_t s) noexcept { return DumpMatches(rva, e, s); }
static bool __cdecl HostHook(const D2RL::PluginContext*, const D2RL::InlineHookRegistration* h) noexcept {
	if (!DumpMatches(h->rva, h->expected, h->expectedSize) || !h->target) return false;
	void* orig = nullptr;
	if (h->rva == 0x5155C0) orig = (void*)&MockOrigChangeLevel;
	else if (h->rva == 0x485350) orig = (void*)&MockOrigSpawn;
	else if (h->rva == 0x406550) orig = (void*)&MockOrigUpdate;
	else return false;
	g_hooks[h->rva] = h->target; if (h->original) *h->original = orig; return true;
}
static D2RL::Lifecycle::Result __cdecl HostRegGameplay(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEventListener* l, D2RL::Lifecycle::ListenerHandle* h) noexcept {
	g_listeners.push_back({ l->kind, l->callback, l->userData }); *h = g_listeners.size(); return D2RL::Lifecycle::Result::Success;
}
static D2RL::LifecycleService g_lifecycle { D2RL::LifecycleServiceSize, D2RL::LifecycleService::AbiVersion, nullptr, nullptr, HostRegGameplay, nullptr, nullptr, nullptr };
static D2RL::ServiceQueryResult __cdecl HostQuery(const D2RL::PluginContext*, D2RL::ServiceId id, uint32_t, const void** out) noexcept {
	if (id == D2RL::ServiceId::Lifecycle && g_lifecycleAvailable) { *out = &g_lifecycle; return D2RL::ServiceQueryResult::Success; }
	return D2RL::ServiceQueryResult::UnknownService;
}
static void FireUi(D2RL::Lifecycle::GameplayEventKind kind, int prev, int cur) {   // UI thread event
	D2RL::Lifecycle::GameplayEvent ev {}; ev.structSize = D2RL::Lifecycle::GameplayEventSize; ev.kind = kind;
	ev.previousValue = prev; ev.currentValue = cur;
	for (auto& l : g_listeners) if (l.kind == kind) l.cb(g_pctx, &ev, l.ud);
}
static void ClientInLevel(int prev, int cur) { FireUi(D2RL::Lifecycle::GameplayEventKind::LevelChanged, prev, cur); }

static void WriteAbsJump(uint64_t rva, void* target) {
	uint8_t* p = g_base + rva; p[0] = 0xFF; p[1] = 0x25; std::memset(p + 2, 0, 4); std::memcpy(p + 6, &target, 8);
}
static void Reset() { n_origChange = n_origSpawn = n_origUpdate = n_cow = n_create = n_setGlobal = 0; last_create_room = nullptr; }
static void NewGame(uint8_t diff, uint8_t ver, bool a4q2, bool a5q6, FakeRoom* playerRoom, bool withBonfire = true) {
	std::memset(g_game, 0, sizeof(g_game)); g_game[0x104] = diff; g_game[0x106] = ver;
	std::memset(g_playerData, 0, sizeof(g_playerData));
	for (int d = 0; d < 3; ++d) { g_flags[d] = { false, false }; void* p = &g_flags[d]; std::memcpy(g_playerData + 0x40 + d * 8, &p, 8); }
	g_flags[diff] = { a4q2, a5q6 };
	SetUnit(g_player, 0, 3); SetUnit(g_bonfire, 2, 39); SetUnit(g_decoyWrongLevel, 2, 39); SetUnit(g_decoyOtherObject, 2, 119);
	g_units.clear();
	g_units[g_player] = { { 25100, 5050 }, playerRoom };
	g_units[g_bonfire] = { { 25000, 5000 }, &g_townRoomA };
	g_units[g_decoyWrongLevel] = { { 1, 1 }, &g_moorRoom };
	g_units[g_decoyOtherObject] = { { 2, 2 }, &g_townRoomA };
	ObjectListClear(); ObjectListAdd(7, g_decoyOtherObject); ObjectListAdd(7, g_decoyWrongLevel);
	if (withBonfire) ObjectListAdd(7, g_bonfire);
	g_playersInGame = { g_player }; g_globalCowOpened = false; g_freePosFails = false; Reset();
	FireUi(D2RL::Lifecycle::GameplayEventKind::GameLeft, 0, 0);   // previous game ended on the client
}
using ChangeFn = void(__fastcall*)(void*, int32_t, int32_t, void*);
using SpawnFn  = uint64_t(__fastcall*)(void*, void*, void*, void*, int32_t, int32_t);
using UpdateFn = void(__fastcall*)(void*);
static uint8_t g_client[0x600];
static uint64_t Spawn() { return reinterpret_cast<SpawnFn>(g_hooks[0x485350])(g_client, g_player, g_game, nullptr, 0, 0); }
static void ServerArrive(int oldLevel) {
	g_units[g_player].room = &g_townRoomA;
	reinterpret_cast<ChangeFn>(g_hooks[0x5155C0])(g_game, oldLevel, 1, g_player);
}
static void Frames(int n, void* game = g_game) { for (int i = 0; i < n; ++i) reinterpret_cast<UpdateFn>(g_hooks[0x406550])(game); }

int main(int argc, char** argv) {
	if (argc < 3) { std::printf("usage: harness <plugin.dll> <d2r_image_93847.bin>\n"); return 2; }
	{ FILE* f = std::fopen(argv[2], "rb"); if (!f) return 2; std::fseek(f, 0, SEEK_END); long n = std::ftell(f);
	  std::fseek(f, 0, SEEK_SET); g_dump.resize(n); std::fread(g_dump.data(), 1, n, f); std::fclose(f); }
	g_base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x600000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	WriteAbsJump(0x5DA2A0, (void*)&MockCreateCowPortal); WriteAbsJump(0x514FD0, (void*)&MockGetGlobal);
	WriteAbsJump(0x5181C0, (void*)&MockSetGlobal);       WriteAbsJump(0x325C50, (void*)&MockGetQuestState);
	WriteAbsJump(0x34B240, (void*)&MockGetPlayerData);   WriteAbsJump(0x34A040, (void*)&MockGetCoords);
	WriteAbsJump(0x34B440, (void*)&MockGetRoom);         WriteAbsJump(0x2EFC10, (void*)&MockLevelFromRoom);
	WriteAbsJump(0x517F90, (void*)&MockGetFreePosition); WriteAbsJump(0x432CE0, (void*)&MockCreatePortal);
	WriteAbsJump(0x490DE0, (void*)&MockIterate);

	HMODULE dll = LoadLibraryA(argv[1]); CHECK(dll != nullptr, "LoadLibrary plugin"); if (!dll) return 1;
	auto getInfo = (D2RL::GetPluginInfoFn)GetProcAddress(dll, D2RL_PLUGIN_GET_INFO_EXPORT);
	auto load = (D2RL::LoadPluginFn)GetProcAddress(dll, D2RL_PLUGIN_LOAD_EXPORT);
	auto unload = (D2RL::UnloadPluginFn)GetProcAddress(dll, D2RL_PLUGIN_UNLOAD_EXPORT);
	const D2RL::PluginInfo* info = getInfo();
	CHECK(info && std::strcmp(info->id, "cow-portal-in-town") == 0 && std::strcmp(info->version, "1.0.0") == 0, "PluginInfo id + version 1.0.0");
	CHECK(D2RL::HasFlag(info->flags, D2RL::PluginFlags::NativeHooks) && D2RL::PluginRoleValue(info->flags) == D2RL::FlagsValue(D2RL::PluginFlags::Server), "flags = Server | NativeHooks");

	D2RL::PluginApi api {}; api.apiSize = D2RL::PluginApiSize; api.logInfo = api.logWarn = api.logError = HostLog;
	api.ensureConfig = HostEnsureConfig; api.readConfig = HostReadConfig; api.checkExpectedBytes = HostCheck;
	api.installInlineHook = HostHook; api.queryService = HostQuery;
	D2RL::PluginContext ctx {}; ctx.contextSize = D2RL::PluginContextSize; ctx.abiVersion = D2RL_PLUGIN_ABI_VERSION; ctx.api = &api;
	ctx.exeBase = (uintptr_t)g_base; ctx.buildName = "93847"; ctx.buildVersion = "3.3.0"; g_pctx = &ctx;
	auto Reload = [&](const char* cfg) { unload(); g_hooks.clear(); g_listeners.clear(); g_configText = cfg; g_configExists = true; return load(&ctx); };

	std::printf("\n--- L1 load default config; all expected bytes checked against REAL dump\n");
	CHECK(load(&ctx), "load returns true");
	CHECK(g_hooks.count(0x5155C0) && g_hooks.count(0x485350) && g_hooks.count(0x406550), "3 hooks installed (UpdateProgress, ChangeLevel, SpawnPlayer)");
	CHECK(g_listeners.size() == 2, "client signal listeners registered (LevelChanged, GameLeft)");
	CHECK(g_configText.find("offset_y = 9") != std::string::npos, "default config offset (6,9)");
	CHECK(g_configText.find("[diagnostics]\n# Log every Rogue Encampment arrival and the decision taken (plugin log file).\n# Startup lines, warnings and errors are always logged.\nenabled = false") != std::string::npos, "default config: diagnostics off");

	std::printf("\n--- B1 REGRESSION (bug 2026-10-26): game starts in Act 5, waypoint to Rogue Encampment, bonfire already there\n");
	NewGame(2, 3, false, true, &g_act5Room); Spawn(); ClientInLevel(-1, 109); Frames(30);
	CHECK(n_create == 0, "nothing while in Act 5");
	g_units[g_player].room = &g_townRoomA; Spawn();   // cross-act waypoint: server places player in Act 1
	Frames(50);                                        // client still loading the act: no LevelChanged yet
	CHECK(n_create == 0 && n_cow == 0, "portal NOT created while the client is still loading (the bug)");
	ClientInLevel(109, 1); Frames(11);
	CHECK(n_create == 0, "not yet within the settle time (11 frames)");
	Frames(1);
	CHECK(n_create == 1 && n_cow == 0, "created 12 frames after the client reported 'in town'");
	CHECK(last_freepos_in.x == 25006 && last_freepos_in.y == 5009 && last_create_xy.x == 25007 && last_create_room == &g_townRoomB, "placed at bonfire + (6,9) via free-position search");
	CHECK(last_create_dest == 39 && last_create_obj == 60 && !last_create_out && !last_create_a9 && !last_create_a10 && last_create_unit == g_player, "CreatePortalObject args like the cube");
	CHECK(n_setGlobal == 1 && last_set_q == 4 && last_set_f == 11, "per-game flag A1Q4/CUSTOM7 set");
	Frames(100);
	CHECK(n_create == 1, "no duplicate on later frames");
	CHECK(n_origUpdate == 192, "original GAME_UpdateProgress called on every frame");

	std::printf("\n--- B2 REGRESSION (bug 2026-10-29): cross-act waypoint that NEITHER hook sees\n");
	NewGame(2, 3, false, true, &g_act5Room); Spawn(); ClientInLevel(-1, 109); Frames(30);
	g_units[g_player].room = &g_townRoomA;            // server moved the player to Act 1, no hook call
	Frames(40);                                        // client loading
	CHECK(n_create == 0, "nothing while the client loads");
	ClientInLevel(109, 1); Frames(17);
	CHECK(n_create == 1 && n_cow == 0, "safety-net scan schedules it; created after the client signal + settle");
	Frames(200);
	CHECK(n_create == 1 && n_cow == 0, "no duplicate from later scans");

	std::printf("\n--- B6 REGRESSION (log 2026-10-29 16:04:44): client reports town BEFORE the scan sees the player\n");
	NewGame(2, 3, false, true, &g_act5Room); Spawn(); ClientInLevel(-1, 109); Frames(30);
	ClientInLevel(109, 1);                             // client finished loading Act 1 first
	g_units[g_player].room = &g_townRoomA;            // then the server places the player (no hook)
	Frames(10);                                        // scan (every 5 frames) schedules; settle starts
	CHECK(n_create == 0, "not instantly (settle time)");
	Frames(8);                                         // <= 5 (scan) + 12 (settle) + 1 frames in total
	CHECK(n_create == 1, "created within ~0.7 s, NOT after the 10 s timeout");

	std::printf("\n--- B3 second session: Act 5 start, waypoint to Act 1 (exact user steps)\n");
	NewGame(2, 3, false, true, &g_townRoomA); Spawn(); ClientInLevel(-1, 1); Frames(12);
	CHECK(n_create == 1, "session 1 (start in Act 1): portal");
	g_units[g_player].room = &g_act5Room; ClientInLevel(1, 109); Frames(20);   // waypoint to Act 5, no hook
	NewGame(2, 3, false, true, &g_act5Room); Spawn(); ClientInLevel(-1, 109); Frames(30);   // save & quit, new session
	g_units[g_player].room = &g_townRoomA; Frames(25); ClientInLevel(109, 1); Frames(17);
	CHECK(n_create == 1, "session 2: portal after cross-act waypoint");

	std::printf("\n--- B4 locked character standing in town: scan stays silent, never calls the native\n");
	NewGame(1, 3, false, false, &g_townRoomA); ClientInLevel(-1, 1); Frames(1000);
	CHECK(n_create == 0 && n_cow == 0, "no portal, no native call, no schedule");

	std::printf("\n--- J1 game start directly in Rogue Encampment\n");
	NewGame(2, 3, false, true, &g_townRoomA, false); uint64_t r = Spawn();
	CHECK(r == 0xC0FFEEULL && spawn_args[0] == g_client && spawn_args[1] == g_player && spawn_args[2] == g_game, "SpawnPlayer: original first, args + return value passed through");
	Frames(3); ClientInLevel(-1, 1); Frames(20);
	CHECK(n_create == 0, "client ready but bonfire not spawned yet -> waiting");
	ObjectListAdd(42, g_bonfire); Frames(1);
	CHECK(n_create == 1, "created on the first frame the bonfire exists");

	std::printf("\n--- W1 in-act waypoint arrival (Cold Plains -> Rogue Encampment)\n");
	NewGame(1, 3, false, true, &g_moorRoom); Spawn(); ClientInLevel(-1, 3); Frames(20);
	ServerArrive(3); ClientInLevel(3, 1); Frames(12);
	CHECK(n_create == 1, "arrival via ChangeLevel -> created after settle");
	ServerArrive(3); ClientInLevel(3, 1); Frames(50);
	CHECK(n_create == 1, "second arrival: already opened, no second portal");

	std::printf("\n--- T1 no client signal (remote TCP/IP client): timeout\n");
	NewGame(2, 3, false, true, &g_townRoomA); Spawn(); Frames(249);
	CHECK(n_create == 0, "nothing before 250 frames");
	Frames(1);
	CHECK(n_create == 1, "created at the 250-frame timeout");

	std::printf("\n--- T2 stale 'in town' signal from the previous stay does not count\n");
	NewGame(2, 3, false, true, &g_moorRoom); Spawn(); ClientInLevel(-1, 1);   // client says town (old event)
	ClientInLevel(1, 3); g_units[g_player].room = &g_townRoomA; Spawn(); Frames(30);
	CHECK(n_create == 0, "no new LevelChanged -> 1 since scheduling -> keep waiting");

	std::printf("\n--- D1 player leaves town before the portal is created, then comes back\n");
	NewGame(2, 3, false, true, &g_act5Room); Spawn(); g_units[g_player].room = &g_townRoomA; Spawn(); Frames(5);
	g_units[g_player].room = &g_act5Room; Frames(5);
	CHECK(n_create == 0, "left town -> schedule dropped");
	g_units[g_player].room = &g_townRoomA; Spawn(); ClientInLevel(109, 1); Frames(12);
	CHECK(n_create == 1, "back in town -> created");

	std::printf("\n--- D2 player leaves the game while scheduled\n");
	NewGame(2, 3, false, true, &g_townRoomA); Spawn(); g_playersInGame.clear(); Frames(300);
	CHECK(n_create == 0 && n_cow == 0, "dropped, nothing created");

	std::printf("\n--- D3 frames of another game are ignored\n");
	NewGame(2, 3, false, true, &g_townRoomA); Spawn(); ClientInLevel(-1, 1); Frames(300, g_otherGame);
	CHECK(n_create == 0, "other game's frames do not create the portal");
	Frames(12);
	CHECK(n_create == 1, "own game's frames do");

	std::printf("\n--- L2 locked difficulty / disabled difficulty\n");
	NewGame(1, 3, false, false, &g_townRoomA); Spawn(); ClientInLevel(-1, 1); Frames(400);
	CHECK(n_create == 0 && n_cow == 0, "locked: no portal, no native call (no sound)");

	std::printf("\n--- F1 bonfire never appears -> fallback next to player at 375 frames\n");
	NewGame(2, 3, false, true, &g_townRoomA, false); Spawn(); ClientInLevel(-1, 1); Frames(374);
	CHECK(n_cow == 0, "still waiting at 374 frames");
	Frames(1);
	CHECK(n_cow == 1 && n_create == 0, "fallback: native cube portal next to player");

	std::printf("\n--- F2 free position not found -> fallback\n");
	NewGame(2, 3, false, true, &g_townRoomA); g_freePosFails = true; Spawn(); ClientInLevel(-1, 1); Frames(12);
	CHECK(n_cow == 1 && n_create == 0, "fallback to native cube portal");

	std::printf("\n--- F3 decoy objects only (class 39 in level 2, waypoint 119)\n");
	NewGame(2, 3, false, true, &g_townRoomA, false); Spawn(); ClientInLevel(-1, 1); Frames(100);
	CHECK(n_create == 0 && n_cow == 0, "decoys ignored -> still waiting");

	std::printf("\n--- C1 Classic game uses A4Q2\n");
	NewGame(0, 1, true, false, &g_townRoomA); Spawn(); ClientInLevel(-1, 1); Frames(12);
	CHECK(n_create == 1, "Classic + Diablo done -> portal");

	std::printf("\n--- K1 config: anchor=player, fallback off, offsets\n");
	CHECK(Reload("[placement]\nanchor = \"player\" # comment\n[diagnostics]\nenabled = false\n"), "reload anchor=player");
	NewGame(2, 3, false, true, &g_townRoomA); Spawn(); Frames(5);
	CHECK(n_cow == 0, "anchor=player also waits for the client");
	ClientInLevel(-1, 1); Frames(12);
	CHECK(n_cow == 1 && n_create == 0, "anchor=player -> native cube function when ready");
	CHECK(Reload("[placement]\nanchor = \"elsewhere\"\noffset_x = -4\noffset_y = 10\nfallback_to_player = false\n"), "reload offsets");
	NewGame(2, 3, false, true, &g_townRoomA); Spawn(); ClientInLevel(-1, 1); Frames(12);
	CHECK(n_create == 1 && last_freepos_in.x == 24996 && last_freepos_in.y == 5010, "invalid anchor -> bonfire; offset (-4,10)");
	NewGame(2, 3, false, true, &g_townRoomA, false); Spawn(); ClientInLevel(-1, 1); Frames(400);
	CHECK(n_create == 0 && n_cow == 0, "fallback_to_player=false -> no portal when bonfire missing");
	ClientInLevel(1, 3); ClientInLevel(3, 1); Frames(1000);
	CHECK(n_create == 0 && n_cow == 0, "after giving up: no rescheduling in the same game (no log/sound spam)");

	std::printf("\n--- K2 lifecycle service unavailable -> timeout path\n");
	g_lifecycleAvailable = false; CHECK(Reload(""), "reload without lifecycle");
	NewGame(2, 3, false, true, &g_townRoomA); Spawn(); Frames(249);
	CHECK(n_create == 0, "waits for timeout");
	Frames(1);
	CHECK(n_create == 1, "created at timeout");
	g_lifecycleAvailable = true;

	std::printf("\n--- K4 packaged default config with CRLF line endings (release zip) parses like the default\n");
	{
		std::string crlf; for (char c : std::string(
			"[cow_portal]\nenabled = true\nnormal = true\nnightmare = true\nhell = false\n\n[placement]\nanchor = \"bonfire\"\noffset_x = 6\noffset_y = 9\nfallback_to_player = true\n\n[diagnostics]\nenabled = false\n")) {
			if (c == '\n') crlf += '\r'; crlf += c; }
		CHECK(Reload(crlf.c_str()), "reload CRLF config");
		NewGame(2, 3, false, true, &g_townRoomA); Spawn(); ClientInLevel(-1, 1); Frames(30);
		CHECK(n_create == 0, "CRLF: hell = false honoured");
		NewGame(0, 3, false, true, &g_townRoomA); Spawn(); ClientInLevel(-1, 1); Frames(12);
		CHECK(n_create == 1 && last_freepos_in.x == 25006 && last_freepos_in.y == 5009, "CRLF: normal on, offset (6,9), anchor bonfire");
	}

	std::printf("\n--- K3 enabled=false\n");
	CHECK(Reload("[cow_portal]\nenabled = false\n") && g_hooks.empty() && g_listeners.empty(), "no hooks/listeners when disabled");

	std::printf("\n--- V1 expected-byte mismatch / wrong build\n");
	g_corruptRva = 0x406550; CHECK(!Reload("") && g_hooks.empty(), "UpdateProgress mismatch -> load fails, no hooks");
	g_corruptRva = 0x510E8D; CHECK(!Reload("") && g_hooks.empty(), "layout anchor mismatch -> load fails, no hooks");
	g_corruptRva = 0; ctx.buildName = "99999"; CHECK(!Reload("") && g_hooks.empty(), "wrong build -> load fails, no hooks");
	unload();

	std::printf("\n%s: %d passed, %d failed\n", g_fail == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", g_pass, g_fail);
	return g_fail == 0 ? 0 : 1;
}
