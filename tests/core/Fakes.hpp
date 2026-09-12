#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/IDecoder.hpp"
#include "core/IFileSource.hpp"

namespace avifpvd::core::test {

struct SignatureException final : std::runtime_error {
  SignatureException() : std::runtime_error("scripted signature exception") {}
};

struct FileOpenException final : std::runtime_error {
  FileOpenException() : std::runtime_error("scripted file-open exception") {}
};

struct DecoderCreateException final : std::runtime_error {
  DecoderCreateException() : std::runtime_error("scripted factory exception") {}
};

struct FrameTimingException final : std::runtime_error {
  FrameTimingException() : std::runtime_error("scripted timing exception") {}
};

struct DecodeFrameException final : std::runtime_error {
  DecodeFrameException() : std::runtime_error("scripted decode exception") {}
};

inline Error error(const ErrorCode code,
                   std::string detail = "scripted failure") {
  return Error{code, std::move(detail)};
}

struct DecoderState {
  std::vector<std::uint32_t> timingFrames;
  std::vector<std::uint32_t> decodedFrames;
  std::vector<pvd::PixelFormat> decodedFormats;
  std::vector<std::uint32_t> decodedPitches;
  std::vector<std::size_t> decodedSizes;
  std::optional<Error> timingError;
  std::optional<Error> decodeError;
  bool throwOnTiming = false;
  bool throwOnDecode = false;
  std::uint32_t durationMs = 125;
  int destructions = 0;
};

class FakeDecoder final : public IDecoder {
public:
  FakeDecoder(ImageMeta meta, DecoderState &state)
      : meta_(std::move(meta)), state_(state) {}
  ~FakeDecoder() override { ++state_.get().destructions; }

  [[nodiscard]] const ImageMeta &meta() const override { return meta_; }

  [[nodiscard]] Result<FrameTiming>
  frameTiming(const std::uint32_t frame) const override {
    auto &state = state_.get();
    state.timingFrames.push_back(frame);
    if (state.throwOnTiming) {
      throw FrameTimingException{};
    }
    if (state.timingError) {
      return std::unexpected(*state.timingError);
    }
    return FrameTiming{state.durationMs};
  }

  [[nodiscard]] Result<void>
  decodeFrame(const std::uint32_t frame, const pvd::PixelFormat format,
              const std::span<std::byte> destination,
              const std::uint32_t pitchBytes) override {
    auto &state = state_.get();
    state.decodedFrames.push_back(frame);
    state.decodedFormats.push_back(format);
    state.decodedPitches.push_back(pitchBytes);
    state.decodedSizes.push_back(destination.size());
    if (state.throwOnDecode) {
      throw DecodeFrameException{};
    }
    if (state.decodeError) {
      return std::unexpected(*state.decodeError);
    }

    // Pixel id = 1 + frame * 100 + row-major index, so pages of different
    // frames are distinguishable (frame 0: 1..6, frame 1: 101..106, ...).
    const auto channels = format == pvd::PixelFormat::Bgra32 ? 4U : 3U;
    for (std::uint32_t y = 0; y < meta_.height; ++y) {
      for (std::uint32_t x = 0; x < meta_.width; ++x) {
        const auto id = 1U + frame * 100U + y * meta_.width + x;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
          const auto offset =
              static_cast<std::size_t>(y) * pitchBytes + x * channels + channel;
          destination[offset] =
              std::byte{static_cast<unsigned char>(id + channel * 16)};
        }
      }
    }
    return {};
  }

private:
  ImageMeta meta_;
  std::reference_wrapper<DecoderState> state_;
};

struct FileSourceState {
  int openCalls = 0;
  int dataDestructions = 0;
  std::string openedPath;
  std::vector<std::byte> fileBytes;
  std::optional<Error> openError;
  bool throwOnOpen = false;
};

class FakeFileData final : public IFileData {
public:
  FakeFileData(std::vector<std::byte> bytes, int &destructions)
      : bytes_(std::move(bytes)), destructions_(destructions) {}
  ~FakeFileData() override { ++destructions_.get(); }

  [[nodiscard]] std::span<const std::byte> bytes() const override {
    return bytes_;
  }

private:
  std::vector<std::byte> bytes_;
  std::reference_wrapper<int> destructions_;
};

class FakeFileSource final : public IFileSource {
public:
  explicit FakeFileSource(FileSourceState &state) : state_(state) {}

  [[nodiscard]] Result<std::unique_ptr<IFileData>>
  open(const std::string_view utf8Path) override {
    auto &state = state_.get();
    ++state.openCalls;
    state.openedPath = utf8Path;
    if (state.throwOnOpen) {
      throw FileOpenException{};
    }
    if (state.openError) {
      return std::unexpected(*state.openError);
    }
    return std::unique_ptr<IFileData>{std::make_unique<FakeFileData>(
        state.fileBytes, state.dataDestructions)};
  }

private:
  std::reference_wrapper<FileSourceState> state_;
};

struct FactoryState {
  bool looksLikeAvif = true;
  int lookCalls = 0;
  int createCalls = 0;
  std::vector<std::byte> lookedAt;
  std::vector<std::byte> createdFrom;
  DecoderOptions receivedOptions{};
  std::optional<Error> createError;
  bool throwOnLook = false;
  bool throwOnCreate = false;
};

class FakeDecoderFactory final : public IDecoderFactory {
public:
  FakeDecoderFactory(FactoryState &factoryState, DecoderState &decoderState,
                     ImageMeta meta)
      : factoryState_(factoryState), decoderState_(decoderState),
        meta_(std::move(meta)) {}

  [[nodiscard]] bool
  looksLikeAvif(const std::span<const std::byte> head) const override {
    auto &state = factoryState_.get();
    ++state.lookCalls;
    state.lookedAt.assign(head.begin(), head.end());
    if (state.throwOnLook) {
      throw SignatureException{};
    }
    return state.looksLikeAvif;
  }

  [[nodiscard]] Result<std::unique_ptr<IDecoder>>
  create(const std::span<const std::byte> file,
         const DecoderOptions &options) override {
    auto &state = factoryState_.get();
    ++state.createCalls;
    state.createdFrom.assign(file.begin(), file.end());
    state.receivedOptions = options;
    if (state.throwOnCreate) {
      throw DecoderCreateException{};
    }
    if (state.createError) {
      return std::unexpected(*state.createError);
    }
    return std::unique_ptr<IDecoder>{
        std::make_unique<FakeDecoder>(meta_, decoderState_.get())};
  }

private:
  std::reference_wrapper<FactoryState> factoryState_;
  std::reference_wrapper<DecoderState> decoderState_;
  ImageMeta meta_;
};

inline ImageMeta meta(const std::uint32_t width = 3,
                      const std::uint32_t height = 2,
                      const bool hasAlpha = false, const std::uint8_t depth = 8,
                      const std::uint32_t frameCount = 1,
                      const bool animated = false, Transforms transforms = {}) {
  return ImageMeta{width,
                   height,
                   depth,
                   ChromaFormat::Yuv420,
                   hasAlpha,
                   false,
                   Cicp{1, 13, 6, false},
                   frameCount,
                   animated,
                   std::move(transforms),
                   false,
                   false,
                   false};
}

inline DecoderOptions options(const std::uint64_t maxPixels = 1'000) {
  return DecoderOptions{2, false, maxPixels, 100};
}

inline pvd::ImageInfo imageInfo(const ImageMeta &imageMeta) {
  return pvd::ImageInfo{imageMeta.frameCount, imageMeta.animated, "AVIF", "AV1",
                        "test image"};
}

} // namespace avifpvd::core::test
