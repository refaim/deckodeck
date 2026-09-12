#include "HostileCorpus.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <system_error>
#include <utility>

#include <doctest/doctest.h>

namespace pvdkit::test
{
    namespace
    {

        constexpr std::size_t kMinimumSourceSize = 16;

        std::size_t below(std::mt19937 &rng, const std::size_t bound)
        {
            return std::uniform_int_distribution<std::size_t>{0, bound - 1}(rng);
        }

        std::size_t between(std::mt19937 &rng, const std::size_t low, const std::size_t high)
        {
            return std::uniform_int_distribution<std::size_t>{low, high}(rng);
        }

        std::byte randomByte(std::mt19937 &rng)
        {
            return static_cast<std::byte>(between(rng, 0, 255));
        }

        std::byte randomNonZeroByte(std::mt19937 &rng)
        {
            return static_cast<std::byte>(between(rng, 1, 255));
        }

        std::string flipBytes(std::vector<std::byte> &bytes, std::mt19937 &rng)
        {
            const auto count = between(rng, 1, 8);
            for (std::size_t flip = 0; flip < count; ++flip) {
                bytes[below(rng, bytes.size())] ^= randomNonZeroByte(rng);
            }
            return "flip " + std::to_string(count) + " bytes";
        }

        std::string truncate(std::vector<std::byte> &bytes, std::mt19937 &rng)
        {
            const auto length = between(rng, 1, bytes.size() - 1);
            bytes.resize(length);
            return "truncate to " + std::to_string(length) + " bytes";
        }

        std::string corruptBoxSize(std::vector<std::byte> &bytes, std::mt19937 &rng)
        {
            const auto window = std::min<std::size_t>(bytes.size(), 4096) - 4;
            const auto offset = below(rng, window / 4 + 1) * 4;
            constexpr std::uint32_t kInteresting[] = {0, 1, 8, 0xFFFFFFFFU};
            const auto choice = below(rng, 5);
            const std::uint32_t value =
                choice < 4 ? kInteresting[choice] : static_cast<std::uint32_t>(between(rng, 0, 0xFFFFFFFFU));
            const std::byte encoded[4] = {static_cast<std::byte>(value >> 24), static_cast<std::byte>(value >> 16),
                                          static_cast<std::byte>(value >> 8), static_cast<std::byte>(value)};
            if (std::equal(encoded, encoded + 4, bytes.begin() + static_cast<std::ptrdiff_t>(offset))) {
                bytes[offset + 3] ^= std::byte{0x01};
                return "box size @" + std::to_string(offset) + " already " + std::to_string(value) +
                       ", low bit flipped";
            }
            std::copy(encoded, encoded + 4, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
            return "box size @" + std::to_string(offset) + " := " + std::to_string(value);
        }

        std::string splice(std::vector<std::byte> &bytes, std::mt19937 &rng)
        {
            const auto length = std::min(between(rng, 16, 64), bytes.size());
            const auto offset = below(rng, bytes.size() - length + 1);
            bool changed = false;
            for (std::size_t index = 0; index < length; ++index) {
                const auto value = randomByte(rng);
                changed = changed || bytes[offset + index] != value;
                bytes[offset + index] = value;
            }
            if (!changed) {
                bytes[offset] ^= std::byte{0xFF};
            }
            return "splice " + std::to_string(length) + " random bytes @" + std::to_string(offset);
        }

        std::string appendGarbage(std::vector<std::byte> &bytes, std::mt19937 &rng)
        {
            const auto length = between(rng, 1, 4096);
            for (std::size_t index = 0; index < length; ++index) {
                bytes.push_back(randomByte(rng));
            }
            return "append " + std::to_string(length) + " random bytes";
        }

        std::string zeroRange(std::vector<std::byte> &bytes, std::mt19937 &rng)
        {
            const auto length = std::min(between(rng, 16, 256), bytes.size());
            const auto offset = below(rng, bytes.size() - length + 1);
            const auto first = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
            const auto last = first + static_cast<std::ptrdiff_t>(length);
            const bool alreadyZero = std::all_of(first, last, [](const std::byte b) { return b == std::byte{0}; });
            std::fill(first, last, std::byte{0});
            if (alreadyZero) {
                bytes[offset] = std::byte{0xFF};
                return "zero " + std::to_string(length) + " bytes @" + std::to_string(offset) +
                       " (already zero: first byte set to 0xFF)";
            }
            return "zero " + std::to_string(length) + " bytes @" + std::to_string(offset);
        }

        std::string fileName(const std::size_t index, const std::string_view origin, const Mutation mutation)
        {
            const std::filesystem::path originPath{origin};
            std::string stem = originPath.stem().string();
            std::string extension = originPath.extension().string();
            std::string number = std::to_string(index);
            while (number.size() < 3) {
                number.insert(number.begin(), '0');
            }
            return number + "-" + stem + "-" + std::string{name(mutation)} + extension;
        }

        void writeFile(const std::filesystem::path &path, const std::span<const std::byte> bytes)
        {
            std::ofstream stream{path, std::ios::binary};
            REQUIRE(stream.good());
            stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            REQUIRE(stream.good());
        }

    } // namespace

