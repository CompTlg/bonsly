#include <catch_amalgamated.hpp>

#include <lightwave/color.hpp>
#include <spectral/spectrum.hpp>
#include <spectral/uplifting.hpp>

#include "golden_data.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

using namespace lightwave;

// clang-format off

/*
 * Tests for spectral upsampling: turning an RGB color into a spectrum that looks the same.
 *
 *   build/yoldas -d yes "[spectral]"                   all of them
 *   build/yoldas -d yes "[spectral][tables]"           only the CIE tables and the XYZ -> sRGB conversion
 *   build/yoldas -d yes "[spectral][albedo]"           only reflectance spectra
 *
 * (the first argument has to start with a dash, otherwise it is taken for a scene file)
 *
 * The thresholds were measured with Mitsuba 3's implementation of the same method (Jakob & Hanika 2019)
 * and are about 2-5x looser than what it achieves. Many spectra give the same color, so the spectrum
 * itself is NOT compared against Mitsuba's (see the informational test at the end): what counts is that
 * the color comes back, that the spectrum is valid, and that it changes smoothly with the color.
 * The reference data in golden_data.hpp comes from unittests/spectral/tools/oracle.py.
 */

namespace {

using Vec3 = std::array<double, 3>;

// Independent copy of the standard conversion (IEC 61966-2-1), so that the round trip tests below do not
// depend on your xyzToLinearSRGB being right (it is tested on its own).
constexpr double XyzToRgb[3][3] = {
    {  3.2404542, -1.5371385, -0.4985314 },
    { -0.9692660,  1.8760108,  0.0415560 },
    {  0.0556434, -0.2040259,  1.0572252 },
};

Vec3 xyzToRgb(const Vec3 &xyz) {
    Vec3 rgb { 0, 0, 0 };
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++)
            rgb[row] += XyzToRgb[row][col] * xyz[col];
    return rgb;
}

Color toColor(const Vec3 &v) { return Color(float(v[0]), float(v[1]), float(v[2])); }

double maxAbsDiff(const Vec3 &a, const Vec3 &b) {
    double result = 0;
    for (int i = 0; i < 3; i++) result = std::max(result, std::abs(a[i] - b[i]));
    return result;
}

std::string str(const Vec3 &v) {
    return "(" + std::to_string(v[0]) + ", " + std::to_string(v[1]) + ", " + std::to_string(v[2]) + ")";
}

constexpr int FirstNm = 360;
constexpr int LastNm  = 830;

/**
 * Integrates a spectrum against your colour matching functions on a 1 nm grid and converts the result
 * to linear sRGB, normalised so that the D65 illuminant itself has Y = 1.
 * @param litByD65 true for a reflectance (it is lit by D65 first), false for an emission spectrum
 *        (which already contains the illuminant)
 */
template <typename Func>
Vec3 integrateToRgb(const Func &spectrum, bool litByD65) {
    Vec3 xyz { 0, 0, 0 };
    double whiteY = 0;
    for (int nm = FirstNm; nm <= LastNm; nm++) {
        const float lambda = float(nm);
        const Color cmf    = cie1931(lambda);
        const double d65   = illuminantD65(lambda);
        const double value = double(spectrum(lambda)) * (litByD65 ? d65 : 1.0);
        for (int i = 0; i < 3; i++) xyz[i] += value * cmf[i];
        whiteY += d65 * cmf[1];
    }
    for (int i = 0; i < 3; i++) xyz[i] /= whiteY;
    return xyzToRgb(xyz);
}

Vec3 albedoRoundTrip(const Vec3 &rgb) {
    const AlbedoSpectrum spectrum(toColor(rgb));
    return integrateToRgb([&](float l) { return spectrum(l); }, true);
}

Vec3 illuminantRoundTrip(const Vec3 &rgb) {
    const IlluminantSpectrum spectrum(toColor(rgb));
    return integrateToRgb([&](float l) { return spectrum(l); }, false);
}

// small deterministic random numbers in (0,1)
struct Rng {
    std::mt19937 gen;
    explicit Rng(unsigned seed) : gen(seed) {}
    double next() { return (double(gen()) + 0.5) / 4294967296.0; }
};

std::vector<Vec3> gridColors() {
    const double values[5] = { 0, 0.25, 0.5, 0.75, 1 };
    std::vector<Vec3> colors;
    for (double r : values)
        for (double g : values)
            for (double b : values)
                colors.push_back({ r, g, b });
    return colors;
}

