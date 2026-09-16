#include "core/colour/Primaries.hpp"

#include <cmath>
#include <cstddef>

namespace pvdkit::core::colour::Primaries
{
    namespace
    {

        // The H.273 Table 2 chromaticities themselves live in the header (Primaries::detail), so
        // that Primaries::chromaticities is constexpr; the matrices below are derived from them.
        using detail::kBt2020;
        using detail::kBt470Bg;
        using detail::kBt470M;
        using detail::kBt709;
        using detail::kD65;
        using detail::kEbu3213;
        using detail::kP3D65;
        using detail::kP3Dci;
        using detail::kSmpteC;
        // Illuminant E, the equal-energy white of the CIE XYZ and CIE RGB encodings.
        constexpr Chromaticity kEqualEnergy{1.0F / 3.0F, 1.0F / 3.0F};
        constexpr float kWhiteTolerance = 1.0e-3F;

        constexpr Matrix3 kIdentity{{{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}}};
        constexpr Matrix3 kBradford{
            {{{0.8951F, 0.2664F, -0.1614F}, {-0.7502F, 1.7135F, 0.0367F}, {0.0389F, -0.0685F, 1.0296F}}}};

        // One derivation, evaluated at compile time for the coded sets (the constants below) and
        // at run time for the explicit chromaticities a container hands over.
        constexpr float determinant(const Matrix3 &matrix) noexcept
        {
            const auto &m = matrix.rows;
            return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                   m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                   m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        }

        constexpr Matrix3 inverse(const Matrix3 &matrix) noexcept
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

        constexpr Matrix3 multiply(const Matrix3 &left, const Matrix3 &right) noexcept
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

        constexpr Rgb xyz(const Chromaticity chromaticity) noexcept
        {
            return {chromaticity.x / chromaticity.y, 1.0F, (1.0F - chromaticity.x - chromaticity.y) / chromaticity.y};
        }

        // The coded sets' derivation, evaluated at compile time into the constants below (consteval:
        // it exists at run time as those constants only): every primary is taken at Y = 1
        // (x/y, 1, (1-x-y)/y) and the columns are scaled to the white. It is kept to the bit - the
        // tables it produces are pinned by every plugin's output hashes - which is why the run-time
        // path below has a derivation of its own.
        consteval Matrix3 rgbToXyz(const Chromaticities &definition) noexcept
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

        // The two inverses that do not depend on the source are constants of the derivation, so
        // that the run-time path (explicit chromaticities) evaluates exactly what the compile-time
        // path does: left inline, the optimizer folds them at compile time as fused multiply-adds
        // while an unoptimised build computes them unfused, and the two matrices then differ by a
        // few ULPs (observed as one-code differences between Debug and Release EXR output).
        constexpr Matrix3 kBradfordInverse = inverse(kBradford);
        constexpr Matrix3 kXyzToBt709 = inverse(rgbToXyz(kBt709));
        constexpr Rgb kD65Cone = multiply(kBradford, xyz(kD65));

        // The run-time derivation for explicit chromaticities (Imf::RGBtoXYZ's column form): the
        // columns are the primaries' (x, y, 1 - x - y), scaled so that RGB (1, 1, 1) lands on the
        // white's XYZ at Y = 1. Only the white's y and the primaries' determinant divide, so a
        // primary at y = 0 (CIE XYZ: red (1, 0), blue (0, 0)) is as valid as any other.
        constexpr Matrix3 rgbToXyzColumns(const Chromaticities &definition) noexcept
        {
            const Matrix3 columns{
                {{{definition.red.x, definition.green.x, definition.blue.x},
                  {definition.red.y, definition.green.y, definition.blue.y},
                  {1.0F - definition.red.x - definition.red.y, 1.0F - definition.green.x - definition.green.y,
                   1.0F - definition.blue.x - definition.blue.y}}}};
            const auto scales = multiply(inverse(columns), xyz(definition.white));
            Matrix3 result{};
            for (std::size_t row = 0; row < 3; ++row) {
                result.rows[row][0] = columns.rows[row][0] * scales[0];
                result.rows[row][1] = columns.rows[row][1] * scales[1];
                result.rows[row][2] = columns.rows[row][2] * scales[2];
            }
            return result;
        }

