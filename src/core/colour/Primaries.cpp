#include "core/colour/Primaries.hpp"

#include <cstddef>

namespace pvdkit::core::colour::Primaries
{
    namespace
    {

        struct Chromaticity
        {
            float x;
            float y;
        };

        struct Definition
        {
            Chromaticity red;
            Chromaticity green;
            Chromaticity blue;
            Chromaticity white;
        };

        constexpr Chromaticity kD65{0.3127F, 0.3290F};
        constexpr Chromaticity kIlluminantC{0.3100F, 0.3160F};
        constexpr Chromaticity kDciWhite{0.3140F, 0.3510F};

        // H.273 Table 2 chromaticities. These were cross-checked against zimg's
        // colorspace_param.h; matrices below are derived from them, never pasted.
        constexpr Definition kBt709{{0.640F, 0.330F}, {0.300F, 0.600F}, {0.150F, 0.060F}, kD65};
        constexpr Definition kBt470M{{0.670F, 0.330F}, {0.210F, 0.710F}, {0.140F, 0.080F}, kIlluminantC};
        constexpr Definition kBt470Bg{{0.640F, 0.330F}, {0.290F, 0.600F}, {0.150F, 0.060F}, kD65};
        constexpr Definition kSmpteC{{0.630F, 0.340F}, {0.310F, 0.595F}, {0.155F, 0.070F}, kD65};
        constexpr Definition kBt2020{{0.708F, 0.292F}, {0.170F, 0.797F}, {0.131F, 0.046F}, kD65};
        constexpr Definition kP3Dci{{0.680F, 0.320F}, {0.265F, 0.690F}, {0.150F, 0.060F}, kDciWhite};
        constexpr Definition kP3D65{{0.680F, 0.320F}, {0.265F, 0.690F}, {0.150F, 0.060F}, kD65};
        constexpr Definition kEbu3213{{0.630F, 0.340F}, {0.295F, 0.605F}, {0.155F, 0.077F}, kD65};

        constexpr Matrix3 kIdentity{{{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}}};
        constexpr Matrix3 kBradford{
            {{{0.8951F, 0.2664F, -0.1614F}, {-0.7502F, 1.7135F, 0.0367F}, {0.0389F, -0.0685F, 1.0296F}}}};

        consteval float determinant(const Matrix3 &matrix) noexcept
        {
            const auto &m = matrix.rows;
            return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                   m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                   m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        }

