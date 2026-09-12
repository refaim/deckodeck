#include "LeakCheck.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// PSAPI_VERSION 2 binds GetProcessMemoryInfo to K32GetProcessMemoryInfo in kernel32 (Windows 7+),
// so the test support links nothing beyond what the plugin itself imports.
#define PSAPI_VERSION 2
#include <Windows.h>

#include <psapi.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include <doctest/doctest.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PVDKIT_TEST_UNDER_ASAN 1
#endif
#endif
#if !defined(PVDKIT_TEST_UNDER_ASAN) && defined(__SANITIZE_ADDRESS__)
#define PVDKIT_TEST_UNDER_ASAN 1
#endif
#if !defined(PVDKIT_TEST_UNDER_ASAN)
#define PVDKIT_TEST_UNDER_ASAN 0
#endif

namespace pvdkit::test
{
    namespace
    {

        // Busy blocks of the process heap: count and payload bytes. The heap is locked for the
        // walk; HeapWalk ends with ERROR_NO_MORE_ITEMS when the whole heap was seen and with
        // anything else when the walk broke off, which the caller treats as a test failure rather
        // than as a zero.
        struct HeapCensus
        {
            std::uint64_t blocks = 0;
            std::uint64_t bytes = 0;
            std::uint64_t committed = 0; // bytes committed to the heap's regions
            DWORD error = ERROR_SUCCESS;
        };

        // The block records live in two buffers reserved once, up front, and never grown: nothing
        // may allocate while the heap is locked and walked, and a buffer allocated per snapshot
        // would itself be one more busy block in the "after" walk than in the "before" one. The
        // two snapshots of one measurement alternate between the buffers, so the "before" records
        // stay intact while the "after" walk fills the other. More blocks than fit are counted but
        // not recorded (the description then says so).
        constexpr std::size_t kMaxRecordedBlocks = std::size_t{1} << 17;

        std::vector<HeapBlock> &recordBuffer()
        {
            static std::array<std::vector<HeapBlock>, 2> buffers = [] {
                std::array<std::vector<HeapBlock>, 2> reserved;
                for (auto &buffer : reserved) {
                    buffer.reserve(kMaxRecordedBlocks);
                }
                return reserved;
            }();
            static std::size_t next = 0;
            auto &buffer = buffers[next];
            next = (next + 1) % buffers.size();
            return buffer;
        }

        HeapBlock record(const PROCESS_HEAP_ENTRY &entry)
        {
            HeapBlock block;
            block.address = reinterpret_cast<std::uintptr_t>(entry.lpData);
            block.size = entry.cbData;
            const auto *bytes = static_cast<const std::byte *>(entry.lpData);
            const auto count = std::min<std::size_t>(entry.cbData, block.head.size());
            std::copy_n(bytes, count, block.head.begin());
            return block;
        }

        HeapCensus walkProcessHeap(std::vector<HeapBlock> &records)
        {
            HeapCensus census;
            records.clear();
            const HANDLE heap = GetProcessHeap();
            if (HeapLock(heap) == FALSE) {
                census.error = GetLastError();
                return census;
            }
            PROCESS_HEAP_ENTRY entry{};
            entry.lpData = nullptr;
            while (HeapWalk(heap, &entry) != FALSE) {
                if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
                    ++census.blocks;
                    census.bytes += entry.cbData;
                    if (records.size() < records.capacity()) {
                        records.push_back(record(entry));
                    }
                }
                if ((entry.wFlags & PROCESS_HEAP_REGION) != 0) {
                    census.committed += entry.Region.dwCommittedSize;
                }
            }
            const DWORD walkError = GetLastError();
            static_cast<void>(HeapUnlock(heap));
            census.error = walkError == ERROR_NO_MORE_ITEMS ? ERROR_SUCCESS : walkError;
            return census;
        }

        // The committed MEM_MAPPED regions of the address space: every view of a file mapping
        // (file-backed or pagefile-backed), whatever module mapped it. VirtualQuery steps through
        // the whole range; free and reserved space is skipped in one step per region.
        struct MappedCensus
        {
            std::uint64_t views = 0;
            std::uint64_t bytes = 0;
        };

        MappedCensus walkMappedViews()
        {
            MappedCensus census;
            SYSTEM_INFO system{};
            GetSystemInfo(&system);
            const auto *address = static_cast<const std::byte *>(system.lpMinimumApplicationAddress);
            const auto *const end = static_cast<const std::byte *>(system.lpMaximumApplicationAddress);
            MEMORY_BASIC_INFORMATION region{};
            while (address < end && VirtualQuery(address, &region, sizeof(region)) == sizeof(region)) {
                if (region.State == MEM_COMMIT && region.Type == MEM_MAPPED) {
                    ++census.views;
                    census.bytes += region.RegionSize;
                }
                address = static_cast<const std::byte *>(region.BaseAddress) + region.RegionSize;
            }
            return census;
        }

