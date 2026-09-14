#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

namespace
{

    enum class ScanState : std::uint8_t
    {
        Code,
        LineComment,
        BlockComment,
        StringLiteral,
        CharacterLiteral
    };

    [[nodiscard]] bool isDigitSeparator(const std::string_view source, const std::size_t index)
    {
        if (index == 0 || index + 1 >= source.size()) {
            return false;
        }

        const auto isIdentifierCharacter = [](const unsigned char character) {
            return std::isalnum(character) != 0 || character == '_';
        };
        if (!isIdentifierCharacter(static_cast<unsigned char>(source[index - 1])) ||
            !isIdentifierCharacter(static_cast<unsigned char>(source[index + 1]))) {
            return false;
        }

        auto tokenStart = index;
        while (tokenStart > 0) {
            const auto character = static_cast<unsigned char>(source[tokenStart - 1]);
            if (!isIdentifierCharacter(character) && character != '.') {
                break;
            }
            --tokenStart;
        }
        return std::ranges::any_of(source.substr(tokenStart, index - tokenStart),
                                   [](const unsigned char character) { return std::isdigit(character) != 0; });
    }

    [[nodiscard]] bool hasCharacterLiteralTerminator(const std::string_view source, const std::size_t opening)
    {
        for (auto index = opening + 1; index < source.size(); ++index) {
            if (source[index] == '\n' || source[index] == '\r') {
                return false;
            }
            if (source[index] == '\\' && index + 1 < source.size()) {
                ++index;
            } else if (source[index] == '\'') {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::string stripIgnored(const std::string_view source, const bool stripLiterals)
    {
        std::string stripped;
        stripped.reserve(source.size());
        auto state = ScanState::Code;

        for (std::size_t index = 0; index < source.size(); ++index) {
            const char current = source[index];
            const char next = index + 1 < source.size() ? source[index + 1] : '\0';

            if (state == ScanState::Code) {
                if (current == '/' && next == '/') {
                    stripped.append(2, ' ');
                    ++index;
                    state = ScanState::LineComment;
                } else if (current == '/' && next == '*') {
                    stripped.append(2, ' ');
                    ++index;
                    state = ScanState::BlockComment;
                } else if (current == '"') {
                    stripped.push_back(stripLiterals ? ' ' : current);
                    state = ScanState::StringLiteral;
                } else if (current == '\'' && !isDigitSeparator(source, index) &&
                           hasCharacterLiteralTerminator(source, index)) {
                    stripped.push_back(stripLiterals ? ' ' : current);
                    state = ScanState::CharacterLiteral;
                } else {
                    stripped.push_back(current);
                }
                continue;
            }

            if (state == ScanState::LineComment) {
                stripped.push_back(current == '\n' ? '\n' : ' ');
                if (current == '\n') {
                    state = ScanState::Code;
                }
                continue;
            }

            if (state == ScanState::BlockComment) {
                if (current == '*' && next == '/') {
                    stripped.append(2, ' ');
                    ++index;
                    state = ScanState::Code;
                } else {
                    stripped.push_back(current == '\n' ? '\n' : ' ');
                }
                continue;
            }

            const auto terminator = state == ScanState::StringLiteral ? '"' : '\'';
            if (current == '\\' && next != '\0') {
                if (stripLiterals) {
                    stripped.append(2, ' ');
                } else {
                    stripped.push_back(current);
                    stripped.push_back(next);
                }
                ++index;
            } else {
                stripped.push_back(stripLiterals && current != '\n' ? ' ' : current);
                if (current == terminator) {
                    state = ScanState::Code;
                }
            }
        }

        return stripped;
    }

    [[nodiscard]] std::string stripCommentsAndLiterals(const std::string_view source)
    {
        return stripIgnored(source, true);
    }

    [[nodiscard]] std::string stripComments(const std::string_view source)
    {
        return stripIgnored(source, false);
    }

    [[nodiscard]] std::string lowercase(std::string text)
    {
        std::ranges::transform(text, text.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return text;
    }

    [[nodiscard]] std::string normalizedLowerPath(const std::filesystem::path &path)
    {
        auto normalized = path.generic_string();
        std::ranges::transform(normalized, normalized.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return normalized;
    }

    // The tree has one shared root (`src/`) and one root per plugin (`plugins/<name>/src/`). The
    // layer directories (`core/`, `pvd/`, `adapters/`) are recognised by name under either root, so
    // the same rules apply to shared and plugin code; only include ownership (below) tells the roots
    // apart.
    [[nodiscard]] bool isCore(const std::string_view path)
    {
        return path.find("/src/core/") != std::string_view::npos;
    }

    [[nodiscard]] bool isPvd(const std::string_view path)
    {
        return path.find("/src/pvd/") != std::string_view::npos;
    }

    [[nodiscard]] bool isAdapters(const std::string_view path)
    {
        return path.find("/src/adapters/") != std::string_view::npos;
    }

    [[nodiscard]] bool isAdapterOrPvd(const std::string_view path)
    {
        return isAdapters(path) || isPvd(path);
    }

    // The two composition roots: the shared Exports.cpp (compiled once per plugin, so it may include
    // the plugin's generated pvd/PluginConstants.hpp) and a plugin's DefaultPlugin.cpp (directly
    // under `plugins/<name>/src/`).
    [[nodiscard]] bool isExports(const std::string_view path)
    {
        return path.ends_with("/src/pvd/exports.cpp");
    }

    [[nodiscard]] bool isPluginRoot(const std::string_view path)
    {
        return path.ends_with("/src/defaultplugin.cpp") && path.find("/plugins/") != std::string_view::npos;
    }

    [[nodiscard]] bool mayIncludeForeignHeaders(const std::string_view path)
    {
        return isAdapters(path) || isExports(path);
    }

    // pvd/PvdApi.hpp exists to include <Windows.h> (for the SDK's fixed-width typedefs) exactly once
    // on behalf of the marshalling layer; it may not reach for a codec library.
    [[nodiscard]] bool mayIncludeWindowsHeader(const std::string_view path)
    {
        return mayIncludeForeignHeaders(path) || path.ends_with("/src/pvd/pvdapi.hpp");
    }

    struct Violation
    {
        std::string token;
    };

    struct CompiledRule
    {
        std::regex pattern;
        std::string_view label;
    };

    // Every rule below is matched with std::regex against every scanned file, and "source tree obeys
    // ownership and layering rules" scans every source root. Compiling a std::regex is comparatively
    // expensive, so each pattern is compiled exactly once into this table (a function-local static is
    // initialized on first use only) instead of once per rule per file.
    struct GuardRegexTable
    {
        std::vector<CompiledRule> forbiddenEverywhere;
        std::regex reinterpretCast;
        std::regex windowsHeader;
        std::regex codecHeader;
        std::regex pvdInclude;
        std::regex adaptersInclude;
        std::regex anyInclude;
        std::regex catchOpen;
        // Scope headers (the function-local static rule): what to blank out before classifying a
        // header, the keywords that open a declarative scope, and an `extern "C"` block after
        // literal stripping.
        std::regex scopeNoise;
        std::regex declarativeKeyword;
        std::regex externBlock;
    };

    [[nodiscard]] const GuardRegexTable &guardRegexes()
    {
        static const GuardRegexTable table{
            .forbiddenEverywhere =
                {
                    CompiledRule{std::regex{R"(\bnew\b)"}, "new"},
                    // `= delete;` on a special member (required by the architecture contract for
                    // classes holding references) must not be flagged; only `delete` used as the
                    // deallocation operator (optionally `delete[]`), i.e. followed by an operand, is
                    // forbidden.
                    CompiledRule{std::regex{R"(\bdelete\b\s*(\[\s*\])?\s*[^\s;])"}, "delete"},
                    CompiledRule{std::regex{R"(\bmalloc\b)"}, "malloc"},
                    CompiledRule{std::regex{R"(\bcalloc\b)"}, "calloc"},
                    CompiledRule{std::regex{R"(\brealloc\b)"}, "realloc"},
                    CompiledRule{std::regex{R"(\bfree\s*\()"}, "free("},
                    CompiledRule{std::regex{R"(\bshared_ptr\b)"}, "shared_ptr"},
                    CompiledRule{std::regex{R"(\bweak_ptr\b)"}, "weak_ptr"},
                    CompiledRule{std::regex{R"(\blcov_excl)"}, "LCOV_EXCL"},
                    CompiledRule{std::regex{R"(\b__builtin_unreachable\b)"}, "__builtin_unreachable"},
                    CompiledRule{std::regex{R"(\[\[\s*assume\b)"}, "[[assume"},
                    // The synchronisation family below is forbidden because MSVC >= 14.50 (VS 2026)
                    // implements it over WaitOnAddress / WakeByAddressAll imported directly from
                    // api-ms-win-core-synch-l1-2-0.dll, and a plugin imports KERNEL32.dll only
                    // (AGENTS.md rule 13). std::thread + join, std::mutex (SRWLOCK) and plain
                    // atomics stay allowed.
                    // jthread: its stop_token state waits and notifies on atomics.
                    CompiledRule{std::regex{R"(\bjthread\b)"}, "jthread"},
                    // stop_token / stop_source / stop_callback: the same atomic wait/notify state.
                    CompiledRule{std::regex{R"(\bstop_(?:token|source|callback)\b)"},
                                 "stop_token/stop_source/stop_callback"},
                    // call_once / once_flag: one-time initialisation, the same family as guarded statics.
                    CompiledRule{std::regex{R"(\b(?:call_once|once_flag)\b)"}, "call_once/once_flag"},
                    // <latch>: counts down on an atomic and waits on it.
                    CompiledRule{std::regex{R"((?:#[ \t]*include[ \t]*<[ \t]*latch[ \t]*>)|(?:::[ \t]*latch\b))"},
                                 "latch"},
                    // <barrier>: phases complete through atomic wait/notify.
                    CompiledRule{std::regex{R"((?:#[ \t]*include[ \t]*<[ \t]*barrier[ \t]*>)|(?:::[ \t]*barrier\b))"},
                                 "barrier"},
                    // <semaphore>: acquire blocks in an atomic wait.
                    CompiledRule{
                        std::regex{
                            R"((?:#[ \t]*include[ \t]*<[ \t]*semaphore[ \t]*>)|(?:::[ \t]*(?:counting_|binary_)?semaphore\b))"},
                        "semaphore"},
                    // condition_variable(_any): the 14.5x STL builds it over the same primitives.
                    CompiledRule{std::regex{R"(\bcondition_variable(?:_any)?\b)"}, "condition_variable"},
                    // .wait( / .wait_for( / .wait_until( / .notify_one( / .notify_all( on an atomic
                    // (or anything else): __std_atomic_wait_direct and its notify twins.
                    CompiledRule{std::regex{R"((?:\.|->)\s*(?:wait(?:_for|_until)?|notify_one|notify_all)\s*\()"},
                                 ".wait(/.notify_one(/.notify_all("},
                    // The free-function spellings of the same operations, for std::atomic and for
                    // std::atomic_flag (atomic_flag_wait forwards to atomic_flag::wait).
                    CompiledRule{std::regex{R"(\batomic_(?:flag_)?(?:wait|wait_explicit|notify_one|notify_all)\b)"},
                                 "atomic_wait/atomic_flag_wait/atomic_notify_*"},
                    // Header-level users of the same primitives in the MSVC STL, forbidden with them
                    // (review-task24 nit 2): the timed mutexes wait on a condition_variable ...
                    CompiledRule{std::regex{R"(\b(?:recursive_|shared_)?timed_mutex\b)"}, "timed_mutex"},
                    // ... <future>'s shared state waits on _Cnd_t ...
                    CompiledRule{
                        std::regex{
                            R"((?:#[ \t]*include[ \t]*<[ \t]*future[ \t]*>)|(?:::[ \t]*(?:future|shared_future|promise|async|packaged_task)\b))"},
                        "future/promise/async/packaged_task"},
                    // ... and <syncstream>'s locked pointer is an atomic wait.
                    CompiledRule{
                        std::regex{
                            R"((?:#[ \t]*include[ \t]*<[ \t]*syncstream[ \t]*>)|(?:::[ \t]*(?:basic_)?(?:osyncstream|syncbuf)\b))"},
                        "osyncstream/syncbuf"},
                },
            .reinterpretCast = std::regex{R"(\breinterpret_cast\b)"},
            .windowsHeader = std::regex{R"((^|\n)[ \t]*#[ \t]*include[ \t]*[<"][ \t]*windows[.]h[ \t]*[>"])"},
            // The codec libraries the plugins wrap: foreign headers that only
            // an adapter may include. Extend the alternation when a plugin brings a new library.
            .codecHeader =
                std::regex{R"((^|\n)[ \t]*#[ \t]*include[ \t]*[<"][ \t]*(avif/avif|dav1d/dav1d|spng)[.]h[ \t]*[>"])"},
            // Captures the whole header path after `pvd/` (nested directories included, `.h` or `.hpp`)
            // so callers can check it against an allowlist, rather than hard-coding the forbidden names
            // in the pattern itself.
            .pvdInclude =
                std::regex{R"((?:^|\n)[ \t]*#[ \t]*include[ \t]*[<"][ \t]*pvd/([a-z0-9_/]+)[.]h(?:pp)?[ \t]*[>"])"},
            .adaptersInclude =
                std::regex{R"((?:^|\n)[ \t]*#[ \t]*include[ \t]*[<"][ \t]*adapters/[a-z0-9_/]+[.]h(?:pp)?[ \t]*[>"])"},
            // Every include directive, whatever it names; the include-ownership rule resolves the path
            // against the source roots and ignores what resolves nowhere (std, SDK, vcpkg headers).
            .anyInclude = std::regex{R"((?:^|\n)[ \t]*#[ \t]*include[ \t]*[<"][ \t]*([^>"]+?)[ \t]*[>"])"},
            .catchOpen = std::regex{R"(\bcatch\s*\()"},
            .scopeNoise = std::regex{R"(\b(?:public|private|protected)\s*:|\[\[[^\]]*\]\]|\balignas\s*\([^)]*\))"},
            .declarativeKeyword =
                std::regex{R"(^\s*(?:(?:inline|export|typedef|friend)\s+)*(?:namespace|class|struct|union|enum)\b)"},
            .externBlock = std::regex{R"(^\s*extern\s*$)"},
        };
        return table;
    }

    [[nodiscard]] bool isIdentifierCharacter(const unsigned char character)
    {
        return std::isalnum(character) != 0 || character == '_';
    }

    [[nodiscard]] std::string_view trimLeft(std::string_view text)
    {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
            text.remove_prefix(1);
        }
        return text;
    }

    // Removes every leading `template <...>` (angle brackets balanced, so `Pair<T, T>` and `>>` are
    // fine) from a scope header, so that `template <class T> struct Foo` is seen as `struct Foo` and
    // `template <class F> auto guarded(...)` as the function it is.
    [[nodiscard]] std::string_view withoutTemplateHeads(std::string_view header)
    {
        constexpr std::string_view keyword{"template"};
        for (header = trimLeft(header); header.starts_with(keyword) &&
                                        (header.size() == keyword.size() ||
                                         !isIdentifierCharacter(static_cast<unsigned char>(header[keyword.size()])));
             header = trimLeft(header)) {
            const auto open = header.find('<');
            if (open == std::string_view::npos) {
                break;
            }
            int depth = 0;
            std::size_t index = open;
            for (; index < header.size(); ++index) {
                if (header[index] == '<') {
                    ++depth;
                } else if (header[index] == '>' && --depth == 0) {
                    break;
                }
            }
            if (index >= header.size()) {
                break;
            }
            header.remove_prefix(index + 1);
        }
        return header;
    }

    // A brace opens a declarative scope (namespace, class, struct, union, enum, or an `extern "C"`
    // block, whose literal has been stripped) when its header - the code since the previous `;`,
    // `{` or `}`, minus access specifiers, attributes, alignas and template heads - starts with one
    // of those keywords and holds no parenthesis (so `struct tm *now()` is the function it is).
    // Every other brace - a function body, a control statement, a lambda, a brace initialiser -
    // opens an executable scope.
    [[nodiscard]] bool opensDeclarativeScope(const std::string_view rawHeader, const GuardRegexTable &regexes)
    {
        const auto header = std::regex_replace(std::string{rawHeader}, regexes.scopeNoise, " ");
        const auto body = withoutTemplateHeads(header);
        if (std::regex_search(body.begin(), body.end(), regexes.externBlock)) {
            return true;
        }
        return std::regex_search(body.begin(), body.end(), regexes.declarativeKeyword) &&
               body.find('(') == std::string_view::npos;
    }

    [[nodiscard]] bool isWordAt(const std::string_view code, const std::size_t index, const std::string_view word)
    {
        if (code.compare(index, word.size(), word) != 0) {
            return false;
        }
        const bool boundaryBefore = index == 0 || !isIdentifierCharacter(static_cast<unsigned char>(code[index - 1]));
        const bool boundaryAfter = index + word.size() >= code.size() ||
                                   !isIdentifierCharacter(static_cast<unsigned char>(code[index + word.size()]));
        return boundaryBefore && boundaryAfter;
    }

    // Function-local statics. MSVC >= 14.50 implements their guarded initialisation
    // (_Init_thread_wait / _Init_thread_notify) over WaitOnAddress / WakeByAddressAll from
    // api-ms-win-core-synch-l1-2-0.dll, which a plugin (KERNEL32.dll only) must never import; a
    // plugin owns such state through its composition root instead (ARCHITECTURE §7). A `static` in an
    // executable scope (see opensDeclarativeScope) that is not `static constexpr` is one; `static_assert`
    // and `static_cast` are different tokens (no word boundary after `static`). Namespace-scope and
    // class-scope statics are not this rule's business.
    void appendFunctionLocalStaticViolations(std::vector<Violation> &violations, const std::string_view code,
                                             const GuardRegexTable &regexes)
    {
        constexpr std::string_view keyword{"static"};
        constexpr std::string_view allowedSpecifier{"constexpr"};
        std::vector<bool> executableScopes;
        std::size_t headerStart = 0;
        for (std::size_t index = 0; index < code.size(); ++index) {
            const char current = code[index];
            if (current == '{') {
                executableScopes.push_back(
                    !opensDeclarativeScope(code.substr(headerStart, index - headerStart), regexes));
                headerStart = index + 1;
            } else if (current == '}' || current == ';') {
                if (current == '}' && !executableScopes.empty()) {
                    executableScopes.pop_back();
                }
                headerStart = index + 1;
            } else if (current == 's' && !executableScopes.empty() && executableScopes.back() &&
                       isWordAt(code, index, keyword)) {
                const auto rest = trimLeft(code.substr(index + keyword.size()));
                if (!(rest.starts_with(allowedSpecifier) &&
                      (rest.size() == allowedSpecifier.size() ||
                       !isIdentifierCharacter(static_cast<unsigned char>(rest[allowedSpecifier.size()]))))) {
                    violations.push_back({"function-local static (only static constexpr is allowed)"});
                    return;
                }
            }
        }
    }

    // Capture group 1 of every match of `pattern` in `includeCode`, in order.
    [[nodiscard]] std::vector<std::string> capturedIncludes(const std::string_view includeCode,
                                                            const std::regex &pattern)
    {
        std::vector<std::string> headers;
        const auto end = std::cregex_iterator{};
        for (auto it = std::cregex_iterator{includeCode.data(), includeCode.data() + includeCode.size(), pattern};
             it != end; ++it) {
            headers.push_back((*it)[1].str());
        }
        return headers;
    }

    void appendIfMatches(std::vector<Violation> &violations, const std::string_view code, const std::regex &pattern,
                         const std::string_view label)
    {
        if (std::regex_search(code.begin(), code.end(), pattern)) {
            violations.push_back({std::string{label}});
        }
    }

    [[nodiscard]] bool isAllowedCoreHeader(const std::string_view headerName)
    {
        // core (shared or a plugin's) may reach into the shared PVD boundary value/plugin contracts
        // only; every other pvd/... header (Shim, Firewall, ContextHandle, PluginFactory, the generated
        // PluginConstants, ...) belongs to the marshalling layer or a composition root and is forbidden
        // here. This is an allowlist, not a blocklist of currently-known headers, so a new pvd header
        // is forbidden in core by default. The whole captured path is compared, so `pvd/detail/types`
        // or `pvd/types/extra` do not qualify either.
        return headerName == "types" || headerName == "plugin";
    }

    // ARCHITECTURE §2: `pvd` (shim side) and `adapters` never include each other. They meet only in
    // the two composition roots:
    //   - src/pvd/Exports.cpp is the only pvd file that may include the seam pvd/PluginFactory.hpp
    //     and the per-plugin generated pvd/PluginConstants.hpp (it is compiled once per plugin);
    //     no pvd file may include an adapters header;
    //   - plugins/<name>/src/DefaultPlugin.cpp is the only file outside `pvd/` that may include
    //     those two, plus the boundary contracts; adapters include no pvd header at all.
    [[nodiscard]] bool isCompositionOnlyHeader(const std::string_view headerName)
    {
        return headerName == "pluginfactory" || headerName == "pluginconstants";
    }

    [[nodiscard]] bool isAllowedPluginRootHeader(const std::string_view headerName)
    {
        return isCompositionOnlyHeader(headerName) || isAllowedCoreHeader(headerName);
    }

    void appendLayeringViolations(std::vector<Violation> &violations, const std::string_view normalizedPath,
                                  const std::string_view includeCode, const GuardRegexTable &regexes)
    {
        const auto pvdHeaders = capturedIncludes(includeCode, regexes.pvdInclude);

        if (isCore(normalizedPath) && !std::ranges::all_of(pvdHeaders, isAllowedCoreHeader)) {
            violations.push_back({"forbidden pvd header"});
        }

        if (isPvd(normalizedPath)) {
            appendIfMatches(violations, includeCode, regexes.adaptersInclude, "#include adapters/ in pvd");
            if (!isExports(normalizedPath) && std::ranges::any_of(pvdHeaders, isCompositionOnlyHeader)) {
                violations.push_back({"pvd/PluginFactory.hpp or pvd/PluginConstants.hpp outside Exports.cpp"});
            }
        }

        if (isAdapters(normalizedPath) && !pvdHeaders.empty()) {
            violations.push_back({"#include pvd/ in adapters"});
        }

        if (isPluginRoot(normalizedPath) && !std::ranges::all_of(pvdHeaders, isAllowedPluginRootHeader)) {
            violations.push_back({"forbidden pvd header in DefaultPlugin.cpp"});
        }

        // Everything under a source root belongs to a layer directory, except the plugin's composition
        // root. A stray file directly under `src/` (or in an unknown directory) would have no rules and
        // silently escape the guard, so it is a violation in itself.
        if (!isCore(normalizedPath) && !isPvd(normalizedPath) && !isAdapters(normalizedPath) &&
            !isPluginRoot(normalizedPath)) {
            violations.push_back({"file outside the layer directories"});
        }
    }

    // Include ownership: which source root each header lives under. Keys are normalized lowercase
    // root paths (`.../src` or `.../plugins/<name>/src`); values are the generic, lowercase,
    // root-relative paths of the headers under that root, i.e. exactly the strings an `#include` names.
    using IncludeIndex = std::map<std::string, std::set<std::string>>;

    [[nodiscard]] bool isPluginRootPath(const std::string_view root)
    {
        return root.find("/plugins/") != std::string_view::npos;
    }

    // Shared code never includes a plugin header, and a plugin includes only shared headers and its
    // own. Every include is resolved against the roots the file may use (its own root and the shared
    // root) first: what resolves there is the includer's own header, even when another plugin has a
    // header of the same name (every plugin keeps its describer in `core/Describe.hpp`). Only an
    // include that resolves in no allowed root but does resolve under a foreign plugin's root is a
    // violation; includes that resolve in no root at all (std, the SDK, vcpkg) are not this rule's
    // business.
    void appendOwnershipViolations(std::vector<Violation> &violations, const std::string_view normalizedPath,
                                   const std::string_view includeCode, const IncludeIndex &index,
                                   const GuardRegexTable &regexes)
    {
        std::string_view ownRoot;
        for (const auto &[root, headers] : index) {
            if (normalizedPath.starts_with(root + "/") && root.size() > ownRoot.size()) {
                ownRoot = root;
            }
        }
        if (ownRoot.empty()) {
            return;
        }

        const std::string_view rootRelative = normalizedPath.substr(ownRoot.size() + 1);
        const auto ownEntry = index.find(std::string{ownRoot});
        const bool isOwnHeader = ownEntry->second.contains(std::string{rootRelative});
        const bool shadowsShared = std::ranges::any_of(index, [&](const auto &entry) {
            return !isPluginRootPath(entry.first) && entry.second.contains(std::string{rootRelative});
        });
        if (isPluginRootPath(ownRoot) && isOwnHeader && shadowsShared) {
            violations.push_back({"plugin header shadows a shared header"});
        }

        const auto isAllowedRoot = [ownRoot](const std::string_view root) {
            return root == ownRoot || !isPluginRootPath(root);
        };
        for (const auto &included : capturedIncludes(includeCode, regexes.anyInclude)) {
            const auto resolvesIn = [&](const bool allowedRoots) {
                return std::ranges::any_of(index, [&](const auto &entry) {
                    return isAllowedRoot(entry.first) == allowedRoots && entry.second.contains(included);
                });
            };
            if (!resolvesIn(true) && resolvesIn(false)) {
                violations.push_back({isPluginRootPath(ownRoot) ? "#include of another plugin's header"
                                                                : "#include of a plugin header from shared code"});
            }
        }
    }

    [[nodiscard]] std::vector<Violation> scan(const std::filesystem::path &path, const std::string_view source,
                                              const IncludeIndex &index)
    {
        const auto &regexes = guardRegexes();
        const auto code = lowercase(stripCommentsAndLiterals(source));
        // Include-directive rules intentionally run on comment-stripped but literal-preserving text
        // (stripComments, not stripCommentsAndLiterals): stripping literals would blank out the quoted
        // form `#include "windows.h"` and the guard would stop detecting it.
        const auto includeCode = lowercase(stripComments(source));
        const auto normalizedPath = normalizedLowerPath(std::filesystem::absolute(path));
        std::vector<Violation> violations;

        for (const auto &rule : regexes.forbiddenEverywhere) {
            appendIfMatches(violations, code, rule.pattern, rule.label);
        }
        appendFunctionLocalStaticViolations(violations, code, regexes);

        if (!isAdapterOrPvd(normalizedPath)) {
            appendIfMatches(violations, code, regexes.reinterpretCast, "reinterpret_cast");
        }

        if (!mayIncludeWindowsHeader(normalizedPath)) {
            appendIfMatches(violations, includeCode, regexes.windowsHeader, "#include windows.h");
        }
        if (!mayIncludeForeignHeaders(normalizedPath)) {
            appendIfMatches(violations, includeCode, regexes.codecHeader, "#include of a codec library header");
        }

        appendLayeringViolations(violations, normalizedPath, includeCode, regexes);
        appendOwnershipViolations(violations, normalizedPath, includeCode, index, regexes);

        if (!normalizedPath.ends_with("/src/pvd/firewall.hpp")) {
            appendIfMatches(violations, code, regexes.catchOpen, "catch(");
        }

        return violations;
    }

    [[nodiscard]] std::vector<Violation> scan(const std::filesystem::path &path, const std::string_view source)
    {
        return scan(path, source, IncludeIndex{});
    }

    [[nodiscard]] std::string readFile(const std::filesystem::path &path)
    {
        std::ifstream input{path, std::ios::binary};
        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    [[nodiscard]] bool isSourceFile(const std::filesystem::path &path)
    {
        const auto extension = path.extension().string();
        return extension == ".cpp" || extension == ".hpp" || extension == ".h";
    }

    [[nodiscard]] bool isHeaderFile(const std::filesystem::path &path)
    {
        const auto extension = path.extension().string();
        return extension == ".hpp" || extension == ".h";
    }

    // The source roots of the tree: `src/` and every `plugins/<name>/src/` that exists.
    [[nodiscard]] std::vector<std::filesystem::path> sourceRoots(const std::filesystem::path &repository)
    {
        std::vector<std::filesystem::path> roots{repository / "src"};
        for (const auto &entry : std::filesystem::directory_iterator{repository / "plugins"}) {
            if (entry.is_directory() && std::filesystem::is_directory(entry.path() / "src")) {
                roots.push_back(entry.path() / "src");
            }
        }
        return roots;
    }

    [[nodiscard]] IncludeIndex indexHeaders(const std::vector<std::filesystem::path> &roots)
    {
        IncludeIndex index;
        for (const auto &root : roots) {
            auto &headers = index[normalizedLowerPath(std::filesystem::absolute(root))];
            for (const auto &entry : std::filesystem::recursive_directory_iterator{root}) {
                if (entry.is_regular_file() && isHeaderFile(entry.path())) {
                    headers.insert(normalizedLowerPath(std::filesystem::relative(entry.path(), root)));
                }
            }
        }
        return index;
    }

    // A fake two-plugin tree for the ownership self-tests, keyed the way indexHeaders keys a real one.
    // Both plugins carry a `core/describe.hpp`, as every real plugin does (ARCHITECTURE §6 puts the
    // describer in `plugins/<id>/src/core/`): the ownership rule must let each plugin include its own
    // copy and still refuse the name from shared code.
    [[nodiscard]] IncludeIndex fakeIndex()
    {
        const auto root = [](const std::string_view relative) {
            return normalizedLowerPath(std::filesystem::absolute(std::filesystem::path{relative}));
        };
        IncludeIndex index;
        index[root("project/src")] = {"core/error.hpp", "pvd/types.hpp", "adapters/win/utf8.hpp"};
        index[root("project/plugins/avif/src")] = {"adapters/avif/decoder.hpp", "core/describe.hpp", "core/error.hpp"};
        index[root("project/plugins/rpgmvp/src")] = {"adapters/spng/decoder.hpp", "core/describe.hpp"};
        return index;
    }

    TEST_CASE("scanner strips comments and literals")
    {
        const auto stripped = stripCommentsAndLiterals(
            "keep; // new Thing\n/* delete value */ keep2; \"malloc\"; 'x'; \"escaped \\\" free(\";");
        CHECK(stripped.find("keep;") != std::string::npos);
        CHECK(stripped.find("keep2;") != std::string::npos);
        CHECK(stripped.find("new Thing") == std::string::npos);
        CHECK(stripped.find("delete value") == std::string::npos);
        CHECK(stripped.find("malloc") == std::string::npos);
        CHECK(stripped.find("free(") == std::string::npos);
    }

    TEST_CASE("every unconditional rule has positive and negative scanner samples")
    {
        constexpr std::array samples{
            std::string_view{"new Thing;"},
            std::string_view{"new(Thing);"},
            std::string_view{"delete value;"},
            std::string_view{"malloc(4);"},
            std::string_view{"calloc(1, 4);"},
            std::string_view{"realloc(value, 4);"},
            std::string_view{"free(value);"},
            std::string_view{"std::shared_ptr<int> value;"},
            std::string_view{"std::weak_ptr<int> value;"},
            std::string_view{"LCOV_EXCL_LINE"},
            std::string_view{"__builtin_unreachable();"},
            std::string_view{"[[assume(true)]];"},
            // The synchronisation family for which MSVC >= 14.50 imports api-ms-win-core-synch-l1-2-0.dll.
            std::string_view{"std::jthread worker;"},
            std::string_view{"std::stop_token token;"},
            std::string_view{"std::stop_source source;"},
            std::string_view{"std::stop_callback<void (*)()> callback{token, fn};"},
            std::string_view{"std::call_once(flag, fn);"},
            std::string_view{"std::once_flag flag;"},
            std::string_view{"#include <latch>"},
            std::string_view{"std::latch done{2};"},
            std::string_view{"#include <barrier>"},
            std::string_view{"std::barrier sync{2};"},
            std::string_view{"#include <semaphore>"},
            std::string_view{"std::counting_semaphore<4> slots{4};"},
            std::string_view{"std::binary_semaphore gate{0};"},
            std::string_view{"#include <condition_variable>"},
            std::string_view{"std::condition_variable ready;"},
            std::string_view{"std::condition_variable_any ready;"},
            std::string_view{"flag.wait(false);"},
            std::string_view{"flag.notify_one();"},
            std::string_view{"flag.notify_all();"},
            std::string_view{"std::atomic_wait(&flag, 0);"},
            std::string_view{"std::atomic_notify_one(&flag);"},
            std::string_view{"std::atomic_notify_all(&flag);"},
            std::string_view{"std::atomic_flag_notify_all(&flag);"},
            // Header-level users of the same primitives (review-task24 nit 2).
            std::string_view{"std::timed_mutex guard;"},
            std::string_view{"#include <future>"},
            std::string_view{"std::promise<int> promise;"},
            std::string_view{"#include <syncstream>"},
            std::string_view{"std::osyncstream out{std::cout};"},
            // A function-local static with a non-constexpr initialiser (thread-safe statics).
            std::string_view{"void f() { static const Tables tables; }"},
        };

        for (const auto sample : samples) {
            CAPTURE(sample);
            CHECK_FALSE(scan("project/src/core/Sample.cpp", sample).empty());
            CHECK(scan("project/src/core/Sample.cpp", std::string{"// "} + std::string{sample} + "\n").empty());
            CHECK(scan("project/src/core/Sample.cpp",
                       std::string{"constexpr auto text = \""} + std::string{sample} + "\";")
                      .empty());
        }
    }

    TEST_CASE("scanner catches token spellings that previously bypassed the guard")
    {
        constexpr std::array forbiddenSamples{
            std::string_view{"delete[] p;"},
            std::string_view{"new\n  Foo();"},
            std::string_view{"new\tFoo();"},
            std::string_view{"catch(...) {}"},
            std::string_view{"#include <Windows.h>"},
            std::string_view{"#include \"windows.h\""},
            std::string_view{"std::free (p);"},
            std::string_view{"constexpr auto size = 268'435'456; delete p;"},
            std::string_view{"delete p;"},
            std::string_view{"malloc(4);"},
        };

        for (const auto sample : forbiddenSamples) {
            CAPTURE(sample);
            CHECK_FALSE(scan("project/src/core/Sample.cpp", sample).empty());
        }
    }

    TEST_CASE("scanner respects token boundaries and stripped text")
    {
        constexpr std::array allowedSamples{
            std::string_view{"deleted = true;"},
            std::string_view{"newline();"},
            std::string_view{"renew();"},
            std::string_view{"free_list.clear();"},
            std::string_view{R"(constexpr char quote = '\'';)"},
            std::string_view{R"(constexpr auto text = "delete value";)"},
            std::string_view{"// malloc(4);\nreturn;"},
            std::string_view{"constexpr auto size = 268'435'456;"},
        };

        for (const auto sample : allowedSamples) {
            CAPTURE(sample);
            CHECK(scan("project/src/core/Sample.cpp", sample).empty());
        }
    }

    TEST_CASE("delete rule flags the deallocation operator but not defaulted or deleted special members")
    {
        constexpr std::array notFlagged{
            std::string_view{"S(const S&) = delete;"},
            std::string_view{"S& operator=(S&&) = delete;"},
            std::string_view{"S(S&&)=delete;"},
            std::string_view{"S(const S&) = delete ;"},
            std::string_view{"~S() = default;"},
            std::string_view{"bool deleted = true;"},
            std::string_view{"std::default_delete<int> d;"},
        };
        for (const auto sample : notFlagged) {
            CAPTURE(sample);
            CHECK(scan("project/src/core/Sample.cpp", sample).empty());
        }

        constexpr std::array flagged{
            std::string_view{"delete p;"},    std::string_view{"delete[] p;"},
            std::string_view{"delete [] p;"}, std::string_view{"delete\np;"},
            std::string_view{"delete\tp;"},   std::string_view{"operator delete(void*);"},
        };
        for (const auto sample : flagged) {
            CAPTURE(sample);
            CHECK_FALSE(scan("project/src/core/Sample.cpp", sample).empty());
        }
    }

    TEST_CASE("reinterpret casts are restricted to adapters and pvd")
    {
        CHECK(scan("project/src/core/Sample.cpp", "reinterpret_cast<int>(value);").empty() == false);
        CHECK(scan("project/plugins/avif/src/adapters/avif/Sample.cpp", "reinterpret_cast<int>(value);").empty());
        CHECK(scan("project/src/adapters/win/Sample.cpp", "reinterpret_cast<int>(value);").empty());
        CHECK(scan("project/src/pvd/Sample.cpp", "reinterpret_cast<int>(value);").empty());
        CHECK(scan("project/src/core/Sample.cpp", "// reinterpret_cast<int>(value);").empty());
    }

    TEST_CASE("foreign headers are restricted to adapters and Exports")
    {
        for (const auto include :
             {std::string_view{"#include <windows.h>"}, std::string_view{"#include \"windows.h\""},
              std::string_view{"#include <Windows.h>"}, std::string_view{"#include <avif/avif.h>"},
              std::string_view{"#include \"AVIF/AVIF.H\""}, std::string_view{"#include <dav1d/dav1d.h>"}}) {
            CAPTURE(include);
            CHECK(scan("project/src/core/Sample.cpp", include).empty() == false);
            CHECK(scan("project/plugins/avif/src/core/Sample.cpp", include).empty() == false);
            CHECK(scan("project/plugins/avif/src/DefaultPlugin.cpp", include).empty() == false);
            CHECK(scan("project/src/adapters/win/Sample.cpp", include).empty());
            CHECK(scan("project/plugins/avif/src/adapters/avif/Sample.cpp", include).empty());
            CHECK(scan("project/src/pvd/Exports.cpp", include).empty());
            CHECK(scan("project/src/core/Sample.cpp", std::string{"// "} + std::string{include}).empty());
        }
        CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include <spng.h>").empty());
        CHECK(scan("project/plugins/rpgmvp/src/adapters/spng/Sample.cpp", "#include <spng.h>").empty());
    }

    TEST_CASE("core may include only the shared pvd boundary contracts, in the shared and the plugin tree")
    {
        for (const auto path : {std::string_view{"project/src/core/Sample.cpp"},
                                std::string_view{"project/plugins/avif/src/core/Describe.cpp"}}) {
            CAPTURE(path);
            CHECK(scan(path, "#include \"pvd/Types.hpp\"").empty());
            CHECK(scan(path, "#include <pvd/Plugin.hpp>").empty());
            CHECK(scan(path, "#include \"core/IDecoder.hpp\"").empty());

            for (const auto header :
                 {std::string_view{"Shim.hpp"}, std::string_view{"ContextHandle.hpp"}, std::string_view{"Firewall.hpp"},
                  std::string_view{"PluginFactory.hpp"}, std::string_view{"PluginConstants.hpp"},
                  std::string_view{"PluginIdentity.hpp"}}) {
                CAPTURE(header);
                CHECK_FALSE(scan(path, std::string{"#include \"pvd/"} + std::string{header} + "\"").empty());
            }
        }
    }

    TEST_CASE("core layering matches nested pvd paths and both header extensions")
    {
        // The pattern must see `pvd/<anything>.h` and `pvd/<anything>.hpp` and compare the whole captured
        // name (nested directories included) with the allowlist, so a header placed one level deeper or
        // spelled with `.h` cannot slip past the guard.
        CHECK(scan("project/src/core/Sample.cpp", "#include \"pvd/Types.h\"").empty());
        CHECK(scan("project/src/core/Sample.cpp", "#include <pvd/Plugin.h>").empty());
        CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/Shim.h\"").empty());
        CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/nested/Shim.hpp\"").empty());
        CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/detail/Types.hpp\"").empty());
        CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/Types/Extra.hpp\"").empty());
        CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/PvdApi.hpp\"").empty());
        CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/PluginConstants.hpp\"").empty());
    }

    TEST_CASE("pvd and adapters never include each other except through the two composition roots")
    {
        // ARCHITECTURE §2: `pvd` (shim side) and `adapters` meet only in Exports.cpp / DefaultPlugin.cpp;
        // pvd/PluginFactory.hpp is the seam those two use and pvd/PluginConstants.hpp is generated per
        // plugin, so only those two (each compiled once per plugin) may include either.
        for (const auto path :
             {std::string_view{"project/src/pvd/Shim.cpp"}, std::string_view{"project/src/pvd/Shim.hpp"},
              std::string_view{"project/src/pvd/Exports.cpp"}}) {
            CAPTURE(path);
            CHECK_FALSE(scan(path, "#include \"adapters/win/FileSource.hpp\"").empty());
            CHECK_FALSE(scan(path, "#include <adapters/avif/Decoder.hpp>").empty());
            CHECK(scan(path, "// #include \"adapters/win/FileSource.hpp\"").empty());
            CHECK(scan(path, "#include \"pvd/Shim.hpp\"").empty());
            CHECK(scan(path, "#include \"pvd/PluginIdentity.hpp\"").empty());
        }
        CHECK(scan("project/src/pvd/Exports.cpp", "#include \"pvd/PluginFactory.hpp\"").empty());
        CHECK(scan("project/src/pvd/Exports.cpp", "#include \"pvd/PluginConstants.hpp\"").empty());
        CHECK_FALSE(scan("project/src/pvd/Shim.cpp", "#include \"pvd/PluginFactory.hpp\"").empty());
        CHECK_FALSE(scan("project/src/pvd/Shim.hpp", "#include \"pvd/PluginFactory.h\"").empty());
        CHECK_FALSE(scan("project/src/pvd/Shim.cpp", "#include \"pvd/PluginConstants.hpp\"").empty());
        CHECK_FALSE(scan("project/src/pvd/Shim.hpp", "#include <pvd/PluginConstants.h>").empty());

        for (const auto path : {std::string_view{"project/plugins/avif/src/adapters/avif/Decoder.hpp"},
                                std::string_view{"project/src/adapters/win/FileSource.cpp"}}) {
            CAPTURE(path);
            CHECK_FALSE(scan(path, "#include \"pvd/Shim.hpp\"").empty());
            CHECK_FALSE(scan(path, "#include \"pvd/Types.hpp\"").empty());
            CHECK_FALSE(scan(path, "#include \"pvd/PluginFactory.hpp\"").empty());
            CHECK_FALSE(scan(path, "#include \"pvd/PluginConstants.hpp\"").empty());
            CHECK(scan(path, "#include \"core/IDecoder.hpp\"").empty());
            CHECK(scan(path, "#include \"adapters/win/Utf8.hpp\"").empty());
            CHECK(scan(path, "// #include \"pvd/Shim.hpp\"").empty());
        }
        for (const auto header :
             {std::string_view{"pvd/PluginFactory.hpp"}, std::string_view{"pvd/Plugin.hpp"},
              std::string_view{"pvd/Types.hpp"}, std::string_view{"pvd/PluginConstants.hpp"},
              std::string_view{"adapters/win/FileSource.hpp"}, std::string_view{"core/Describe.hpp"}}) {
            CAPTURE(header);
            CHECK(scan("project/plugins/avif/src/DefaultPlugin.cpp",
                       std::string{"#include \""} + std::string{header} + "\"")
                      .empty());
        }
        for (const auto header :
             {std::string_view{"pvd/Shim.hpp"}, std::string_view{"pvd/ContextHandle.hpp"},
              std::string_view{"pvd/Firewall.hpp"}, std::string_view{"pvd/PvdApi.hpp"},
              std::string_view{"pvd/PluginIdentity.hpp"}, std::string_view{"pvd/nested/Types.hpp"}}) {
            CAPTURE(header);
            CHECK_FALSE(scan("project/plugins/avif/src/DefaultPlugin.cpp",
                             std::string{"#include \""} + std::string{header} + "\"")
                            .empty());
        }
    }

    TEST_CASE("only a plugin's DefaultPlugin.cpp may live outside the layer directories")
    {
        CHECK(scan("project/plugins/avif/src/DefaultPlugin.cpp", "int x;").empty());
        CHECK_FALSE(scan("project/src/DefaultPlugin.cpp", "int x;").empty());
        CHECK_FALSE(scan("project/src/Stray.cpp", "int x;").empty());
        CHECK_FALSE(scan("project/plugins/avif/src/Other.cpp", "int x;").empty());
        CHECK_FALSE(scan("project/plugins/avif/src/misc/Other.hpp", "int x;").empty());
        CHECK(scan("project/plugins/avif/src/core/Describe.hpp", "int x;").empty());
        CHECK(scan("project/plugins/avif/src/adapters/avif/Decoder.hpp", "int x;").empty());
    }

    TEST_CASE("shared code never includes a plugin header")
    {
        const auto index = fakeIndex();
        for (const auto path :
             {std::string_view{"project/src/core/FileSession.cpp"}, std::string_view{"project/src/pvd/Exports.cpp"},
              std::string_view{"project/src/adapters/win/FileSource.cpp"}}) {
            CAPTURE(path);
            CHECK(scan(path, "#include \"core/Error.hpp\"", index).empty());
            CHECK(scan(path, "#include <doctest/doctest.h>", index).empty());
            // core/Describe.hpp exists under both plugin roots and under no shared root: still a plugin
            // header from where shared code stands.
            CHECK_FALSE(scan(path, "#include \"core/Describe.hpp\"", index).empty());
            CHECK_FALSE(scan(path, "#include <adapters/spng/Decoder.hpp>", index).empty());
            CHECK(scan(path, "// #include \"core/Describe.hpp\"", index).empty());
        }
        // Without an index (unit samples) the ownership rule is inert.
        CHECK(scan("project/src/core/FileSession.cpp", "#include \"core/Describe.hpp\"").empty());
    }

    TEST_CASE("a plugin includes shared headers and its own, never another plugin's")
    {
        const auto index = fakeIndex();
        const auto avif = std::string_view{"project/plugins/avif/src/adapters/avif/Decoder.cpp"};
        CHECK(scan(avif, "#include \"core/Error.hpp\"", index).empty());
        CHECK(scan(avif, "#include \"adapters/win/Utf8.hpp\"", index).empty()); // shared adapters are includable
        CHECK(scan(avif, "#include \"adapters/avif/Decoder.hpp\"", index).empty());
        CHECK(scan(avif, "#include <dav1d/dav1d.h>", index).empty());
        CHECK_FALSE(scan(avif, "#include \"adapters/spng/Decoder.hpp\"", index).empty());
        CHECK_FALSE(scan(avif, "#include <adapters/spng/Decoder.hpp>", index).empty());

        const auto rpgmvp = std::string_view{"project/plugins/rpgmvp/src/adapters/spng/Decoder.cpp"};
        CHECK(scan(rpgmvp, "#include \"adapters/spng/Decoder.hpp\"", index).empty());
        CHECK(scan(rpgmvp, "#include \"adapters/win/Utf8.hpp\"", index).empty());
        CHECK(scan(rpgmvp, "#include \"pvd/Types.hpp\"", index).empty() == false); // adapters rule, not ownership
        CHECK_FALSE(scan(rpgmvp, "#include \"adapters/avif/Decoder.hpp\"", index).empty());
        CHECK_FALSE(scan(rpgmvp, "#include <adapters/avif/Decoder.hpp>", index).empty());

        // A file outside every indexed root is not subject to ownership at all.
        CHECK(scan("elsewhere/src/core/Sample.cpp", "#include \"core/Describe.hpp\"", index).empty());
    }

    TEST_CASE("a header name shared by two plugins resolves to the includer's own copy")
    {
        // Every plugin keeps its describer in src/core/Describe.hpp (ARCHITECTURE §6), so the same
        // root-relative name exists under every plugin root. The include is resolved against the
        // includer's own root first: its own copy is allowed from anywhere in the plugin, and the
        // existence of a namesake under another plugin's root changes nothing.
        const auto index = fakeIndex();
        for (const auto path : {std::string_view{"project/plugins/avif/src/adapters/avif/Decoder.cpp"},
                                std::string_view{"project/plugins/avif/src/core/Describe.cpp"},
                                std::string_view{"project/plugins/avif/src/DefaultPlugin.cpp"},
                                std::string_view{"project/plugins/rpgmvp/src/adapters/spng/Decoder.cpp"},
                                std::string_view{"project/plugins/rpgmvp/src/core/Describe.cpp"},
                                std::string_view{"project/plugins/rpgmvp/src/DefaultPlugin.cpp"}}) {
            CAPTURE(path);
            CHECK(scan(path, "#include \"core/Describe.hpp\"", index).empty());
            CHECK(scan(path, "#include <core/Describe.hpp>", index).empty());
        }
        // The same name from shared code resolves in no shared root and is still rejected.
        CHECK_FALSE(scan("project/src/core/CodecPlugin.cpp", "#include \"core/Describe.hpp\"", index).empty());
    }

    TEST_CASE("a plugin header cannot shadow a shared header")
    {
        const auto index = fakeIndex();
        CHECK_FALSE(scan("project/plugins/avif/src/core/Error.hpp", "#pragma once", index).empty());
        CHECK(scan("project/plugins/avif/src/core/Describe.hpp", "#pragma once", index).empty());
        CHECK(scan("project/src/core/Error.hpp", "#pragma once", index).empty());
    }

    TEST_CASE("PvdApi.hpp is the one pvd header that may include Windows.h")
    {
        CHECK(scan("project/src/pvd/PvdApi.hpp", "#include <Windows.h>").empty());
        CHECK_FALSE(scan("project/src/pvd/PvdApi.hpp", "#include <avif/avif.h>").empty());
        CHECK_FALSE(scan("project/src/pvd/Shim.hpp", "#include <Windows.h>").empty());
        CHECK_FALSE(scan("project/src/pvd/Shim.cpp", "#include \"windows.h\"").empty());
    }

    TEST_CASE("core layering is an allowlist, not a fixed blocklist")
    {
        // Any pvd/... header other than the two boundary contracts is forbidden in core, not just the
        // historically enumerated ones.
        CHECK(scan("project/src/core/Sample.cpp", "#include \"pvd/Types.hpp\"").empty());
        CHECK_FALSE(scan("project/src/core/Sample.cpp", "#include \"pvd/Marshal.hpp\"").empty());
    }

    TEST_CASE("the synchronisation family behind the Windows 8 synch API set is caught in every spelling")
    {
        // MSVC >= 14.50 imports WaitOnAddress / WakeByAddress* from api-ms-win-core-synch-l1-2-0.dll
        // for thread-safe statics, atomic wait/notify and everything built on them; a plugin must
        // stay KERNEL32-only, so each of these is forbidden under src/** and plugins/*/src/**.
        constexpr std::array flagged{
            std::string_view{"std::jthread worker{fn};"},
            std::string_view{"std::vector<std::jthread> workers;"},
            std::string_view{"jthread worker{fn};"},
            std::string_view{"#include <stop_token>"},
            std::string_view{"std::stop_token token = source.get_token();"},
            std::string_view{"std::stop_source source;"},
            std::string_view{"std::stop_callback callback{token, fn};"},
            std::string_view{"std::once_flag once;"},
            std::string_view{"std::call_once(once, fn);"},
            std::string_view{"call_once(once, fn);"},
            std::string_view{"#include <latch>"},
            std::string_view{"# include < latch >"},
            std::string_view{"std::latch done{2};"},
            std::string_view{"std :: latch done{2};"},
            std::string_view{"#include <barrier>"},
            std::string_view{"std::barrier<> sync{2};"},
            std::string_view{"#include <semaphore>"},
            std::string_view{"std::counting_semaphore<4> slots{4};"},
            std::string_view{"std::binary_semaphore gate{0};"},
            std::string_view{"#include <condition_variable>"},
            std::string_view{"std::condition_variable ready;"},
            std::string_view{"std::condition_variable_any ready;"},
            std::string_view{"condition_variable ready;"},
            std::string_view{"flag.wait(false);"},
            std::string_view{"flag .wait (false);"},
            std::string_view{"flag->wait(false);"},
            std::string_view{"ready.wait_for(lock, 1ms);"},
            std::string_view{"ready.wait_until(lock, deadline);"},
            std::string_view{"flag.notify_one();"},
            std::string_view{"flag->notify_one();"},
            std::string_view{"flag.notify_all();"},
            std::string_view{"flag->notify_all();"},
            std::string_view{"std::atomic_wait(&flag, 0);"},
            std::string_view{"std::atomic_wait_explicit(&flag, 0, order);"},
            std::string_view{"std::atomic_notify_one(&flag);"},
            std::string_view{"std::atomic_notify_all(&flag);"},
            std::string_view{"std::atomic_flag_wait(&flag, false);"},
            std::string_view{"std::atomic_flag_wait_explicit(&flag, false, order);"},
            std::string_view{"std::atomic_flag_notify_one(&flag);"},
            std::string_view{"std::atomic_flag_notify_all(&flag);"},
            // Header-implemented over condition_variable / _Cnd_t / __std_atomic_wait_direct in
            // the MSVC STL (review-task24 nit 2): the timed mutexes, <future> and <syncstream>.
            std::string_view{"std::timed_mutex guard;"},
            std::string_view{"std::recursive_timed_mutex guard;"},
            std::string_view{"std::shared_timed_mutex guard;"},
            std::string_view{"timed_mutex guard;"},
            std::string_view{"#include <future>"},
            std::string_view{"# include < future >"},
            std::string_view{"std::future<int> result;"},
            std::string_view{"std::shared_future<int> result;"},
            std::string_view{"std::promise<int> promise;"},
            std::string_view{"std::packaged_task<int()> task;"},
            std::string_view{"auto result = std::async(std::launch::async, fn);"},
            std::string_view{"std :: future<int> result;"},
            std::string_view{"#include <syncstream>"},
            std::string_view{"std::osyncstream out{std::cout};"},
            std::string_view{"std::syncbuf buffer{stream.rdbuf()};"},
            std::string_view{"std::basic_osyncstream<char> out{std::cout};"},
            std::string_view{"std::basic_syncbuf<char> buffer;"},
        };
        for (const auto sample : flagged) {
            CAPTURE(sample);
            CHECK_FALSE(scan("project/src/core/Sample.cpp", sample).empty());
            CHECK_FALSE(scan("project/plugins/avif/src/adapters/avif/Sample.cpp", sample).empty());
            CHECK_FALSE(scan("project/src/pvd/Exports.cpp", sample).empty());
        }

        // std::thread plus join, std::mutex (SRWLOCK-backed) and plain atomics import nothing
        // beyond KERNEL32; identifiers that merely contain a forbidden word are not the token.
        constexpr std::array allowed{
            std::string_view{"#include <thread>"},
            std::string_view{"std::thread worker{fn};"},
            std::string_view{"std::vector<std::thread> workers;"},
            std::string_view{"worker.join();"},
            std::string_view{"#include <mutex>"},
            std::string_view{"std::mutex guard;"},
            std::string_view{"const std::scoped_lock lock{guard};"},
            std::string_view{"#include <atomic>"},
            std::string_view{"std::atomic<int> counter{0};"},
            std::string_view{"counter.fetch_add(1);"},
            std::string_view{"counter.load();"},
            std::string_view{"waiting = true;"},
            std::string_view{"awaitable.await();"},
            std::string_view{"notify_owner();"},
            std::string_view{"host.notify_one_page();"},
            std::string_view{"int latch_count = 0;"},
            std::string_view{"std::vector<int> latches;"},
            std::string_view{"barrier_free();"},
            std::string_view{"semaphore_like();"},
            std::string_view{"my_stop_token();"},
            std::string_view{"call_once_more();"},
            std::string_view{"jthreads_started = 0;"},
            std::string_view{"std::atomic_flag ready;"},
            std::string_view{"ready.test_and_set();"},
            std::string_view{"ready.clear();"},
            std::string_view{"std::shared_mutex guard;"},
            std::string_view{"std::recursive_mutex guard;"},
            std::string_view{"const std::unique_lock lock{guard};"},
            std::string_view{"timed_mutexes = 0;"},
            std::string_view{"int future = 0;"},
            std::string_view{"promise_kept = true;"},
            std::string_view{"asyncCount = 0;"},
            std::string_view{"std::vector<int> futures;"},
            std::string_view{"syncbuffer.flush();"},
            std::string_view{"std::ostream &out = stream;"},
            std::string_view{"// std::jthread worker;\nreturn;"},
            std::string_view{"constexpr auto text = \"flag.notify_all();\";"},
        };
        for (const auto sample : allowed) {
            CAPTURE(sample);
            CHECK(scan("project/src/core/Sample.cpp", sample).empty());
        }
    }

    TEST_CASE("a function-local static is forbidden unless it is constexpr")
    {
        // MSVC >= 14.50 implements the guarded initialisation of a function-local static
        // (_Init_thread_wait / _Init_thread_notify) over WaitOnAddress / WakeByAddressAll from
        // api-ms-win-core-synch-l1-2-0.dll; a plugin must own such state through its composition root.
        constexpr std::array flagged{
            std::string_view{"void f() { static const Tables tables; }"},
            std::string_view{"void f() { static Tables tables; }"},
            std::string_view{"void f() { static auto tables = make(); }"},
            std::string_view{"void f() { static int counter = 0; }"},
            std::string_view{"void f() { static std::string name{}; }"},
            std::string_view{"void f() { static const Tables tables(1, 2); }"},
            std::string_view{"void f() { static thread_local int slot = 0; }"},
            std::string_view{"void f() { static Tables tables; return tables.value(); }"},
            std::string_view{"void f()\n{\n    static\nconst Tables tables;\n}"},
            std::string_view{"void f() { if (x) { static const Tables tables; } }"},
            std::string_view{"void f() { for (;;) { static int n = 0; } }"},
            std::string_view{"void f() { auto l = [] { static int z = 0; return z; }; }"},
            std::string_view{"void f() { std::array<int, 2> a{1, 2}; static int q; }"},
            std::string_view{"const Tables &tables() { static const Tables tables; return tables; }"},
            std::string_view{
                "[[nodiscard]] const Tables &tables() noexcept { static const Tables tables; return tables; }"},
            std::string_view{"template <class F> auto guarded(F &&f) noexcept { static int calls = 0; return f(); }"},
            std::string_view{"template <typename T, typename U = Pair<T, T>> T g() { static T value; return value; }"},
            std::string_view{"extern \"C\" UINT32 __stdcall pvdInit(void) { static int calls = 0; return calls; }"},
            std::string_view{"namespace { struct S { int f() { static int x = 1; return x; } }; }"},
            std::string_view{"namespace a::b { class C { public: void f() { static int x; } }; }"},
            std::string_view{"struct S { std::array<int, 2> a_{}; void g() { static int q; } };"},
            std::string_view{"struct S final : Base { void g() override { static int q; } };"},
            std::string_view{"enum class E : std::uint8_t { A, B }; void f() { static E e = E::A; }"},
            std::string_view{"S::S() : a_(1), b_{2} { static int q; }"},
            std::string_view{"auto f() -> Tables { static Tables t; return t; }"},
            std::string_view{"int f() { switch (x) { case 1: { static int q; return q; } default: return 0; } }"},
            std::string_view{"void f() { do { static int q; } while (x); }"},
            std::string_view{"struct S { int x = [] { static int q = 0; return q; }(); };"},
        };
        for (const auto sample : flagged) {
            CAPTURE(sample);
            CHECK_FALSE(scan("project/src/core/Sample.cpp", sample).empty());
            CHECK_FALSE(scan("project/plugins/avif/src/DefaultPlugin.cpp", sample).empty());
            CHECK_FALSE(scan("project/src/pvd/Exports.cpp", sample).empty());
        }

        // static constexpr (no guard, constant initialisation), static_assert, static_cast, static
        // member functions and static data members at class scope, and namespace-scope declarations
        // are not function-local statics.
        constexpr std::array allowed{
            std::string_view{"void f() { static constexpr int k = 3; }"},
            std::string_view{"void f() { static constexpr std::array<int, 2> k{1, 2}; }"},
            std::string_view{"void f() { static_assert(sizeof(int) == 4); }"},
            std::string_view{"void f() { return static_cast<int>(x); }"},
            std::string_view{"void f() { static_assert(std::is_trivially_destructible_v<Tables>); }"},
            std::string_view{"class Foo { static bool needed(const Cicp &cicp) noexcept; };"},
            std::string_view{"class Foo {\n  public:\n    [[nodiscard]] static Result<Foo> create(int x);\n};"},
            std::string_view{"struct Foo final : public Bar { static Result<Foo> open(std::wstring_view path); };"},
            std::string_view{"template <class T> struct Foo { static constexpr std::size_t kCount = 65'536; };"},
            std::string_view{"struct S { static int count(); };"},
            std::string_view{"void f() { struct Local { static int h(); }; }"},
            std::string_view{"namespace { int x = 0; }"},
            std::string_view{"static int helper() { return 0; }"},
            std::string_view{"namespace { static int x = 0; }"},
            std::string_view{"class Foo { static bool needed(int) noexcept { return true; } };"},
            std::string_view{"union U { static int f(); };"},
            std::string_view{"enum class E : std::uint8_t { A, B };"},
            std::string_view{"extern \"C\" { int x; }"},
            std::string_view{"inline namespace v1 { class Foo { static int f(); }; }"},
            std::string_view{"void f() { } }"},
            std::string_view{"// void f() { static const Tables tables; }\n"},
            std::string_view{"constexpr auto text = \"void f() { static const Tables tables; }\";"},
        };
        for (const auto sample : allowed) {
            CAPTURE(sample);
            CHECK(scan("project/src/core/Sample.cpp", sample).empty());
        }
    }

    TEST_CASE("catch-all handling is restricted to Firewall")
    {
        CHECK(scan("project/src/core/Sample.cpp", "try {} catch (...) {}").empty() == false);
        CHECK(scan("project/src/pvd/Firewall.hpp", "try {} catch (...) {}").empty());
        CHECK(scan("project/src/core/Sample.cpp", "// catch (...)").empty());
    }

    TEST_CASE("the shared and every plugin source tree obey the ownership and layering rules")
    {
        const auto repository = std::filesystem::path{PVDKIT_SOURCE_DIR};
        const auto roots = sourceRoots(repository);
        const auto index = indexHeaders(roots);
        // The shared root plus at least one plugin, or the walk below proves nothing.
        REQUIRE(roots.size() >= 2);
        std::ostringstream failures;
        std::size_t failureCount = 0;
        std::size_t scanned = 0;

        for (const auto &root : roots) {
            for (const auto &entry : std::filesystem::recursive_directory_iterator{root}) {
                if (!entry.is_regular_file() || !isSourceFile(entry.path())) {
                    continue;
                }
                ++scanned;
                for (const auto &violation : scan(entry.path(), readFile(entry.path()), index)) {
                    ++failureCount;
                    failures << entry.path().generic_string() << ": forbidden token " << violation.token << '\n';
                }
            }
        }

        INFO(failures.str());
        CHECK(scanned > 0);
        CHECK(failureCount == 0);
    }

} // namespace
