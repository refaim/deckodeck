#include "adapters/avif/Decoder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <dav1d/dav1d.h>

namespace pvdkit::avif
{
    namespace
    {

        using Mapping = std::pair<avifResult, core::ErrorCode>;

        constexpr std::array kResultMappings{
            Mapping{AVIF_RESULT_INVALID_FTYP, core::ErrorCode::ParseFailed},
            Mapping{AVIF_RESULT_NO_CONTENT, core::ErrorCode::ParseFailed},
            Mapping{AVIF_RESULT_NO_YUV_FORMAT_SELECTED, core::ErrorCode::ParseFailed},
            Mapping{AVIF_RESULT_BMFF_PARSE_FAILED, core::ErrorCode::ParseFailed},
            Mapping{AVIF_RESULT_MISSING_IMAGE_ITEM, core::ErrorCode::ParseFailed},
            Mapping{AVIF_RESULT_COLOR_ALPHA_SIZE_MISMATCH, core::ErrorCode::ParseFailed},
            Mapping{AVIF_RESULT_ISPE_SIZE_MISMATCH, core::ErrorCode::ParseFailed},
            Mapping{AVIF_RESULT_INVALID_EXIF_PAYLOAD, core::ErrorCode::ParseFailed},
            Mapping{AVIF_RESULT_INVALID_IMAGE_GRID, core::ErrorCode::ParseFailed},
            Mapping{AVIF_RESULT_TRUNCATED_DATA, core::ErrorCode::ParseFailed},
            Mapping{AVIF_RESULT_INVALID_TONE_MAPPED_IMAGE, core::ErrorCode::ParseFailed},
            Mapping{AVIF_RESULT_NO_CODEC_AVAILABLE, core::ErrorCode::DecodeFailed},
            Mapping{AVIF_RESULT_DECODE_COLOR_FAILED, core::ErrorCode::DecodeFailed},
            Mapping{AVIF_RESULT_DECODE_ALPHA_FAILED, core::ErrorCode::DecodeFailed},
            Mapping{AVIF_RESULT_DECODE_GAIN_MAP_FAILED, core::ErrorCode::DecodeFailed},
            Mapping{AVIF_RESULT_DECODE_SAMPLE_TRANSFORM_FAILED, core::ErrorCode::DecodeFailed},
            Mapping{AVIF_RESULT_REFORMAT_FAILED, core::ErrorCode::ConversionFailed},
            // libavif 1.4.2 read.c:7073 (avifDecoderNthImageTiming) and read.c:7111 (avifDecoderNthImage)
            // answer this exactly when frameIndex >= imageCount.
            Mapping{AVIF_RESULT_NO_IMAGES_REMAINING, core::ErrorCode::PageOutOfRange},
            // The container parsed but libavif refuses the feature (e.g. read.c:5955, alpha transforms
            // that differ from the colour item's; unsupported bit depths).
            Mapping{AVIF_RESULT_NOT_IMPLEMENTED, core::ErrorCode::UnsupportedFeature},
            Mapping{AVIF_RESULT_UNSUPPORTED_DEPTH, core::ErrorCode::UnsupportedFeature},
        };

        std::string resultDetail(const avifResult result, const avifDiagnostics &diagnostics)
        {
            std::string detail{avifResultToString(result)};
            if (diagnostics.error[0] != '\0') {
                detail += ": ";
                detail += diagnostics.error;
            }
            return detail;
        }

        core::Error resultError(const avifResult result, const core::ErrorCode code, const avifDiagnostics &diagnostics)
        {
            return {code, resultDetail(result, diagnostics)};
        }

        // libavif 1.4.2 reports a violated imageSizeLimit / imageDimensionLimit only through its
        // diagnostic text, under unrelated result codes: read.c:2153 (grid box, NOT_IMPLEMENTED),
        // read.c:4046 (sequence track, BMFF_PARSE_FAILED) and read.c:5337 (item ispe, BMFF_PARSE_FAILED)
        // all print "... dimensions are too large ...". Re-check these lines on every libavif upgrade.
        bool isSizeLimitDiagnostic(const std::string_view diagnostic)
        {
            return diagnostic.find("dimensions are too large") != std::string_view::npos;
        }

    } // namespace

    core::Result<core::ChromaFormat> detail::chromaFormat(const avifPixelFormat format)
    {
        switch (format) {
        case AVIF_PIXEL_FORMAT_YUV444:
            return core::ChromaFormat::Yuv444;
        case AVIF_PIXEL_FORMAT_YUV422:
            return core::ChromaFormat::Yuv422;
        case AVIF_PIXEL_FORMAT_YUV420:
            return core::ChromaFormat::Yuv420;
        case AVIF_PIXEL_FORMAT_YUV400:
            return core::ChromaFormat::Yuv400;
        case AVIF_PIXEL_FORMAT_NONE:
        case AVIF_PIXEL_FORMAT_COUNT:
            return std::unexpected(
                core::Error{core::ErrorCode::ParseFailed, avifResultToString(AVIF_RESULT_NO_YUV_FORMAT_SELECTED)});
        }
        return std::unexpected(core::Error{core::ErrorCode::Internal, avifResultToString(AVIF_RESULT_UNKNOWN_ERROR)});
    }