std::vector<Vec3> randomColors(int count, unsigned seed) {
    Rng rng(seed);
    std::vector<Vec3> colors;
    for (int i = 0; i < count; i++)
        colors.push_back({ rng.next(), rng.next(), rng.next() });
    return colors;
}

// colors at or very near the border of the sRGB gamut, where every method is least accurate
std::vector<Vec3> edgeColors() {
    return {
        { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 1, 0 }, { 0, 1, 1 }, { 1, 0, 1 },
        { 0.99, 0.01, 0.01 }, { 0.01, 0.01, 0.99 }, { 0.001, 0.001, 0.001 }, { 0.999, 0.999, 0.999 },
        { 0.8, 0.1, 0.1 }, { 0.1, 0.8, 0.1 },
    };
}

struct RoundTrip {
    double maxError = 0, meanError = 0;
    Vec3 worst { 0, 0, 0 };
};

RoundTrip measureAlbedoRoundTrip(const std::vector<Vec3> &colors) {
    RoundTrip result;
    for (const Vec3 &rgb : colors) {
        const double error = maxAbsDiff(albedoRoundTrip(rgb), rgb);
        result.meanError += error / double(colors.size());
        if (error > result.maxError) {
            result.maxError = error;
            result.worst    = rgb;
        }
    }
    return result;
}

} // namespace

TEST_CASE( "spectral interface is implemented", "[spectral][implemented]" ) {
    // fails with a message until the corresponding function exists
    REQUIRE_NOTHROW( cie1931(550.f) );
    REQUIRE_NOTHROW( illuminantD65(550.f) );
    REQUIRE_NOTHROW( xyzToLinearSRGB(Color(0.5f)) );
    REQUIRE_NOTHROW( AlbedoSpectrum(Color(0.5f))(550.f) );
    REQUIRE_NOTHROW( IlluminantSpectrum(Color(0.5f))(550.f) );
}

TEST_CASE( "CIE tables match the reference data", "[spectral][tables]" ) {
    SECTION( "colour matching functions" ) {
        // measured at the reference wavelengths, including half-integer ones that test the interpolation
        double worstError = 0, worstLambda = 0;
        for (int i = 0; i < golden::NumLambdas; i++) {
            const Color c = cie1931(golden::Lambdas[i]);
            for (int k = 0; k < 3; k++) {
                const double error = std::abs(double(c[k]) - double(golden::Cmf[i][k]));
                if (error > worstError) { worstError = error; worstLambda = golden::Lambdas[i]; }
            }
        }
        INFO( "largest difference " << worstError << " at " << worstLambda << " nm (check that value in your table, "
              "and that you interpolate linearly between the 1 nm samples)" );
        REQUIRE( worstError <= 1e-3 );
    }
    SECTION( "outside of the visible range" ) {
        for (float lambda : { LambdaMin - 10.f, LambdaMin - 0.5f, LambdaMax + 0.5f, LambdaMax + 10.f }) {
            const Color c = cie1931(lambda);
            INFO( "wavelength " << lambda );
            REQUIRE( c[0] == 0.f );
            REQUIRE( c[1] == 0.f );
            REQUIRE( c[2] == 0.f );
        }
    }
    SECTION( "D65 illuminant (relative to 560 nm)" ) {
        const double reference = illuminantD65(560.f);
        REQUIRE( reference > 0 );
        double worstError = 0, worstLambda = 0;
        for (int i = 0; i < golden::NumLambdas; i++) {
            const double ratio = double(illuminantD65(golden::Lambdas[i])) / reference;
            const double error = std::abs(ratio - double(golden::D65Ratio[i])) / double(golden::D65Ratio[i]);
            if (error > worstError) { worstError = error; worstLambda = golden::Lambdas[i]; }
        }
        INFO( "largest relative difference " << worstError << " at " << worstLambda << " nm" );
        REQUIRE( worstError <= 2e-3 );
    }
}

