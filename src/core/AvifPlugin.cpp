#include "core/AvifPlugin.hpp"

#include <cstddef>
#include <span>
#include <utility>

#include "core/Describe.hpp"
#include "core/FileSession.hpp"

namespace avifpvd::core {

AvifPlugin::AvifPlugin(IFileSource &fileSource, IDecoderFactory &decoderFactory,
                       const DecoderOptions options, pvd::PluginInfo pluginInfo)
    : fileSource_(fileSource), decoderFactory_(decoderFactory),
      options_(options), pluginInfo_(std::move(pluginInfo)) {}

const pvd::PluginInfo &AvifPlugin::info() const { return pluginInfo_; }

Result<std::unique_ptr<pvd::IFileSession>>
AvifPlugin::open(const pvd::OpenRequest &request) {
  if (!decoderFactory_.looksLikeAvif(request.head)) {
    return std::unexpected(
        Error{ErrorCode::NotAvif, "input does not have an AVIF signature"});
  }

  std::unique_ptr<IFileData> fileData;
  std::span<const std::byte> bytes = request.head;
  if (request.fileSize != 0) {
    auto opened = fileSource_.open(request.utf8FileName);
    if (!opened) {
      return std::unexpected(opened.error());
    }
    fileData = std::move(*opened);
    bytes = fileData->bytes();
  }

  auto decoder = decoderFactory_.create(bytes, options_);
  if (!decoder) {
    return std::unexpected(decoder.error());
  }

  const auto &meta = (*decoder)->meta();
  pvd::ImageInfo imageInfo{meta.frameCount, meta.animated, "AVIF", "AV1",
                           describe(meta)};
  std::unique_ptr<pvd::IFileSession> session = std::make_unique<FileSession>(
      std::move(fileData), std::move(*decoder), std::move(imageInfo), options_);
  return session;
}

} // namespace avifpvd::core
