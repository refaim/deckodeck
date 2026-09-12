// doctest's implementation and main(), compiled once into every test executable.
//
// Under the asan preset this translation unit alone is left uninstrumented: clang-cl 19's
// AddressSanitizer breaks a rethrow (`throw;`) inside a catch handler on x64 Windows - the
// rethrown object reaches the inner handler as garbage (an access violation or a 0xC0000409
// fail-fast, reproduced with a ten-line probe) - while an uninstrumented rethrowing function
// called from an instrumented catch block works. doctest's exception translation
// (translateActiveException, the exception translators) is the only rethrow in this tree - the
// framework, not the code under test; nothing under src/ or plugins/*/src/ rethrows (the firewall
// swallows). The pragma applies the exemption to every function this file defines, so every
// instrumented CHECK_THROWS in the tests keeps working and every line under test keeps its checks.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PVDKIT_TEST_MAIN_NO_ASAN 1
#endif
#endif
#if defined(PVDKIT_TEST_MAIN_NO_ASAN)
#pragma clang attribute push(__attribute__((no_sanitize("address"))), apply_to = function)
#endif
#include <doctest/doctest.h>
#if defined(PVDKIT_TEST_MAIN_NO_ASAN)
#pragma clang attribute pop
#endif