    std::string_view name(const Mutation mutation) noexcept
    {
        switch (mutation) {
        case Mutation::FlipBytes:
            return "flip";
        case Mutation::Truncate:
            return "truncate";
        case Mutation::FlipAndTruncate:
            return "flip+truncate";
        case Mutation::BoxSize:
            return "boxsize";
        case Mutation::Splice:
            return "splice";
        case Mutation::TrailingGarbage:
            return "append";
        case Mutation::ZeroRange:
            return "zero";
        case Mutation::Count:
            break;
        }
        return "?";
    }

    MutatedFile mutate(const std::span<const std::byte> source, const std::string_view originName,
                       const Mutation mutation, std::mt19937 &rng)
    {
        REQUIRE(source.size() >= kMinimumSourceSize);
        MutatedFile file;
        file.origin = std::string{originName};
        file.bytes.assign(source.begin(), source.end());
        switch (mutation) {
        case Mutation::FlipBytes:
            file.mutation = flipBytes(file.bytes, rng);
            break;
        case Mutation::Truncate:
            file.mutation = truncate(file.bytes, rng);
            break;
        case Mutation::FlipAndTruncate:
            // Two statements: the flips come first, the cut second (operands of + are unsequenced).
            file.mutation = flipBytes(file.bytes, rng);
            file.mutation += " + " + truncate(file.bytes, rng);
            break;
        case Mutation::BoxSize:
            file.mutation = corruptBoxSize(file.bytes, rng);
            break;
        case Mutation::Splice:
            file.mutation = splice(file.bytes, rng);
            break;
        case Mutation::TrailingGarbage:
            file.mutation = appendGarbage(file.bytes, rng);
            break;
        case Mutation::ZeroRange:
            file.mutation = zeroRange(file.bytes, rng);
            break;
        case Mutation::Count:
            REQUIRE(mutation != Mutation::Count);
            break;
        }
        return file;
    }

    HostileCorpus::HostileCorpus(const std::span<const CorpusSource> sources, const std::size_t count,
                                 const std::uint32_t seed)
    {
        std::vector<const CorpusSource *> usable;
        for (const auto &source : sources) {
            if (source.bytes.size() >= kMinimumSourceSize) {
                usable.push_back(&source);
            }
        }
        REQUIRE_FALSE(usable.empty());

        // Process id plus a per-process sequence number: two corpora in one process (or two test
        // processes side by side) never share, and therefore never delete, a directory.
        static std::atomic<unsigned> sequence{0};
        directory_ = std::filesystem::temp_directory_path() /
                     ("pvdkit-hostile-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(seed) + "-" +
                      std::to_string(sequence.fetch_add(1)));
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
        std::filesystem::create_directories(directory_);

        std::mt19937 rng{seed};
        constexpr auto kMutations = static_cast<std::size_t>(Mutation::Count);
        files_.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto &source = *usable[index % usable.size()];
            const auto mutation = static_cast<Mutation>(index % kMutations);
            auto file = mutate(source.bytes, source.name, mutation, rng);
            const auto path = directory_ / fileName(index, source.name, mutation);
            writeFile(path, file.bytes);
            const auto utf8 = path.u8string();
            file.utf8Path.assign(utf8.begin(), utf8.end());
            files_.push_back(std::move(file));
        }
    }

    HostileCorpus::~HostileCorpus()
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    const std::vector<MutatedFile> &HostileCorpus::files() const noexcept
    {
        return files_;
    }

    const std::filesystem::path &HostileCorpus::directory() const noexcept
    {
        return directory_;
    }

} // namespace pvdkit::test