TEST_CASE( "XYZ to linear sRGB conversion", "[spectral][tables]" ) {
    const std::vector<Vec3> samples = {
        { 0.95047, 1.0, 1.08883 }, // the D65 white point must map to (1,1,1)
        { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0.2, 0.3, 0.4 }, { 0.41246, 0.21267, 0.01933 },
    };
    for (const Vec3 &xyz : samples) {
        const Vec3 expected = xyzToRgb(xyz);
        const Color actual  = xyzToLinearSRGB(toColor(xyz));
        INFO( "xyz = " << str(xyz) << ", expected " << str(expected) );
        REQUIRE( maxAbsDiff({ actual[0], actual[1], actual[2] }, expected) <= 1e-4 );
    }
    const Color white = xyzToLinearSRGB(Color(0.95047f, 1.f, 1.08883f));
    REQUIRE( std::abs(white[0] - 1.f) <= 1e-3f );
    REQUIRE( std::abs(white[1] - 1.f) <= 1e-3f );
    REQUIRE( std::abs(white[2] - 1.f) <= 1e-3f );
}

TEST_CASE( "white point", "[spectral][tables]" ) {
    // a perfectly white reflectance under D65 has to come out white: this tests your CMFs, your D65 table
    // and the normalisation, independent of any upsampling
    const Vec3 rgb = integrateToRgb([](float) { return 1.f; }, true);
    INFO( "constant reflectance 1 under D65 gives " << str(rgb) << ", expected (1, 1, 1)" );
    REQUIRE( maxAbsDiff(rgb, { 1, 1, 1 }) <= 5e-4 );
}

TEST_CASE( "albedo spectra reproduce their color", "[spectral][albedo]" ) {
    SECTION( "grid and random colors" ) {
        std::vector<Vec3> colors = gridColors();
        for (const Vec3 &c : randomColors(300, 7)) colors.push_back(c);
        const RoundTrip result = measureAlbedoRoundTrip(colors);
        INFO( "max error " << result.maxError << " at rgb " << str(result.worst) << ", mean error " << result.meanError );
        REQUIRE( result.maxError  <= 2e-3 );
        REQUIRE( result.meanError <= 5e-4 );
    }
    SECTION( "colors at the border of the gamut" ) {
        const RoundTrip result = measureAlbedoRoundTrip(edgeColors());
        INFO( "max error " << result.maxError << " at rgb " << str(result.worst) );
        REQUIRE( result.maxError <= 6e-3 );
    }
}

TEST_CASE( "albedo spectra are valid", "[spectral][albedo]" ) {
    std::vector<Vec3> colors = gridColors();
    for (const Vec3 &c : randomColors(200, 11)) colors.push_back(c);
    for (const Vec3 &c : edgeColors()) colors.push_back(c);

    SECTION( "finite and between 0 and 1" ) {
        for (const Vec3 &rgb : colors) {
            const AlbedoSpectrum spectrum(toColor(rgb));
            for (int nm = FirstNm; nm <= LastNm; nm += 5) {
                const float value = spectrum(float(nm));
                INFO( "rgb " << str(rgb) << " at " << nm << " nm gives " << value );
                REQUIRE( std::isfinite(value) );
                REQUIRE( value >= -1e-3f );
                REQUIRE( value <= 1.f + 1e-3f );
            }
        }
    }
    SECTION( "grey colors give a constant spectrum" ) {
        for (double grey : { 0.0, 0.1, 0.5, 0.9, 1.0 }) {
            const AlbedoSpectrum spectrum(toColor({ grey, grey, grey }));
            for (int nm = FirstNm; nm <= LastNm; nm += 5) {
                INFO( "grey " << grey << " at " << nm << " nm gives " << spectrum(float(nm)) );
                REQUIRE( std::abs(double(spectrum(float(nm))) - grey) <= 1e-3 );
            }
        }
    }
}

TEST_CASE( "albedo spectra change continuously with the color", "[spectral][albedo]" ) {
    // Walk along a line through color space and record the largest change of the spectrum between two
    // neighbouring points. With 4x smaller steps a continuous function changes about 4x less (ratio 0.25,
    // Mitsuba: 0.25-0.26); a jump, e.g. where you switch from one table cell to the next in the wrong way,
    // does not shrink at all (ratio about 1).
    const auto largestStep = [](const Vec3 &a, const Vec3 &b, int steps) {
        double largest = 0;
        std::array<float, golden::NumAlbedoLambdas> previous {};
        for (int i = 0; i <= steps; i++) {
            const double t = double(i) / steps;
            const AlbedoSpectrum spectrum(toColor({
                a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t }));
            for (int k = 0; k < golden::NumAlbedoLambdas; k++) {
                const float value = spectrum(golden::AlbedoLambdas[k]);
                if (i > 0) largest = std::max(largest, std::abs(double(value) - double(previous[k])));
                previous[k] = value;
            }
        }
        return largest;
    };

    Rng rng(21);
    const auto point = [&]() { return Vec3 { 0.03 + 0.94 * rng.next(), 0.03 + 0.94 * rng.next(), 0.03 + 0.94 * rng.next() }; };
    for (int line = 0; line < 25; line++) {
        const Vec3 a = point(), b = point();
        const double coarse = largestStep(a, b, 300);
        const double fine   = largestStep(a, b, 1200);
        INFO( "line from " << str(a) << " to " << str(b) << ": largest step " << coarse << " (300 steps), "
              << fine << " (1200 steps)" );
        if (coarse < 1e-6) continue; // nothing to measure (e.g. a line inside the greys)
        REQUIRE( fine / coarse <= 0.5 );
    }
}

