#include "fresnel.hpp"
#include "microfacet.hpp"
#include <lightwave.hpp>

namespace lightwave {

class RoughDielectric : public Bsdf {

    ref<Texture> m_ior;
    ref<Texture> m_reflectance;
    ref<Texture> m_transmittance;
    ref<Texture> m_roughness;

public:
    RoughDielectric(const Properties &properties) {
        m_ior           = properties.get<Texture>("ior");
        m_reflectance   = properties.get<Texture>("reflectance");
        m_transmittance = properties.get<Texture>("transmittance");
        m_roughness     = properties.get<Texture>("roughness");
    }

    BsdfEval evaluate(const Point2 &uv, const Vector &wo,
                      const Vector &wi) const override {

        // everything from rough conductor
        const auto alpha = std::max(float(1e-3), sqr(m_roughness->scalar(uv)));

        Color reflectance = m_reflectance->evaluate(uv);

        Vector w_m  = (wi + wo) / (wi + wo).length(); // the halfvector
        float d_wm  = microfacet::evaluateGGX(alpha, w_m);
        float g1_wi = microfacet::smithG1(alpha, w_m, wi);
        float g1_wo = microfacet::smithG1(alpha, w_m, wo);

        float denominator =
            1 / abs(4 * Frame::cosTheta(wo)); // why not the one below?
        // float demoninator =
        //     1 / abs(4 * Frame::cosTheta(wo) * Frame::cosTheta(wi));

        Color together = reflectance * d_wm * g1_wi * g1_wo * denominator;

        return BsdfEval{ together };
    }

    BsdfSample sample(const Point2 &uv, const Vector &wo,
                      Sampler &rng) const override {

        float ior = m_ior->scalar(uv);
        // Vector normal = Vector(0, 0, 1);

        const auto alpha = std::max(float(1e-3), sqr(m_roughness->scalar(uv)));
        Vector normal    = microfacet::sampleGGXVNDF(alpha, wo, rng.next2D());

        // from dielectric
        if (Frame::cosTheta(wo) < 0) {
            if (ior != 0) {
                ior    = 1 / ior;
                normal = -normal;
            }
        }
        // fresnel coefficients
        // float F = fresnelDielectric(normal.dot(wo), ior);
        float F = fresnelDielectric(wo.z(), ior);

        // reflect
        if (rng.next() < F) {
            Vector wi = reflect(wo, normal);
            return BsdfSample{ wi, m_reflectance->evaluate(uv) };
        }

        // refractence aka(?) transmittance
        // refract wo around the normal
        Vector wi = refract(wo, normal, ior);

        // checking if a zero vector was returned
        if (wi.isZero()) {
            // if refraction fails, fallback to reflection
            wi = reflect(wo, normal);
            return BsdfSample{ wi, m_reflectance->evaluate(uv) };
        }

        float g1     = microfacet::smithG1(alpha, normal, wi);
        Color weight = (m_transmittance->evaluate(uv) / (ior * ior)) * g1;

        return BsdfSample{ wi, weight };
    }

    std::string toString() const override {
        return tfm::format(
            "RoughDielectric[\n"
            "  ior           = %s,\n"
            "  reflectance   = %s,\n"
            "  transmittance = %s\n"
            "  roughness = %s\n"
            "]",
            indent(m_reflectance),
            indent(m_roughness));
    }
};

} // namespace lightwave

REGISTER_BSDF(RoughDielectric, "roughdielectric")