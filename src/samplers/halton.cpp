#include <lightwave.hpp>

#include "pcg32.h"
#include <functional>

namespace lightwave {

class Halton : public Sampler {
    uint64_t m_seed;
    pcg32 m_pcg;
    int m_base_prime;
    int sample_index;
    float offset;

public:
    Halton(const Properties &properties) : Sampler(properties) {
        m_seed =
            properties.get<int>("seed", std::getenv("reference") ? 1337 : 420);
        // default base to 2 if not specified
        m_base_prime = properties.get<int>("base", 2);
        sample_index = 0;
        offset       = 0.0f;
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
        m_base_prime = 2;
        sample_index = sampleIndex;
        m_pcg.seed(m_seed, sampleIndex);
    }

    void seed(const Point2i &pixel, int sampleIndex) override {
        // const uint64_t a =
        //     hash::fnv1a(pixel.x(), pixel.y(), sampleIndex, m_seed);

        const uint64_t a = (uint64_t(pixel.x()) << 32) ^ pixel.y();

        m_base_prime = 2;
        sample_index = sampleIndex;

        m_pcg.seed(m_seed, a);
        offset = m_pcg.nextFloat();
    }

    float next() override {
        float rev = radicalInv(sample_index, m_base_prime);
        m_base_prime++;

        rev = fmod(rev + offset, 1.0f);
        return rev;
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