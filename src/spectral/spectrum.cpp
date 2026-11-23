#include <spectral/spectrum.hpp>
#include <lightwave.hpp>
#include <spectral/tools/cie_tables.hpp>

namespace lightwave
{
    // The bodies below throw (instead of using NOT_IMPLEMENTED, which aborts the whole process) so that
    // the unit tests report a failure with a message while the functions are not written yet.

    static const float CIESRGB[3][3] = {{3.2404542f, -1.5371385f, -0.4985314f}, {-0.9692660f, 1.8760108f, 0.0415560f}, {0.0556434f, -0.2040259f, 1.0572252f}};
    static const Matrix3x3 CIESRGBMatrix = []
    {
        Matrix3x3 matrix;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                matrix(i, j) = CIESRGB[i][j];
        return matrix;
    }();

    Color cie1931(float lambdaNm)
    {
        // lightwave_throw("cie1931 is not implemented yet");
        if (lambdaNm < LambdaMin || lambdaNm > LambdaMax)
            return Color(0.f);
        const int index = std::min(static_cast<int>(lambdaNm) - 360, 469); // min so when it is at the last index we dont get table outta reach for index + 1
        const float fraction = lambdaNm - static_cast<float>(index + 360);
        Color a = Color(CieXyz[index][0], CieXyz[index][1], CieXyz[index][2]);
        Color b = Color(CieXyz[index + 1][0], CieXyz[index + 1][1], CieXyz[index + 1][2]);
        return lerp(a, b, fraction);
    }

    float illuminantD65(float lambdaNm)
    {
        // lightwave_throw("illuminantD65 is not implemented yet");
        if (lambdaNm < LambdaMin || lambdaNm > LambdaMax)
            return 0.f;
        const int index = std::min(static_cast<int>(lambdaNm) - 360, 469); // min so when it is at the last index we dont get table outta reach for index + 1
        const float fraction = lambdaNm - static_cast<float>(index + 360);
        const float a = D65[index];
        const float b = D65[index + 1];
        return a + fraction * (b - a);
    }

    Color xyzToLinearSRGB(const Color &xyz)
    {
        Color srgb;
        for (int i = 0; i < 3; i++)
        {
            srgb[i] = 0.f;
            for (int j = 0; j < 3; j++)
                srgb[i] += CIESRGBMatrix(i, j) * xyz[j];
        }
        return srgb;
    }
}
