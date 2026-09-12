#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "core/Error.hpp"
#include "pvd/Types.hpp"

namespace avifpvd::pvd {

/// Defines one independently opened PictureView file session.
class IFileSession {
 public:
  virtual ~IFileSession() = default;
  [[nodiscard]] virtual const ImageInfo& imageInfo() const = 0;
  [[nodiscard]] virtual core::Result<PageInfo> pageInfo(std::uint32_t page) const = 0;
  [[nodiscard]] virtual core::Result<DecodedPage> decodePage(std::uint32_t page, const Progress&) = 0;
  virtual bool freePage(std::span<const std::byte> pixels) = 0;
};

/// Defines the C++ boundary consumed by the PVD marshalling layer.
class IPlugin {
 public:
  virtual ~IPlugin() = default;
  [[nodiscard]] virtual const PluginInfo& info() const = 0;
  [[nodiscard]] virtual core::Result<std::unique_ptr<IFileSession>> open(const OpenRequest&) = 0;
};

}  // namespace avifpvd::pvd
