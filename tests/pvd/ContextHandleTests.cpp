#include <memory>

#include <doctest/doctest.h>

#include "pvd/ContextHandle.hpp"

#include "Fakes.hpp"

namespace avifpvd::pvd {
namespace {

TEST_CASE("context handle round trip preserves identity and unique ownership") {
  test::FakeState state;
  auto session = std::make_unique<test::FakeSession>(state);
  const auto identity = session.get();

  void* hostContext = toHost(std::move(session));
  CHECK(session == nullptr);
  CHECK(state.liveSessions == 1);
  CHECK(borrow(hostContext) == identity);

  auto restored = fromHost(hostContext);
  CHECK(restored.get() == identity);
  CHECK(state.liveSessions == 1);
  restored.reset();
  CHECK(state.liveSessions == 0);
}

TEST_CASE("null context conversions remain empty") {
  CHECK(fromHost(nullptr) == nullptr);
  CHECK(borrow(nullptr) == nullptr);
  CHECK(toHost({}) == nullptr);
}

}  // namespace
}  // namespace avifpvd::pvd