        std::string withSign(const std::int64_t value)
        {
            return (value >= 0 ? "+" : "") + std::to_string(value);
        }

        std::string kibibytes(const std::int64_t bytes)
        {
            return withSign(bytes / 1024) + " KiB";
        }

        ResourceUsage readResources(std::vector<HeapBlock> &records)
        {
            const auto census = walkProcessHeap(records);
            const DWORD heapWalkError = census.error;
            CAPTURE(heapWalkError);
            REQUIRE(census.error == ERROR_SUCCESS);
            // The listing merges the two snapshots by address; the walk hands out the heap's
            // segments in address order but VirtualAlloc'd blocks (above 512 KiB, e.g. the two
            // record buffers themselves) last, so sort - outside the heap lock, no allocation.
            std::sort(records.begin(), records.end(),
                      [](const HeapBlock &a, const HeapBlock &b) { return a.address < b.address; });

            DWORD handles = 0;
            REQUIRE(GetProcessHandleCount(GetCurrentProcess(), &handles) != FALSE);

            const auto mapped = walkMappedViews();

            PROCESS_MEMORY_COUNTERS_EX counters{};
            counters.cb = sizeof(counters);
            REQUIRE(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                                         sizeof(counters)) != FALSE);

            ResourceUsage usage;
            usage.heapBlocks = census.blocks;
            usage.heapBytes = census.bytes;
            usage.handles = handles;
            usage.mappedViews = mapped.views;
            usage.mappedBytes = mapped.bytes;
            usage.privateBytes = counters.PrivateUsage;
            usage.heapCommitted = census.committed;
            usage.blocks = records;
            return usage;
        }

        bool sameCounters(const ResourceUsage &a, const ResourceUsage &b) noexcept
        {
            return a.heapBlocks == b.heapBlocks && a.heapBytes == b.heapBytes && a.handles == b.handles &&
                   a.mappedViews == b.mappedViews && a.mappedBytes == b.mappedBytes &&
                   a.privateBytes == b.privateBytes && a.heapCommitted == b.heapCommitted;
        }

        std::string hexAddress(const std::uintptr_t address)
        {
            char digits[2 * sizeof(address) + 1] = {};
            const auto [end, error] = std::to_chars(digits, digits + 2 * sizeof(address), address, 16);
            static_cast<void>(error);
            std::string text{digits, end};
            return "0x" + std::string(2 * sizeof(address) - text.size(), '0') + text;
        }

        std::string hex(const std::span<const std::byte> bytes)
        {
            static constexpr char kDigits[] = "0123456789abcdef";
            std::string text;
            for (const std::byte value : bytes) {
                const auto v = std::to_integer<unsigned>(value);
                text += kDigits[v >> 4];
                text += kDigits[v & 0xF];
                text += ' ';
            }
            if (!text.empty()) {
                text.pop_back();
            }
            return text;
        }

        // The one process-heap block Windows itself allocates behind a plugin's back: ntdll gives
        // a critical section its RTL_CRITICAL_SECTION_DEBUG on the FIRST CONTENDED acquisition
        // (DebugInfo is -1 after InitializeCriticalSection and after any number of uncontended
        // uses) - from a static pool of 64 while that lasts, from the process heap afterwards -
        // and keeps it as long as the section lives; DeleteCriticalSection zeroes the block and
        // keeps it busy for the next section to reuse (measured on this machine, x64 and x86).
        // The plugin DLL's UCRT locks (__acrt_locale_lock, __acrt_multibyte_cp_lock) are contended
        // whenever several threads start using the CRT at once - eight host threads, or dav1d's
        // workers - so whichever pass sees the first contention of a lock sees +1 block that is
        // neither the plugin's nor a leak. A live one is recognisable beyond doubt: exactly
        // sizeof(RTL_CRITICAL_SECTION_DEBUG) bytes, Type 0 (a critical section, not a resource),
        // and its CriticalSection field pointing at a readable CRITICAL_SECTION whose DebugInfo
        // points straight back at the block. Those are excluded from the heap counters and
        // reported as `cs-debug`. A zeroed, free-listed one is indistinguishable from an ordinary
        // zeroed block and stays charged - it only arises when a section is deleted after its
        // first contention within a measured pass, at most once per section, which the retry
        // rule absorbs (the plugin's CRT locks are deleted at DLL unload only).
        constexpr WORD kCriticalSectionType = 0; // the Type ntdll writes for a critical section

        bool isLiveCriticalSectionDebug(const HeapBlock &block) noexcept
        {
            static_assert(offsetof(RTL_CRITICAL_SECTION_DEBUG, CriticalSection) + sizeof(void *) <=
                          std::tuple_size_v<decltype(block.head)>);
            static_assert(offsetof(RTL_CRITICAL_SECTION, DebugInfo) == 0);
            if (block.size != sizeof(RTL_CRITICAL_SECTION_DEBUG)) {
                return false;
            }
            WORD type = 0;
            std::memcpy(&type, block.head.data() + offsetof(RTL_CRITICAL_SECTION_DEBUG, Type), sizeof(type));
            if (type != kCriticalSectionType) {
                return false;
            }
            // The CriticalSection field, read as the pointer it is (from the block's raw bytes).
            const void *address = nullptr;
            std::memcpy(static_cast<void *>(&address),
                        block.head.data() + offsetof(RTL_CRITICAL_SECTION_DEBUG, CriticalSection), sizeof(address));
            if (address == nullptr) {
                return false;
            }
            const auto section = reinterpret_cast<std::uintptr_t>(address);
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQuery(address, &region, sizeof(region)) != sizeof(region) || region.State != MEM_COMMIT ||
                (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            if ((region.Protect & kReadable) == 0) {
                return false;
            }
            const auto regionEnd = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
            if (section + sizeof(CRITICAL_SECTION) > regionEnd) {
                return false;
            }
            std::uintptr_t debugInfo = 0;
            std::memcpy(&debugInfo, address, sizeof(debugInfo));
            return debugInfo == block.address;
        }

        // Calls `visit(block, liveCriticalSectionDebug)` for every busy block present in `after`
        // and not in `before` (by address and size). Both lists are sorted by address
        // (readResources), so this is one merge pass.
        template <class Visit>
        void forEachNewBlock(const ResourceUsage &after, const ResourceUsage &before, Visit &&visit)
        {
            auto old = before.blocks.begin();
            for (const auto &block : after.blocks) {
                while (old != before.blocks.end() && old->address < block.address) {
                    ++old;
                }
                if (old != before.blocks.end() && old->address == block.address && old->size == block.size) {
                    continue;
                }
                visit(block, isLiveCriticalSectionDebug(block));
            }
        }

        std::string describeDelta(const ResourceDelta &delta)
        {
            return "heap blocks " + withSign(delta.heapBlocks) + ", heap bytes " + withSign(delta.heapBytes) +
                   ", handles " + withSign(delta.handles) + ", views " + withSign(delta.mappedViews) + " (" +
                   kibibytes(delta.mappedBytes) + "), cs-debug " + withSign(delta.csDebugBlocks);
        }

    } // namespace

    ResourceUsage snapshotResources()
    {
        // Teardown is not instantaneous: the kernel releases a joined thread's stack and decommits
        // freed pages a little after the fact, so the commit charge keeps moving for a while, and
        // the exact counters are compared as well so that a reading taken in the middle of any
        // such release never becomes the snapshot. Wait until two readings 5 ms apart agree on
        // every number, bounded at one second. If the bound is hit the last reading is used and
        // the log says so. (The +1 block once blamed on this wait was ntdll's critical-section
        // debug block, now recognised - see isLiveCriticalSectionDebug.)
        // One record buffer per snapshot: every reading of this snapshot overwrites the same one.
        auto &records = recordBuffer();
        auto previous = readResources(records);
        for (int attempt = 0; attempt < 200; ++attempt) {
            Sleep(5);
            const auto current = readResources(records);
            if (sameCounters(current, previous)) {
                return current;
            }
            previous = current;
        }
        std::puts("[leak] snapshot did not settle: two readings 5 ms apart never agreed within 1 s; using the last");
        static_cast<void>(std::fflush(stdout));
        return previous;
    }

    ResourceDelta operator-(const ResourceUsage &after, const ResourceUsage &before) noexcept
    {
        const auto difference = [](const std::uint64_t a, const std::uint64_t b) {
            return static_cast<std::int64_t>(a) - static_cast<std::int64_t>(b);
        };
        ResourceDelta delta{difference(after.heapBlocks, before.heapBlocks),
                            difference(after.heapBytes, before.heapBytes),
                            difference(after.handles, before.handles),
                            difference(after.mappedViews, before.mappedViews),
                            difference(after.mappedBytes, before.mappedBytes),
                            difference(after.privateBytes, before.privateBytes),
                            difference(after.heapCommitted, before.heapCommitted)};
        // Live critical-section debug blocks that appeared are Windows', not the plugin's: taken
        // out of the exact counters and counted on their own.
        forEachNewBlock(after, before, [&](const HeapBlock &block, const bool criticalSectionDebug) {
            if (criticalSectionDebug) {
                --delta.heapBlocks;
                delta.heapBytes -= block.size;
                ++delta.csDebugBlocks;
            }
        });
        return delta;
    }

    std::string describeNewBlocks(const ResourceUsage &after, const ResourceUsage &before)
    {
        std::string text;
        std::size_t listed = 0;
        forEachNewBlock(after, before, [&](const HeapBlock &block, const bool criticalSectionDebug) {
            if (listed < 32) {
                text += "  " + hexAddress(block.address) + " " + std::to_string(block.size) + " bytes: " +
                        hex(std::span{block.head}.first(std::min<std::size_t>(block.size, block.head.size()))) +
                        (criticalSectionDebug ? " [RTL_CRITICAL_SECTION_DEBUG of a live critical section: Windows', "
                                                "not charged]"
                                              : "") +
                        "\n";
            }
            ++listed;
        });
        if (listed > 32) {
            text += "  ... " + std::to_string(listed - 32) + " more\n";
        }
        if (after.heapBlocks > after.blocks.size() || before.heapBlocks > before.blocks.size()) {
            text += "  (more busy blocks than the record buffers hold; the list above is partial)\n";
        }
        return text;
    }

    bool underAddressSanitizer() noexcept
    {
        return PVDKIT_TEST_UNDER_ASAN != 0;
    }

    std::string formatReport(const LeakReport &report)
    {
        std::string line =
            "[leak] " + report.scenario + ": " + describeDelta(report.delta) + ", private " +
            kibibytes(report.delta.privateBytes) + " (heap committed " + kibibytes(report.delta.heapCommitted) +
            ") (warm-up " + std::to_string(report.warmUpIterations) + " x " + std::to_string(report.warmUp.count()) +
            " ms, measured " + std::to_string(report.iterations) + " x " + std::to_string(report.measured.count()) +
            " ms)" + (underAddressSanitizer() ? " [asan]" : "");
        if (report.secondPassRan) {
            line += " [first pass: " + describeDelta(report.firstPass) +
                    "; second pass: " + describeDelta(report.secondPass) +
                    (report.firstPassWithinNoise ? "; the first pass grew within noise, the second decided]"
                                                 : "; the first pass grew beyond noise and decides]");
        }
        return line;
    }

    void printReport(const LeakReport &report)
    {
        // stdout rather than doctest's MESSAGE: one plain line per scenario that ctest keeps in its
        // log and a reader can grep, with none of doctest's file:line decoration. The blocks that
        // appeared are listed for every pass whose heap counters moved at all, so a net-negative
        // pass that still gained a block shows it.
        std::puts(formatReport(report).c_str());
        const auto list = [](const char *pass, const ResourceDelta &delta, const std::string &blocks) {
            if (!blocks.empty() && (delta.heapBlocks != 0 || delta.heapBytes != 0)) {
                std::printf("[leak]   blocks that appeared in the %s:\n%s", pass, blocks.c_str());
            }
        };
        if (report.secondPassRan) {
            list("first pass", report.firstPass, report.firstPassNewBlocks);
            list("second pass", report.secondPass, report.secondPassNewBlocks);
        } else {
            list("measured pass", report.delta, report.newBlocks);
        }
        static_cast<void>(std::fflush(stdout));
    }

    void requireNoLeak(const LeakReport &report, const std::int64_t privateBytesTolerance, const Allowance allowance)
    {
        printReport(report);
        const auto line = formatReport(report);
        CAPTURE(line);
        CHECK(report.delta.handles <= allowance.handles);
        CHECK(report.delta.mappedViews <= 0);
        CHECK(report.delta.mappedBytes <= 0);
        if (underAddressSanitizer()) {
            return;
        }
        CHECK(report.delta.heapBlocks <= allowance.heapBlocks);
        CHECK(report.delta.heapBytes <= allowance.heapBytes);
        CHECK(report.delta.privateBytes <= privateBytesTolerance);
    }

    std::size_t leakIterations(const std::size_t defaultIterations)
    {
        char buffer[32] = {};
        const DWORD length = GetEnvironmentVariableA("PVDKIT_LEAK_ITERATIONS", buffer, sizeof(buffer));
        if (length == 0 || length >= sizeof(buffer)) {
            return defaultIterations;
        }
        std::size_t value = 0;
        const auto [end, error] = std::from_chars(buffer, buffer + length, value);
        if (error != std::errc{} || end != buffer + length || value == 0) {
            return defaultIterations;
        }
        return value;
    }

} // namespace pvdkit::test
