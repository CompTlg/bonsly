#include "fresnel.hpp"
#include <lightwave.hpp>

namespace lightwave {

class Dielectric : public Bsdf {
    ref<Texture> m_ior;
    ref<Texture> m_reflectance;
    ref<Texture> m_transmittance;

public:
    Dielectric(const Properties &properties) {
        m_ior           = properties.get<Texture>("ior");
        m_reflectance   = properties.get<Texture>("reflectance");
        m_transmittance = properties.get<Texture>("transmittance");
    }

    BsdfEval evaluate(const Point2 &uv, const Vector &wo,
                      const Vector &wi) const override {
        // the probability of a light sample picking exactly the direction `wi'
        // that results from reflecting or refracting `wo' is zero, hence we can
        // just ignore that case and always return black
        return BsdfEval::invalid();
    }

    BsdfSample sample(const Point2 &uv, const Vector &wo,
                      Sampler &rng) const override {
        // NOT_IMPLEMENTED
        float ior     = m_ior->scalar(uv);
        Vector normal = Vector(0, 0, 1);

        if (Frame::cosTheta(wo) < 0) {
            if (ior != 0) {
                ior    = 1 / ior;
                normal = -normal;
            }
        }

        // fresnel coefficients
        float F = fresnelDielectric(wo.z(), ior);

        // reflect or refract
        if (rng.next() < F) {
            // reflect wo around the normal outputs the direction wi
            Vector wi = reflect(wo, normal);
            return BsdfSample{ wi, m_reflectance->evaluate(uv) };
        }

        // refract wo around the normal
        Vector wi = refract(wo, normal, ior);

        // Feedback of checking if a zero vector was returned
        if (wi.isZero()) {
            // if refraction fails, fallback to reflection
            wi = reflect(wo, normal);
            return BsdfSample{ wi, m_reflectance->evaluate(uv) };
        }

        auto refracter = m_transmittance->evaluate(uv) / (ior * ior);
        // upper thing makes the code run faster on my pc
        return BsdfSample{ wi, refracter };
    }

    std::string toString() const override {
        return tfm::format(
            "Dielectric[\n"
            "  ior           = %s,\n"
            "  reflectance   = %s,\n"
            "  transmittance = %s\n"
            "]",
            indent(m_ior),
            indent(m_reflectance),
            indent(m_transmittance));
    }
};

} // namespace lightwave

REGISTER_BSDF(Dielectric, "dielectric")