    core::Result<core::Transforms> detail::transforms(const avifImage &image, avifDiagnostics &diagnostics)
    {
        core::Transforms result{};
        if ((image.transformFlags & AVIF_TRANSFORM_CLAP) != 0) {
            avifCropRect crop{};
            if (avifCropRectFromCleanApertureBox(&crop, &image.clap, image.width, image.height, &diagnostics) ==
                AVIF_FALSE) {
                return std::unexpected(
                    resultError(AVIF_RESULT_INVALID_ARGUMENT, core::ErrorCode::InvalidTransform, diagnostics));
            }
            result.clap = core::CropRect{crop.x, crop.y, crop.width, crop.height};
        }
        if ((image.transformFlags & AVIF_TRANSFORM_IROT) != 0) {
            result.irotAngle = image.irot.angle;
        }
        if ((image.transformFlags & AVIF_TRANSFORM_IMIR) != 0) {
            // libavif's avifImageMirror comment: axis 0 exchanges top/bottom; axis 1 exchanges left/right.
            result.imir = image.imir.axis == 0 ? core::MirrorAxis::TopBottom : core::MirrorAxis::LeftRight;
        }
        return result;
    }

    core::Result<void> detail::checkedResult(const avifResult result, const core::ErrorCode code,
                                             const avifDiagnostics &diagnostics)
    {
        if (result != AVIF_RESULT_OK) {
            return std::unexpected(resultError(result, code, diagnostics));
        }
        return {};
    }

    std::uint32_t detail::durationMilliseconds(const double durationSeconds)
    {
        const double milliseconds = durationSeconds * 1000.0;
        if (milliseconds <= 0.0) {
            return 0;
        }
        constexpr double maximum = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
        if (milliseconds >= maximum) {
            return std::numeric_limits<std::uint32_t>::max();
        }
        // std::llround: long is 32 bits on Windows (both architectures), so std::lround would
        // overflow between LONG_MAX and the uint32 maximum accepted above.
        return static_cast<std::uint32_t>(std::llround(milliseconds));
    }

    DecoderHandle detail::requireDecoder(DecoderHandle decoder)
    {
        if (!decoder) {
            throw std::bad_alloc{};
        }
        return decoder;
    }

    core::Result<void> detail::checkDestination(const core::ImageMeta &meta, const pvd::PixelFormat format,
                                                const std::size_t dstSize, const std::uint32_t pitchBytes)
    {
        if (format == pvd::PixelFormat::Bgra64) {
            return std::unexpected(
                core::Error{core::ErrorCode::UnsupportedFeature, "libavif output is limited to 8 bits per channel"});
        }
        const std::uint32_t bytesPerPixel = format == pvd::PixelFormat::Bgra32 ? 4U : 3U;
        const std::uint64_t minimumPitch = static_cast<std::uint64_t>(meta.width) * bytesPerPixel;
        if (pitchBytes < minimumPitch) {
            return std::unexpected(
                core::Error{core::ErrorCode::Internal, "caller pitch is smaller than one pixel row"});
        }
        const std::uint64_t required = static_cast<std::uint64_t>(pitchBytes) * meta.height;
        if (dstSize < required) {
            return std::unexpected(
                core::Error{core::ErrorCode::Internal, "caller buffer is smaller than pitch multiplied by height"});
        }
        return {};
    }

    avifRGBImage detail::rgbTarget(const avifImage &image, const pvd::PixelFormat format,
                                   const std::span<std::byte> dst, const std::uint32_t pitchBytes, const int maxThreads)
    {
        avifRGBImage rgb{};
        avifRGBImageSetDefaults(&rgb, &image);
        rgb.depth = 8;
        rgb.format = format == pvd::PixelFormat::Bgra32 ? AVIF_RGB_FORMAT_BGRA : AVIF_RGB_FORMAT_BGR;
        rgb.alphaPremultiplied = AVIF_FALSE;
        rgb.chromaUpsampling = AVIF_CHROMA_UPSAMPLING_AUTOMATIC;
        rgb.maxThreads = maxThreads;
        rgb.pixels = reinterpret_cast<std::uint8_t *>(dst.data());
        rgb.rowBytes = pitchBytes;
        return rgb;
    }

    namespace
    {

        core::Result<core::ImageMeta> imageMeta(avifDecoder &decoder)
        {
            const avifImage &image = *decoder.image;
            return detail::chromaFormat(image.yuvFormat).and_then([&](const core::ChromaFormat chroma) {
                return detail::transforms(image, decoder.diag).transform([&](core::Transforms normalizedTransforms) {
                    return core::ImageMeta{
                        image.width,
                        image.height,
                        static_cast<std::uint8_t>(image.depth),
                        chroma,
                        decoder.alphaPresent != AVIF_FALSE,
                        image.alphaPremultiplied != AVIF_FALSE,
                        core::Cicp{image.colorPrimaries, image.transferCharacteristics, image.matrixCoefficients,
                                   image.yuvRange == AVIF_RANGE_FULL},
                        static_cast<std::uint32_t>(decoder.imageCount),
                        decoder.imageCount > 1,
                        normalizedTransforms,
                        image.icc.size != 0,
                        image.exif.size != 0,
                        image.xmp.size != 0,
                    };
                });
            });
        }

    } // namespace

