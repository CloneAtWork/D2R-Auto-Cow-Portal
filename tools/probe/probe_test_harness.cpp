// Test harness for cowportal-probe.dll (runs under Wine or Windows, NOT inside D2R).
// Simulates the D2RLoader host: fake PluginApi + LifecycleService, exeBase = this exe.
// Checks: plugin loads, dump file is created, its size matches the image extent, its bytes
// match this process's memory for the readable header/.text, a second load does not overwrite,
// and lifecycle listeners receive events.
#include <D2RLPlugin/api.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, msg)                                         \
	do {                                                         \
		if (cond) std::printf("PASS  %s\n", msg);                \
		else { std::printf("FAIL  %s\n", msg); ++g_fail; }       \
	} while (0)

static void __cdecl HostLog(const D2RL::PluginContext*, const char* m) noexcept { std::printf("  [log] %s\n", m); }

struct Listener { D2RL::Lifecycle::GameplayEventKind kind; D2RL::Lifecycle::GameplayEventCallback cb; void* ud; };
static std::vector<Listener> g_listeners;

static D2RL::Lifecycle::Result __cdecl RegGameplay(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEventListener* l, D2RL::Lifecycle::ListenerHandle* h) noexcept {
	g_listeners.push_back({ l->kind, l->callback, l->userData });
	*h = g_listeners.size();
	return D2RL::Lifecycle::Result::Success;
}
static D2RL::LifecycleService g_lifecycle { D2RL::LifecycleServiceSize, D2RL::LifecycleService::AbiVersion, nullptr, nullptr, RegGameplay, nullptr, nullptr, nullptr };

static D2RL::ServiceQueryResult __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id, uint32_t, const void** out) noexcept {
	if (id == D2RL::ServiceId::Lifecycle) { *out = &g_lifecycle; return D2RL::ServiceQueryResult::Success; }
	return D2RL::ServiceQueryResult::UnknownService;
}

int main(int argc, char** argv) {
	if (argc < 3) { std::printf("usage: harness <probe.dll> <workdir>\n"); return 2; }
	const std::string work = argv[2];
	CreateDirectoryA(work.c_str(), nullptr);
	CreateDirectoryA((work + "\\logs").c_str(), nullptr);
	std::wstring wlog(work.begin(), work.end()); wlog += L"\\logs\\cowportal-probe.log";

	HMODULE dll = LoadLibraryA(argv[1]);
	CHECK(dll != nullptr, "LoadLibrary probe dll");
	if (!dll) return 1;
	auto getInfo = reinterpret_cast<D2RL::GetPluginInfoFn>(GetProcAddress(dll, D2RL_PLUGIN_GET_INFO_EXPORT));
	auto load    = reinterpret_cast<D2RL::LoadPluginFn>(GetProcAddress(dll, D2RL_PLUGIN_LOAD_EXPORT));
	auto unload  = reinterpret_cast<D2RL::UnloadPluginFn>(GetProcAddress(dll, D2RL_PLUGIN_UNLOAD_EXPORT));
	CHECK(getInfo && load && unload, "three exports resolved");
	const D2RL::PluginInfo* info = getInfo();
	CHECK(info && info->abiVersion == D2RL_PLUGIN_ABI_VERSION && std::strcmp(info->id, "cowportal-probe") == 0, "PluginInfo id/abi");
	CHECK(info && D2RL::PluginRoleValue(info->flags) == D2RL::FlagsValue(D2RL::PluginFlags::Client), "role = Client");

	D2RL::PluginApi api {};
	api.apiSize = D2RL::PluginApiSize; api.logInfo = HostLog; api.logWarn = HostLog; api.logError = HostLog; api.queryService = Query;
	D2RL::PluginContext ctx {};
	ctx.contextSize = D2RL::PluginContextSize; ctx.abiVersion = D2RL_PLUGIN_ABI_VERSION; ctx.api = &api;
	ctx.loadScope = D2RL::LoadScope::Global; ctx.exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
	ctx.pluginId = "cowportal-probe"; ctx.buildVersion = "0.0.0"; ctx.buildName = "harness"; ctx.pluginLogPath = wlog.c_str();

	CHECK(load(&ctx), "load returns true");

	const std::string bin = work + "\\logs\\cowportal-probe\\d2r_image_harness.bin";
	HANDLE f = CreateFileA(bin.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	CHECK(f != INVALID_HANDLE_VALUE, "dump file exists");
	if (f != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER sz {}; GetFileSizeEx(f, &sz);
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(ctx.exeBase);
		auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS64*>(ctx.exeBase + dos->e_lfanew);
		std::printf("  dump size=0x%llX SizeOfImage=0x%lX\n", (unsigned long long)sz.QuadPart, (unsigned long)nt->OptionalHeader.SizeOfImage);
		CHECK((uint64_t)sz.QuadPart >= nt->OptionalHeader.SizeOfImage, "dump size >= SizeOfImage");
		std::vector<uint8_t> data((size_t)sz.QuadPart); DWORD rd = 0;
		ReadFile(f, data.data(), (DWORD)data.size(), &rd, nullptr); CloseHandle(f);
		CHECK(rd == data.size() && data[0] == 'M' && data[1] == 'Z', "dump starts with MZ");
		auto* sec = IMAGE_FIRST_SECTION(nt);
		const uint8_t* text = reinterpret_cast<const uint8_t*>(ctx.exeBase + sec->VirtualAddress);
		CHECK(std::memcmp(data.data() + sec->VirtualAddress, text, sec->Misc.VirtualSize) == 0, "first section bytes in dump == memory (RVA layout preserved)");
	}
	const std::string txt = work + "\\logs\\cowportal-probe\\d2r_image_harness.txt";
	CHECK(GetFileAttributesA(txt.c_str()) != INVALID_FILE_ATTRIBUTES, "sidecar .txt exists");
	CHECK(g_listeners.size() == 6, "6 lifecycle listeners registered");

	D2RL::Lifecycle::GameplayEvent ev {};
	ev.structSize = D2RL::Lifecycle::GameplayEventSize; ev.kind = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady; ev.difficulty = 2;
	for (auto& l : g_listeners) if (l.kind == ev.kind) l.cb(&ctx, &ev, l.ud);
	ev.kind = D2RL::Lifecycle::GameplayEventKind::LevelChanged; ev.previousValue = -1; ev.currentValue = 1;
	for (auto& l : g_listeners) if (l.kind == ev.kind) l.cb(&ctx, &ev, l.ud);

	// Second load must not overwrite the dump.
	FILETIME before {}, after {};
	HANDLE h1 = CreateFileA(bin.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr); GetFileTime(h1, nullptr, nullptr, &before); CloseHandle(h1);
	Sleep(50);
	unload(); g_listeners.clear();
	CHECK(load(&ctx), "second load returns true");
	HANDLE h2 = CreateFileA(bin.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr); GetFileTime(h2, nullptr, nullptr, &after); CloseHandle(h2);
	CHECK(CompareFileTime(&before, &after) == 0, "existing dump not overwritten");
	unload();

	std::printf("\n%s (%d failure(s))\n", g_fail == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", g_fail);
	return g_fail == 0 ? 0 : 1;
}
