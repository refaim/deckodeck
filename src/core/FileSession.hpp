#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/IDecoder.hpp"
#include "core/IFileSource.hpp"
#include "core/PixelBuffer.hpp"
#include "pvd/Plugin.hpp"

namespace pvdkit::core {

class FileSession final : public pvd::IFileSession {
public:
  FileSession(std::unique_ptr<IFileData> fileData,
              std::unique_ptr<IDecoder> decoder, pvd::ImageInfo imageInfo,
              DecoderOptions options);
  ~FileSession() override;

  FileSession(const FileSession &) = delete;
  FileSession &operator=(const FileSession &) = delete;
  FileSession(FileSession &&) = delete;
  FileSession &operator=(FileSession &&) = delete;

  [[nodiscard]] const pvd::ImageInfo &imageInfo() const override;
  [[nodiscard]] Result<pvd::PageInfo>
  pageInfo(std::uint32_t page) const override;
  [[nodiscard]] Result<pvd::DecodedPage>
  decodePage(std::uint32_t page, const pvd::Progress &progress) override;
  bool freePage(std::span<const std::byte> pixels) override;

private:
  std::unique_ptr<IFileData> fileData_;
  std::unique_ptr<IDecoder> decoder_;
  pvd::ImageInfo imageInfo_;
  std::vector<std::unique_ptr<PixelBuffer>> outstanding_;
  DecoderOptions options_;
};

} // namespace pvdkit::core