    void DecoderDestroy::operator()(avifDecoder *decoder) const noexcept
    {
        avifDecoderDestroy(decoder);
    }

    core::ErrorCode errorCodeForResult(const avifResult result) noexcept
    {
        const auto found = std::ranges::find(kResultMappings, result, &Mapping::first);
        return found == kResultMappings.end() ? core::ErrorCode::Internal : found->second;
    }

    std::string libraryVersions()
    {
        return std::string{"libavif "} + avifVersion() + ", dav1d " + dav1d_version() + ", libyuv " +
               std::to_string(avifLibYUVVersion());
    }

    Decoder::Decoder(Key, DecoderHandle decoder, core::ImageMeta meta, const int maxThreads) noexcept
        : decoder_(std::move(decoder)), meta_(meta), maxThreads_(maxThreads)
    {
    }

    const core::ImageMeta &Decoder::meta() const
    {
        return meta_;
    }

    core::Result<core::FrameTiming> Decoder::frameTiming(const std::uint32_t frame) const
    {
        avifImageTiming timing{};
        const avifResult result = avifDecoderNthImageTiming(decoder_.get(), frame, &timing);
        return detail::checkedResult(result, errorCodeForResult(result), decoder_->diag).transform([&] {
            return core::FrameTiming{detail::durationMilliseconds(timing.duration)};
        });
    }

    core::Result<void> Decoder::decodeFrame(const std::uint32_t frame, const pvd::PixelFormat format,
                                            const std::span<std::byte> dst, const std::uint32_t pitchBytes)
    {
        return detail::checkDestination(meta_, format, dst.size(), pitchBytes)
            .and_then([&] {
                const avifResult result = avifDecoderNthImage(decoder_.get(), frame);
                return detail::checkedResult(result, errorCodeForResult(result), decoder_->diag);
            })
            .and_then([&] {
                avifRGBImage rgb = detail::rgbTarget(*decoder_->image, format, dst, pitchBytes, maxThreads_);
                return detail::checkedResult(avifImageYUVToRGB(decoder_->image, &rgb),
                                             core::ErrorCode::ConversionFailed, decoder_->diag);
            });
    }

    bool DecoderFactory::recognises(const std::span<const std::byte> head) const
    {
        if (head.size() < 12) {
            return false;
        }
        const avifROData input{reinterpret_cast<const std::uint8_t *>(head.data()), head.size()};
        return avifPeekCompatibleFileType(&input) != AVIF_FALSE;
    }

    core::Result<std::unique_ptr<core::IDecoder>> DecoderFactory::create(const std::span<const std::byte> file,
                                                                         const core::DecoderOptions &options)
    {
        DecoderHandle decoder = detail::requireDecoder(DecoderHandle{avifDecoderCreate()});

        const int maxThreads = static_cast<int>(
            std::min<unsigned>(options.maxThreads, static_cast<unsigned>(std::numeric_limits<int>::max())));
        decoder->maxThreads = maxThreads;
        decoder->strictFlags = options.strict ? AVIF_STRICT_ENABLED : AVIF_STRICT_DISABLED;
        // libavif 1.4.2 read.c:5292-5296 answers AVIF_RESULT_NOT_IMPLEMENTED for imageSizeLimit == 0 or
        // > AVIF_DEFAULT_IMAGE_SIZE_LIMIT (avif.h:1292-1294: "The value 0 is reserved"), so only that
        // range is handed down; core enforces the real maxPixels in PixelBuffer::create.
        decoder->imageSizeLimit = static_cast<std::uint32_t>(
            std::clamp<std::uint64_t>(options.maxPixels, 1, static_cast<std::uint64_t>(AVIF_DEFAULT_IMAGE_SIZE_LIMIT)));
        decoder->imageDimensionLimit = options.maxDimension;

        const avifResult ioResult =
            avifDecoderSetIOMemory(decoder.get(), reinterpret_cast<const std::uint8_t *>(file.data()), file.size());
        return detail::checkedResult(ioResult, errorCodeForResult(ioResult), decoder->diag)
            .and_then([&]() -> core::Result<std::unique_ptr<core::IDecoder>> {
                const avifResult parseResult = avifDecoderParse(decoder.get());
                if (parseResult != AVIF_RESULT_OK) {
                    const std::string_view diagnostic{decoder->diag.error};
                    const core::ErrorCode code =
                        isSizeLimitDiagnostic(diagnostic) ? core::ErrorCode::TooLarge : errorCodeForResult(parseResult);
                    return std::unexpected(resultError(parseResult, code, decoder->diag));
                }

                return imageMeta(*decoder).transform([&](core::ImageMeta meta) {
                    return std::unique_ptr<core::IDecoder>{
                        std::make_unique<Decoder>(Decoder::Key{}, std::move(decoder), meta, maxThreads)};
                });
            });
    }

} // namespace pvdkit::avif