        consteval Matrix3 inverse(const Matrix3 &matrix) noexcept
        {
            const auto &m = matrix.rows;
            const auto det = determinant(matrix);
            return Matrix3{
                {{{(m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det, (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det,
                   (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det},
                  {(m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det,
                   (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det},
                  {(m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det, (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det,
                   (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det}}}};
        }

        consteval Matrix3 multiply(const Matrix3 &left, const Matrix3 &right) noexcept
        {
            Matrix3 result{};
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t column = 0; column < 3; ++column) {
                    for (std::size_t item = 0; item < 3; ++item) {
                        result.rows[row][column] += left.rows[row][item] * right.rows[item][column];
                    }
                }
            }
            return result;
        }

        constexpr Rgb multiply(const Matrix3 &matrix, const Rgb &vector) noexcept
        {
            Rgb result{};
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t column = 0; column < 3; ++column) {
                    result[row] += matrix.rows[row][column] * vector[column];
                }
            }
            return result;
        }

        consteval Rgb xyz(const Chromaticity chromaticity) noexcept
        {
            return {chromaticity.x / chromaticity.y, 1.0F, (1.0F - chromaticity.x - chromaticity.y) / chromaticity.y};
        }

        consteval Matrix3 rgbToXyz(const Definition &definition) noexcept
        {
            const auto red = xyz(definition.red);
            const auto green = xyz(definition.green);
            const auto blue = xyz(definition.blue);
            const Matrix3 unscaled{
                {{{red[0], green[0], blue[0]}, {red[1], green[1], blue[1]}, {red[2], green[2], blue[2]}}}};
            const auto scales = multiply(inverse(unscaled), xyz(definition.white));
            Matrix3 result{};
            for (std::size_t row = 0; row < 3; ++row) {
                result.rows[row][0] = unscaled.rows[row][0] * scales[0];
                result.rows[row][1] = unscaled.rows[row][1] * scales[1];
                result.rows[row][2] = unscaled.rows[row][2] * scales[2];
            }
            return result;
        }

        consteval Matrix3 adaptation(const Chromaticity source, const Chromaticity target) noexcept
        {
            if (source.x == target.x && source.y == target.y) {
                return kIdentity;
            }
            const auto sourceCone = multiply(kBradford, xyz(source));
            const auto targetCone = multiply(kBradford, xyz(target));
            Matrix3 scale{};
            scale.rows[0][0] = targetCone[0] / sourceCone[0];
            scale.rows[1][1] = targetCone[1] / sourceCone[1];
            scale.rows[2][2] = targetCone[2] / sourceCone[2];
            return multiply(multiply(inverse(kBradford), scale), kBradford);
        }

        consteval Matrix3 conversion(const Definition &source) noexcept
        {
            return multiply(multiply(inverse(rgbToXyz(kBt709)), adaptation(source.white, kD65)), rgbToXyz(source));
        }

        constexpr Matrix3 kBt470MToSrgb = conversion(kBt470M);
        constexpr Matrix3 kBt470BgToSrgb = conversion(kBt470Bg);
        constexpr Matrix3 kSmpteCToSrgb = conversion(kSmpteC);
        constexpr Matrix3 kBt2020ToSrgb = conversion(kBt2020);
        constexpr Matrix3 kP3DciToSrgb = conversion(kP3Dci);
        constexpr Matrix3 kP3D65ToSrgb = conversion(kP3D65);
        constexpr Matrix3 kEbu3213ToSrgb = conversion(kEbu3213);

        consteval Rgb luminance(const Definition &definition) noexcept
        {
            return rgbToXyz(definition).rows[1];
        }

        constexpr Rgb kBt709Luminance = luminance(kBt709);
        constexpr Rgb kBt470MLuminance = luminance(kBt470M);
        constexpr Rgb kBt470BgLuminance = luminance(kBt470Bg);
        constexpr Rgb kSmpteCLuminance = luminance(kSmpteC);
        constexpr Rgb kBt2020Luminance = luminance(kBt2020);
        constexpr Rgb kP3DciLuminance = luminance(kP3Dci);
        constexpr Rgb kP3D65Luminance = luminance(kP3D65);
        constexpr Rgb kEbu3213Luminance = luminance(kEbu3213);

    } // namespace

    Matrix3 toSrgb(const std::uint16_t primaries) noexcept
    {
        switch (primaries) {
        case 4:
            return kBt470MToSrgb;
        case 5:
            return kBt470BgToSrgb;
        case 6:
        case 7:
            return kSmpteCToSrgb;
        case 9:
            return kBt2020ToSrgb;
        case 11:
            return kP3DciToSrgb;
        case 12:
            return kP3D65ToSrgb;
        case 22:
            return kEbu3213ToSrgb;
        case 1:
        case 2:
        default:
            return kIdentity;
        }
    }

    Rgb apply(const Matrix3 &matrix, const Rgb &colour) noexcept
    {
        return multiply(matrix, colour);
    }

    Rgb luminanceCoefficients(const std::uint16_t primaries) noexcept
    {
        switch (primaries) {
        case 4:
            return kBt470MLuminance;
        case 5:
            return kBt470BgLuminance;
        case 6:
        case 7:
            return kSmpteCLuminance;
        case 9:
            return kBt2020Luminance;
        case 11:
            return kP3DciLuminance;
        case 12:
            return kP3D65Luminance;
        case 22:
            return kEbu3213Luminance;
        case 1:
        case 2:
        default:
            return kBt709Luminance;
        }
    }

    bool isKnown(const std::uint16_t primaries) noexcept
    {
        switch (primaries) {
        case 1:
        case 2:
        case 4:
        case 5:
        case 6:
        case 7:
        case 9:
        case 11:
        case 12:
        case 22:
            return true;
        default:
            return false;
        }
    }

    bool isIdentity(const std::uint16_t primaries) noexcept
    {
        return primaries == 1 || primaries == 2;
    }

} // namespace pvdkit::core::colour::Primaries