TEST_CASE( "illuminant spectra reproduce their color", "[spectral][illuminant]" ) {
    SECTION( "white is the D65 illuminant" ) {
        const IlluminantSpectrum spectrum(Color(1.f));
        for (int nm = FirstNm; nm <= LastNm; nm += 10) {
            const double ratio = double(spectrum(float(nm))) / double(illuminantD65(float(nm)));
            INFO( "spectrum / D65 at " << nm << " nm = " << ratio );
            REQUIRE( std::abs(ratio - 1.0) <= 1e-3 );
        }
    }
    SECTION( "round trip, also for values far above 1" ) {
        const std::vector<Vec3> colors = {
            { 5, 2, 1 }, { 0.02, 0.5, 0.1 }, { 100, 100, 100 }, { 3, 0.1, 0.1 }, { 1, 1, 1 },
            { 0.3, 0.3, 0.9 }, { 40, 25, 5 },
        };
        for (const Vec3 &rgb : colors) {
            const Vec3 result = illuminantRoundTrip(rgb);
            const double scale = std::max({ rgb[0], rgb[1], rgb[2] });
            INFO( "rgb " << str(rgb) << " came back as " << str(result) );
            REQUIRE( maxAbsDiff(result, rgb) / scale <= 2e-3 );
        }
    }
    SECTION( "scaling the color scales the spectrum" ) {
        const IlluminantSpectrum base(Color(0.5f, 0.2f, 0.1f)), doubled(Color(1.0f, 0.4f, 0.2f));
        for (int nm = FirstNm; nm <= LastNm; nm += 10) {
            const double expected = 2.0 * double(base(float(nm)));
            INFO( "at " << nm << " nm: " << doubled(float(nm)) << " instead of " << expected );
            REQUIRE( std::abs(double(doubled(float(nm))) - expected) <= 1e-3 * std::max(expected, 1e-3) );
        }
    }
    SECTION( "black" ) {
        const IlluminantSpectrum illuminant(Color(0.f));
        const AlbedoSpectrum albedo(Color(0.f));
        for (int nm = FirstNm; nm <= LastNm; nm += 10) {
            REQUIRE( std::isfinite(illuminant(float(nm))) );
            REQUIRE( std::abs(illuminant(float(nm))) <= 1e-6f );
            REQUIRE( std::isfinite(albedo(float(nm))) );
            REQUIRE( std::abs(albedo(float(nm))) <= 1e-3f );
        }
    }
}

TEST_CASE( "comparison with Mitsuba's spectra (informational)", "[spectral][oracle]" ) {
    // Not a pass/fail test: spectra that differ can still be equally good, because infinitely many spectra
    // have the same color. If you use the same method and a similar table you should be within a few
    // percent; a large difference is only a hint to double check the round trip tests above.
    double worst = 0, sum = 0;
    int worstColor = 0;
    for (int i = 0; i < golden::NumColors; i++) {
        const AlbedoSpectrum spectrum(Color(golden::Colors[i][0], golden::Colors[i][1], golden::Colors[i][2]));
        for (int k = 0; k < golden::NumAlbedoLambdas; k++) {
            const double difference = std::abs(double(spectrum(golden::AlbedoLambdas[k])) - double(golden::Albedo[i][k]));
            sum += difference / double(golden::NumColors * golden::NumAlbedoLambdas);
            if (difference > worst) { worst = difference; worstColor = i; }
        }
    }
    WARN( "difference to Mitsuba's spectra over " << golden::NumColors << " colors: largest " << worst
          << " (rgb " << golden::Colors[worstColor][0] << ", " << golden::Colors[worstColor][1] << ", "
          << golden::Colors[worstColor][2] << "), mean " << sum );
    SUCCEED();
}
