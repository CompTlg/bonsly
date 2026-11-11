#include <lightwave.hpp>

#include "pcg32.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace lightwave {

class Halton : public Sampler {
    uint64_t m_seed;
    pcg32 m_pcg;
    int m_dimension; // which random number of the current sample comes next (selects the base)
    int sample_index;
    uint64_t m_pixelHash; // identifies the pixel: one random shift per dimension is derived from it
    bool m_shifted;       // false for seed(int): the plain, unshifted Halton sequence

    // Halton bases must be PRIME. Consecutive integers (2, 3, 4, 5, 6, ...) are not, e.g. base 4 is a
    // function of the digits of base 2, so those dimensions are strongly correlated.
    static constexpr int kPrimes[] = {
        2,   3,   5,   7,   11,  13,  17,  19,  23,  29,  31,  37,  41,  43,  47,  53,
        59,  61,  67,  71,  73,  79,  83,  89,  97,  101, 103, 107, 109, 113, 127, 131,
        137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223,
        227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311};
    static constexpr int kNumPrimes = sizeof(kPrimes) / sizeof(kPrimes[0]);

public:
    Halton(const Properties &properties) : Sampler(properties) {
        m_seed =
            properties.get<int>("seed", std::getenv("reference") ? 1337 : 420);
        m_dimension  = 0;
        sample_index = 0;
        m_pixelHash  = 0;
        m_shifted    = false;
    }

    // uniform number in [0,1) from a 64 bit value (murmur3 finalizer)
    static float toUnit(uint64_t h) {
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return float(h >> 40) * (1.0f / 16777216.0f);
    }

    // Cranley-Patterson shift of one dimension. Every dimension needs its OWN independent shift:
    // a single shared shift only slides the point set along the diagonal of the unit cube.
    float shift(int dimension) const {
        return toUnit(m_pixelHash * 0x9E3779B97F4A7C15ULL +
                      uint64_t(dimension + 1) * 0xD1B54A32D192ED03ULL + m_seed);
    }

    // radical inverse function phi_p
    double radicalInv(int a, int base) {
        double reversed = 0;
        double InvBase  = 1.0 / base;

        while (a > 0) {
            reversed += (a % base) * InvBase;
            a       = a / base; // shift right by 1 bit
            InvBase = InvBase / base;
        }

        return reversed;
    }

    void seed(int sampleIndex) override {
        m_dimension  = 0;
        sample_index = sampleIndex;
        m_shifted    = false;
        m_pcg.seed(m_seed, sampleIndex);
    }

    void seed(const Point2i &pixel, int sampleIndex) override {
        // const uint64_t a =
        //     hash::fnv1a(pixel.x(), pixel.y(), sampleIndex, m_seed);

        const uint64_t a = (uint64_t(pixel.x()) << 32) ^ pixel.y();

        m_dimension  = 0;
        sample_index = sampleIndex;

        m_pixelHash = a;
        m_shifted   = true;
        m_pcg.seed(m_seed, a);
    }

    float next() override {
        // beyond the table (very long paths) fall back to plain random numbers
        if (m_dimension >= kNumPrimes)
            return m_pcg.nextFloat();
        const int dimension = m_dimension++;
        float rev           = float(radicalInv(sample_index, kPrimes[dimension]));
        if (m_shifted)
            rev = std::fmod(rev + shift(dimension), 1.0f);
        // converting the double radical inverse to float can round up to exactly 1.0 (index 2^25 - 1
        // gives 1 - 2^-25), but samplers must return numbers in [0,1)
        return std::min(rev, std::nextafter(1.0f, 0.0f));
    } // rng

    ref<Sampler> clone() const override {
        return std::make_shared<Halton>(*this);
    }

    std::string toString() const override {
        return tfm::format(
            "halton[\n"
            "  count = %d\n"
            "]",
            m_samplesPerPixel);
    }
};
} // namespace lightwave

REGISTER_SAMPLER(Halton, "halton")