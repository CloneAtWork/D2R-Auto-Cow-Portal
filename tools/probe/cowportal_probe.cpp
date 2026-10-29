// cowportal-probe — diagnostic D2RLoader plugin (Step 1 of the Cow Portal in Town project)
//
// PURPOSE
//   D2R.exe is encrypted on disk (.text entropy 8.0), so it cannot be disassembled
//   statically. D2RLoader maps a decrypted runtime image at exeBase before plugins load.
//   This probe:
//     1. Logs the PluginContext fields (ABI, exeBase, build, paths).
//     2. Logs the PE section table read from the *in-memory* image.
//     3. Dumps the mapped image [exeBase, exeBase + extent) to a .bin file once per build,
//        plus a .txt sidecar with the memory-region map and per-section FNV-1a hashes.
//     4. Logs gameplay lifecycle events (LocalPlayerReady, ActChanged, LevelChanged,
//        QuestCompleted, GameJoined, GameLeft) to confirm their real values in 3.3.0.
//     5. On the first LocalPlayerReady, re-hashes the sections and logs whether code
//        changed after load (detects lazy decryption or runtime patching).
//
// SAFETY
//   - Read-only: no hooks, no byte patches, no writes to game memory.
//   - Memory is read with ReadProcessMemory on the current process so an unreadable
//     page fails gracefully (written as zeros and recorded in the region map) instead
//     of crashing the game.
//   - An existing dump is never overwritten. Delete it manually to force a new dump.
//   - Role = Client, so it is not recorded in character plugin history.

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace {

constexpr const char* kPluginId      = "cowportal-probe";
constexpr const char* kPluginVersion = "0.1.0";

constexpr D2RL::PluginInfo kPluginInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = kPluginId,
	.name        = "Cow Portal Probe (diagnostic)",
	.version     = kPluginVersion,
	.author      = "Alex",
	.description = "Read-only diagnostics: dumps the mapped D2R image and logs lifecycle events.",
	.flags       = D2RL::PluginFlags::Client,
};

struct SectionInfo {
	char     name[9];
	uint32_t va;
	uint32_t vsize;
	uint32_t characteristics;
	uint64_t hashAtLoad;
};

const D2RL::PluginContext* g_ctx = nullptr;
std::vector<SectionInfo>   g_sections;
bool                       g_rehashDone = false;

// ---------------------------------------------------------------------------
// Logging helpers
// ---------------------------------------------------------------------------
void Log(const char* fmt, ...) noexcept {
	if (g_ctx == nullptr) {
		return;
	}
	char    buffer[1024];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	g_ctx->LogInfo(buffer);
}

void LogWarn(const char* fmt, ...) noexcept {
	if (g_ctx == nullptr) {
		return;
	}
	char    buffer[1024];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	g_ctx->LogWarn(buffer);
}

