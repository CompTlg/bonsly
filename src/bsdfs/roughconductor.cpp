#include "fresnel.hpp"
#include "microfacet.hpp"
#include <lightwave.hpp>

namespace lightwave {

class RoughConductor : public Bsdf {
    ref<Texture> m_reflectance;
    ref<Texture> m_roughness;

public:
    RoughConductor(const Properties &properties) {
        m_reflectance = properties.get<Texture>("reflectance");
        m_roughness   = properties.get<Texture>("roughness");
    }

    BsdfEval evaluate(const Point2 &uv, const Vector &wo,
                      const Vector &wi) const override {
        // Using the squared roughness parameter results in a more gradual
        // transition from specular to rough. For numerical stability, we avoid
        // extremely specular distributions (alpha values below 10^-3)
        const auto alpha = std::max(float(1e-3), sqr(m_roughness->scalar(uv)));

        // NOT_IMPLEMENTED
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
        // hints:
        // * the microfacet normal can be computed from `wi' and `wo'
    }

    BsdfSample sample(const Point2 &uv, const Vector &wo,
                      Sampler &rng) const override {
        const auto alpha = std::max(float(1e-3), sqr(m_roughness->scalar(uv)));

        // NOT_IMPLEMENTED
        //

        // reflectance R
        Color reflectance = m_reflectance->evaluate(uv);

        Vector normal = microfacet::sampleGGXVNDF(
            alpha, wo, rng.next2D()); // microfacet normal

        // calculate g1_wi
        Vector wi = reflect(wo, normal); //

        // Feedback: ensure wi is in the same hemisphere as the surface normal
        if (!Frame::sameHemisphere(wo, wi)) {
            return BsdfSample::invalid();
        }

        float g1_wi = microfacet::smithG1(alpha, normal, wi);

        // (the resulting sample weight is only a product of two factors)
        Color weight = reflectance * g1_wi; // final weight

        return BsdfSample{ .wi = wi, .weight = weight };

        // hints:
        // * do not forget to cancel out as many terms from your equations as
        // possible!
        //   (the resulting sample weight is only a product of two factors)
    }

    std::string toString() const override {
        return tfm::format(
            "RoughConductor[\n"
            "  reflectance = %s,\n"
            "  roughness = %s\n"
            "]",
            indent(m_reflectance),
            indent(m_roughness));
    }
};

} // namespace lightwave

REGISTER_BSDF(RoughConductor, "roughconductor")