        constexpr bool near(const Chromaticity a, const Chromaticity b, const float tolerance) noexcept
        {
            const auto dx = a.x - b.x;
            const auto dy = a.y - b.y;
            return dx * dx <= tolerance * tolerance && dy * dy <= tolerance * tolerance;
        }

        constexpr Matrix3 adaptation(const Chromaticity source) noexcept
        {
            if (source.x == kD65.x && source.y == kD65.y) {
                return kIdentity;
            }
            // Illuminant E declares no viewing white: the values are absolute tristimulus
            // (Primaries.hpp, toSrgb) and are not adapted.
            if (near(source, kEqualEnergy, kWhiteTolerance)) {
                return kIdentity;
            }
            const auto sourceCone = multiply(kBradford, xyz(source));
            Matrix3 scale{};
            scale.rows[0][0] = kD65Cone[0] / sourceCone[0];
            scale.rows[1][1] = kD65Cone[1] / sourceCone[1];
            scale.rows[2][2] = kD65Cone[2] / sourceCone[2];
            return multiply(multiply(kBradfordInverse, scale), kBradford);
        }

        consteval Matrix3 conversion(const Chromaticities &source) noexcept
        {
            return multiply(multiply(kXyzToBt709, adaptation(source.white)), rgbToXyz(source));
        }

        constexpr Matrix3 conversionColumns(const Chromaticities &source) noexcept
        {
            return multiply(multiply(kXyzToBt709, adaptation(source.white)), rgbToXyzColumns(source));
        }

        bool isFinite(const Matrix3 &matrix) noexcept
        {
            for (const auto &row : matrix.rows) {
                for (const auto value : row) {
                    if (!std::isfinite(value)) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool isFinite(const Chromaticity chromaticity) noexcept
        {
            return std::isfinite(chromaticity.x) && std::isfinite(chromaticity.y);
        }

        constexpr Matrix3 kBt470MToSrgb = conversion(kBt470M);
        constexpr Matrix3 kBt470BgToSrgb = conversion(kBt470Bg);
        constexpr Matrix3 kSmpteCToSrgb = conversion(kSmpteC);
        constexpr Matrix3 kBt2020ToSrgb = conversion(kBt2020);
        constexpr Matrix3 kP3DciToSrgb = conversion(kP3Dci);
        constexpr Matrix3 kP3D65ToSrgb = conversion(kP3D65);
        constexpr Matrix3 kEbu3213ToSrgb = conversion(kEbu3213);

        consteval Rgb luminance(const Chromaticities &definition) noexcept
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

    Matrix3 toSrgb(const Chromaticities &chromaticities) noexcept
    {
        return conversionColumns(chromaticities);
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

    Rgb luminanceCoefficients(const Chromaticities &chromaticities) noexcept
    {
        return rgbToXyzColumns(chromaticities).rows[1];
    }

    bool isUsable(const Chromaticities &chromaticities) noexcept
    {
        if (!isFinite(chromaticities.red) || !isFinite(chromaticities.green) || !isFinite(chromaticities.blue) ||
            !isFinite(chromaticities.white) || !(chromaticities.white.y > 0.0F)) {
            return false;
        }
        // The determinant of the primaries' columns (their (x, y, 1 - x - y)) is zero exactly when
        // they are collinear: the same y for all three, or all on one line through the diagram.
        const auto &set = chromaticities;
        const auto determinantOfColumns = set.red.x * (set.green.y - set.blue.y) +
                                          set.green.x * (set.blue.y - set.red.y) +
                                          set.blue.x * (set.red.y - set.green.y);
        if (determinantOfColumns == 0.0F || !std::isfinite(determinantOfColumns)) {
            return false;
        }
        // The product includes RGB -> XYZ (an infinity in it cannot cancel: the XYZ -> BT.709 factor
        // has no zero entry), whose Y row is the luminance coefficients.
        return isFinite(conversionColumns(set));
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