std::string Narrow(const wchar_t* text) {
	if (text == nullptr) {
		return "<null>";
	}
	const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (needed <= 0) {
		return "<conv-fail>";
	}
	std::string out(static_cast<size_t>(needed - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
	return out;
}

const char* Safe(const char* text) noexcept { return text != nullptr ? text : "<null>"; }

// ---------------------------------------------------------------------------
// Memory helpers (never dereference game memory directly)
// ---------------------------------------------------------------------------
bool SafeRead(uintptr_t address, void* out, size_t size) noexcept {
	SIZE_T read = 0;
	return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size, &read) != 0 && read == size;
}

bool IsReadableProtect(DWORD protect) noexcept {
	if ((protect & PAGE_GUARD) != 0 || (protect & PAGE_NOACCESS) != 0) {
		return false;
	}
	const DWORD base = protect & 0xFF;
	return base == PAGE_READONLY || base == PAGE_READWRITE || base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READ ||
	       base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
}

// FNV-1a 64-bit over a memory range, read in chunks. Returns 0 if any chunk failed.
uint64_t HashRange(uintptr_t start, size_t size) noexcept {
	uint64_t                     hash = 1469598103934665603ULL;
	static std::array<uint8_t, 1 << 16> chunk;
	size_t                       done = 0;
	while (done < size) {
		const size_t n = (size - done) < chunk.size() ? (size - done) : chunk.size();
		if (!SafeRead(start + done, chunk.data(), n)) {
			return 0;
		}
		for (size_t i = 0; i < n; ++i) {
			hash ^= chunk[i];
			hash *= 1099511628211ULL;
		}
		done += n;
	}
	return hash;
}

// ---------------------------------------------------------------------------
// PE header parsing of the in-memory image
// ---------------------------------------------------------------------------
bool ReadSections(uintptr_t base, uint32_t* sizeOfImage) noexcept {
	IMAGE_DOS_HEADER dos {};
	if (!SafeRead(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
		LogWarn("PE: no MZ header at exeBase 0x%llX.", static_cast<unsigned long long>(base));
		return false;
	}
	IMAGE_NT_HEADERS64 nt {};
	if (!SafeRead(base + static_cast<uintptr_t>(dos.e_lfanew), &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) {
		LogWarn("PE: no PE header at e_lfanew 0x%lX.", static_cast<long>(dos.e_lfanew));
		return false;
	}
	*sizeOfImage = nt.OptionalHeader.SizeOfImage;
	Log("PE: ImageBase(header)=0x%llX SizeOfImage=0x%X EntryRVA=0x%X Sections=%u TimeDateStamp=0x%08X",
		static_cast<unsigned long long>(nt.OptionalHeader.ImageBase), nt.OptionalHeader.SizeOfImage,
		nt.OptionalHeader.AddressOfEntryPoint, nt.FileHeader.NumberOfSections, nt.FileHeader.TimeDateStamp);

	const uintptr_t sectionTable = base + static_cast<uintptr_t>(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
	                               nt.FileHeader.SizeOfOptionalHeader;
	g_sections.clear();
	for (uint16_t i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
		IMAGE_SECTION_HEADER sh {};
		if (!SafeRead(sectionTable + i * sizeof(IMAGE_SECTION_HEADER), &sh, sizeof(sh))) {
			LogWarn("PE: section header %u unreadable.", i);
			break;
		}
		SectionInfo info {};
		std::memcpy(info.name, sh.Name, 8);
		info.name[8]         = '\0';
		info.va              = sh.VirtualAddress;
		info.vsize           = sh.Misc.VirtualSize;
		info.characteristics = sh.Characteristics;
		info.hashAtLoad      = HashRange(base + info.va, info.vsize);
		g_sections.push_back(info);
		Log("PE: section %-8s VA=0x%08X VSize=0x%08X Chars=0x%08X FNV64=0x%016llX", info.name, info.va, info.vsize,
			info.characteristics, static_cast<unsigned long long>(info.hashAtLoad));
	}
	return true;
}

// ---------------------------------------------------------------------------
// Dump
// ---------------------------------------------------------------------------
std::wstring DumpDirectory() {
	// pluginLogPath is a file path (…\d2rloader\logs\<id>.log). Use its folder + "\cowportal-probe".
	std::wstring dir;
	if (g_ctx->pluginLogPath != nullptr) {
		dir = g_ctx->pluginLogPath;
		const size_t slash = dir.find_last_of(L"\\/");
		dir = (slash == std::wstring::npos) ? std::wstring(L".") : dir.substr(0, slash);
	} else if (g_ctx->scopeRootDirectory != nullptr) {
		dir = std::wstring(g_ctx->scopeRootDirectory) + L"\\logs";
	} else {
		dir = L".";
	}
	dir += L"\\cowportal-probe";
	return dir;
}

void DumpImage(uintptr_t base, uint32_t headerSizeOfImage) {
	const std::wstring dir = DumpDirectory();
	CreateDirectoryW(dir.c_str(), nullptr); // ok if it already exists

	const std::string  build    = Safe(g_ctx->buildName);
	const std::wstring wbuild(build.begin(), build.end());
	const std::wstring binPath  = dir + L"\\d2r_image_" + wbuild + L".bin";
	const std::wstring mapPath  = dir + L"\\d2r_image_" + wbuild + L".txt";

	if (GetFileAttributesW(binPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
		Log("Dump: %s already exists, not overwritten (delete it to re-dump).", Narrow(binPath.c_str()).c_str());
		return;
	}

	// Determine the extent: walk regions that belong to the same allocation as exeBase,
	// and take the larger of that and the header SizeOfImage.
	struct Region {
		uintptr_t start;
		size_t    size;
		DWORD     state;
		DWORD     protect;
		DWORD     type;
		bool      readable;
	};
	std::vector<Region> regions;
	uintptr_t           cursor   = base;
	uintptr_t           allocEnd = base;
	for (;;) {
		MEMORY_BASIC_INFORMATION mbi {};
		if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) == 0) {
			break;
		}
		if (reinterpret_cast<uintptr_t>(mbi.AllocationBase) != base) {
			break;
		}
		allocEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
		cursor   = allocEnd;
	}
	uintptr_t end = base + headerSizeOfImage;
	if (allocEnd > end) {
		end = allocEnd;
	}
	Log("Dump: allocation extent=0x%llX, header SizeOfImage=0x%X, dumping 0x%llX bytes.",
		static_cast<unsigned long long>(allocEnd - base), headerSizeOfImage, static_cast<unsigned long long>(end - base));

	// Build the region map over [base, end).
	cursor = base;
	while (cursor < end) {
		MEMORY_BASIC_INFORMATION mbi {};
		if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) == 0) {
			regions.push_back({ cursor, static_cast<size_t>(end - cursor), 0, 0, 0, false });
			break;
		}
		uintptr_t rStart = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
		uintptr_t rEnd   = rStart + mbi.RegionSize;
		if (rStart < cursor) {
			rStart = cursor;
		}
		if (rEnd > end) {
			rEnd = end;
		}
		const bool readable = mbi.State == MEM_COMMIT && IsReadableProtect(mbi.Protect);
		regions.push_back({ rStart, static_cast<size_t>(rEnd - rStart), mbi.State, mbi.Protect, mbi.Type, readable });
		cursor = rEnd;
	}

	HANDLE file = CreateFileW(binPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		LogWarn("Dump: could not create %s (error %lu).", Narrow(binPath.c_str()).c_str(), GetLastError());
		return;
	}

	std::vector<uint8_t> chunk(1 << 20);
	uint64_t             zeroFilledBytes = 0;
	uint64_t             failedReads     = 0;
	bool                 writeOk         = true;
	for (const Region& r : regions) {
		size_t done = 0;
		while (done < r.size && writeOk) {
			const size_t n  = (r.size - done) < chunk.size() ? (r.size - done) : chunk.size();
			bool         ok = r.readable && SafeRead(r.start + done, chunk.data(), n);
			if (!ok) {
				std::memset(chunk.data(), 0, n);
				zeroFilledBytes += n;
				if (r.readable) {
					++failedReads;
				}
			}
			DWORD written = 0;
			writeOk       = WriteFile(file, chunk.data(), static_cast<DWORD>(n), &written, nullptr) != 0 && written == n;
			done += n;
		}
	}
	CloseHandle(file);
	if (!writeOk) {
		LogWarn("Dump: write failed (error %lu). The .bin file is incomplete; delete it.", GetLastError());
		return;
	}

	// Sidecar text file: build info, sections, region map.
	FILE* map = _wfopen(mapPath.c_str(), L"w");
	if (map != nullptr) {
		std::fprintf(map, "cowportal-probe %s dump sidecar\n", kPluginVersion);
		std::fprintf(map, "buildVersion=%s buildName=%s abi=%u exeBase=0x%llX\n", Safe(g_ctx->buildVersion), Safe(g_ctx->buildName),
			g_ctx->abiVersion, static_cast<unsigned long long>(base));
		std::fprintf(map, "dumpBytes=0x%llX headerSizeOfImage=0x%X allocExtent=0x%llX zeroFilled=0x%llX failedReads=%llu\n\n",
			static_cast<unsigned long long>(end - base), headerSizeOfImage, static_cast<unsigned long long>(allocEnd - base),
			static_cast<unsigned long long>(zeroFilledBytes), static_cast<unsigned long long>(failedReads));
		std::fprintf(map, "[sections]  name VA VSize Characteristics FNV64(at load)\n");
		for (const SectionInfo& s : g_sections) {
			std::fprintf(map, "%-8s 0x%08X 0x%08X 0x%08X 0x%016llX\n", s.name, s.va, s.vsize, s.characteristics,
				static_cast<unsigned long long>(s.hashAtLoad));
		}
		std::fprintf(map, "\n[regions]  RVA Size State Protect Type Readable\n");
		for (const Region& r : regions) {
			std::fprintf(map, "0x%08llX 0x%08llX 0x%05lX 0x%03lX 0x%07lX %s\n", static_cast<unsigned long long>(r.start - base),
				static_cast<unsigned long long>(r.size), r.state, r.protect, r.type, r.readable ? "yes" : "NO");
		}
		std::fclose(map);
	}

	Log("Dump: wrote %s (0x%llX bytes, zero-filled 0x%llX, failed reads %llu).", Narrow(binPath.c_str()).c_str(),
		static_cast<unsigned long long>(end - base), static_cast<unsigned long long>(zeroFilledBytes),
		static_cast<unsigned long long>(failedReads));
}

// ---------------------------------------------------------------------------
// Lifecycle events
// ---------------------------------------------------------------------------
const char* EventName(D2RL::Lifecycle::GameplayEventKind kind) noexcept {
	using K = D2RL::Lifecycle::GameplayEventKind;
	switch (kind) {
		case K::GameJoined:       return "GameJoined";
		case K::GameLeft:         return "GameLeft";
		case K::LocalPlayerReady: return "LocalPlayerReady";
		case K::ActChanged:       return "ActChanged";
		case K::LevelChanged:     return "LevelChanged";
		case K::QuestCompleted:   return "QuestCompleted";
		default:                  return "Other";
	}
}

void RehashAfterReady() noexcept {
	if (g_rehashDone || g_ctx == nullptr) {
		return;
	}
	g_rehashDone = true;
	for (const SectionInfo& s : g_sections) {
		const uint64_t now = HashRange(g_ctx->exeBase + s.va, s.vsize);
		Log("Rehash: %-8s load=0x%016llX ready=0x%016llX %s", s.name, static_cast<unsigned long long>(s.hashAtLoad),
			static_cast<unsigned long long>(now), now == s.hashAtLoad ? "same" : "CHANGED");
	}
}

void __cdecl OnGameplayEvent(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	if (context == nullptr || !D2RL::Lifecycle::HasGameplayEventField(event, D2RL::Lifecycle::GameplayEventRequiredSize)) {
		return;
	}
	Log("Event: %s player=%u session=%llu prev=%d cur=%d difficulty=%u questRow=%u flags=0x%X", EventName(event->kind),
		event->playerId, static_cast<unsigned long long>(event->sessionGeneration), event->previousValue, event->currentValue,
		event->difficulty, event->questRecordId, event->flags);
	if (event->kind == D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady) {
		RehashAfterReady();
	}
}

bool RegisterEvents() noexcept {
	const D2RL::LifecycleService* lifecycle = nullptr;
	const auto                    q         = g_ctx->QueryService(&lifecycle);
	if (q != D2RL::ServiceQueryResult::Success || !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
		LogWarn("Lifecycle service unavailable (query result %u); events will not be logged.", static_cast<unsigned>(q));
		return false;
	}
	using K = D2RL::Lifecycle::GameplayEventKind;
	constexpr std::array kinds { K::GameJoined, K::GameLeft, K::LocalPlayerReady, K::ActChanged, K::LevelChanged, K::QuestCompleted };
	for (const auto kind : kinds) {
		const D2RL::Lifecycle::GameplayEventListener listener {
			.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
			.kind       = kind,
			.callback   = OnGameplayEvent,
		};
		D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
		const auto                      r      = lifecycle->registerGameplayEventListener(g_ctx, &listener, &handle);
		if (r != D2RL::Lifecycle::Result::Success) {
			LogWarn("Could not register listener for %s (result %u).", EventName(kind), static_cast<unsigned>(r));
		}
	}
	return true;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &kPluginInfo; }

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	g_ctx = context;

	// Context fields first, so a version mismatch is visible even if later steps fail.
	Log("cowportal-probe %s loaded. contextSize=%u (SDK expects %u) abi=%u scope=%u", kPluginVersion, context->contextSize,
		D2RL::PluginContextSize, context->abiVersion, static_cast<unsigned>(context->loadScope));
	if (!D2RL::HasContext(context) || context->GetApi() == nullptr) {
		// LogInfo itself depends on GetApi(); nothing else can be reported in this case.
		return false;
	}
	Log("exeBase=0x%llX buildVersion=%s buildName=%s activeMod=%s runtimeFlags=0x%X launchPlatform=%u modDataVersionBuild=%u",
		static_cast<unsigned long long>(context->exeBase), Safe(context->buildVersion), Safe(context->buildName),
		Safe(context->activeMod), context->runtimeFlags, context->launchPlatform, context->modDataVersionBuild);
	Log("pluginPath=%s", Narrow(context->pluginPath).c_str());
	Log("pluginLogPath=%s", Narrow(context->pluginLogPath).c_str());
	Log("scopeRootDirectory=%s", Narrow(context->scopeRootDirectory).c_str());

	uint32_t sizeOfImage = 0;
	if (ReadSections(context->exeBase, &sizeOfImage)) {
		DumpImage(context->exeBase, sizeOfImage);
	}
	RegisterEvents();
	return true; // diagnostics never block the game from starting
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	g_ctx = nullptr;
}
