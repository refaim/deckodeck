#pragma once

// A deterministic fuzz-lite corpus: mutated copies of real fixtures (random byte flips,
// truncations, corrupted box sizes, spliced garbage, trailing garbage, zeroed ranges), generated
// at test time from a fixed seed into %TEMP% and never committed. The leak scenarios open every
// file in both host modes; the plugin must answer FALSE or hand out a valid image, free
// everything, and - under the asan preset - trip no memory error.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pvdkit::test
{

    struct MutatedFile
    {
        std::string origin;   // the fixture it was derived from
        std::string mutation; // what was done to it, for the log
        std::string utf8Path; // where it was written (inside the corpus directory)
        std::vector<std::byte> bytes;
    };

    /// The mutation strategies, cycled so every corpus of seven or more files has each of them.
    enum class Mutation : std::uint8_t
    {
        FlipBytes, // XOR 1..8 random bytes with random non-zero values
        Truncate,  // keep a random prefix of 1..size-1 bytes
        FlipAndTruncate,
        BoxSize, // overwrite a random 4-aligned 32-bit word in the first 4 KiB with 0, 1, 8, 0xFFFFFFFF or random
        Splice,  // overwrite a random run of 16..64 bytes with random bytes
        TrailingGarbage, // append 1..4096 random bytes
        ZeroRange,       // zero a random run of 16..256 bytes
        Count
    };

    [[nodiscard]] std::string_view name(Mutation mutation) noexcept;

    /// Applies `mutation` to `source` with `rng`; the result always differs from `source`.
    [[nodiscard]] MutatedFile mutate(std::span<const std::byte> source, std::string_view originName, Mutation mutation,
                                     std::mt19937 &rng);

    /// A source the corpus can be built from: a name for the log and the bytes.
    struct CorpusSource
    {
        std::string name;
        std::vector<std::byte> bytes;
    };

    /// Generates `count` files, source i % sources.size() with mutation i % Mutation::Count, from
    /// `seed`, and writes them to `%TEMP%/pvdkit-hostile-<pid>-<seed>-<n>/`. The destructor removes the
    /// directory. Same inputs, same seed: byte-identical corpus on every run and both architectures
    /// (std::mt19937 is specified exactly; the distributions are the MSVC STL's, the one STL this
    /// kit builds with).
    class HostileCorpus
    {
      public:
        HostileCorpus(std::span<const CorpusSource> sources, std::size_t count, std::uint32_t seed);
        ~HostileCorpus();

        HostileCorpus(const HostileCorpus &) = delete;
        HostileCorpus &operator=(const HostileCorpus &) = delete;
        HostileCorpus(HostileCorpus &&) = delete;
        HostileCorpus &operator=(HostileCorpus &&) = delete;

        // Out of line on purpose: an inline accessor defined in this header is compiled into every
        // test executable that includes it, and llvm-cov then reports "functions have mismatched
        // data" when it merges their profiles.
        [[nodiscard]] const std::vector<MutatedFile> &files() const noexcept;
        [[nodiscard]] const std::filesystem::path &directory() const noexcept;

      private:
        std::filesystem::path directory_;
        std::vector<MutatedFile> files_;
    };

} // namespace pvdkit::test
