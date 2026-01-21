#include <lightwave.hpp>

#include "fresnel.hpp"
#include "microfacet.hpp"

namespace lightwave {

struct DiffuseLobe {
    Color color;

    BsdfEval evaluate(const Vector &wo, const Vector &wi) const {
        // NOT_IMPLEMENTED

        return { color * Frame::absCosTheta(wi) * InvPi };

        // hints:
        // * copy your diffuse bsdf evaluate here
        // * you do not need to query a texture, the albedo is given by `color`
    }

    BsdfSample sample(const Vector &wo, Sampler &rng) const {
        // NOT_IMPLEMENTED

        // mainly copied from bsdf/diffuse.cpp 'sample'
        // vector v is the reflected vectore from the diffuse area
        Vector v = squareToCosineHemisphere(rng.next2D());
        return BsdfSample{ v.normalized(), color };

        // hints:
        // * copy your diffuse bsdf evaluate here
        // * you do not need to query a texture, the albedo is given by `color`
    }
};

struct MetallicLobe {
    float alpha;
    Color color;

    BsdfEval evaluate(const Vector &wo, const Vector &wi) const {
        // NOT_IMPLEMENTED

        // copied from roughconductor.cpp evaluate
        Color reflectance = color;

        Vector w_m  = (wi + wo) / (wi + wo).length(); // the halfvector
        float d_wm  = microfacet::evaluateGGX(alpha, w_m);
        float g1_wi = microfacet::smithG1(alpha, w_m, wi);
        float g1_wo = microfacet::smithG1(alpha, w_m, wo);

        float demoninator =
            1 / abs(4 * Frame::cosTheta(wo)); // why not the one below?
        // float demoninator =
        //     1 / abs(4 * Frame::cosTheta(wo) * Frame::cosTheta(wi));

        Color together = reflectance * d_wm * g1_wi * g1_wo * demoninator;

        return BsdfEval{ together };

        // hints:
        // * copy your roughconductor bsdf evaluate here
        // * you do not need to query textures
        //   * the reflectance is given by `color'
        //   * the variable `alpha' is already provided for you
    }

    BsdfSample sample(const Vector &wo, Sampler &rng) const {
        // NOT_IMPLEMENTED

        // copied from roughconductor.cpp sample
        // reflectance R
        Color reflectance = color;

        Vector normal = microfacet::sampleGGXVNDF(
            alpha, wo, rng.next2D()); // microfacet normal

        // calculate g1_wi
        Vector wi   = reflect(wo, normal);
        float g1_wi = microfacet::smithG1(alpha, normal, wi);

        // (the resulting sample weight is only a product of two factors)
        Color weight = reflectance * g1_wi; // final weight

        return BsdfSample{ .wi = wi, .weight = weight };

        // hints:
        // * copy your roughconductor bsdf sample here
        // * you do not need to query textures
        //   * the reflectance is given by `color'
        //   * the variable `alpha' is already provided for you
    }
};

class Principled : public Bsdf {
    ref<Texture> m_baseColor;
    ref<Texture> m_roughness;
    ref<Texture> m_metallic;
    ref<Texture> m_specular;

    struct Combination {
        float diffuseSelectionProb;
        DiffuseLobe diffuse;
        MetallicLobe metallic;
    };

    Combination combine(const Point2 &uv, const Vector &wo) const {
        const auto baseColor = m_baseColor->evaluate(uv);
        const auto alpha = std::max(float(1e-3), sqr(m_roughness->scalar(uv)));
        const auto specular = m_specular->scalar(uv);
        const auto metallic = m_metallic->scalar(uv);
        const auto F =
            specular * schlick((1 - metallic) * 0.08f, Frame::cosTheta(wo));

        const DiffuseLobe diffuseLobe = {
            .color = (1 - F) * (1 - metallic) * baseColor,
        };
        const MetallicLobe metallicLobe = {
            .alpha = alpha,
            .color = F * Color(1) + (1 - F) * metallic * baseColor,
        };

        const auto diffuseAlbedo = diffuseLobe.color.mean();
        const auto totalAlbedo =
            diffuseLobe.color.mean() + metallicLobe.color.mean();
        return {
            .diffuseSelectionProb =
                totalAlbedo > 0 ? diffuseAlbedo / totalAlbedo : 1.0f,
            .diffuse  = diffuseLobe,
            .metallic = metallicLobe,
        };
    }

public:
    Principled(const Properties &properties) {
        m_baseColor = properties.get<Texture>("baseColor");
        m_roughness = properties.get<Texture>("roughness");
        m_metallic  = properties.get<Texture>("metallic");
        m_specular  = properties.get<Texture>("specular");
    }

    BsdfEval evaluate(const Point2 &uv, const Vector &wo,
                      const Vector &wi) const override {
        PROFILE("Principled")

        const auto combination = combine(uv, wo);
        // NOT_IMPLEMENTED

        BsdfEval diffuse = combination.diffuse.evaluate(wo, wi);
        BsdfEval metal   = combination.metallic.evaluate(wo, wi);
        return BsdfEval{ diffuse.value + metal.value };

        // hint: evaluate `combination.diffuse` and `combination.metallic` and
        // combine their results
    }

    BsdfSample sample(const Point2 &uv, const Vector &wo,
                      Sampler &rng) const override {
        PROFILE("Principled")

        const auto combination = combine(uv, wo);
        // NOT_IMPLEMENTED

        float prob_diffuse = combination.diffuseSelectionProb;

        // decide which lobe to sample
        if (rng.next() < prob_diffuse) { // sample the diffuse lobe
            BsdfSample sampling = combination.diffuse.sample(wo, rng);
            if (prob_diffuse != 0) {
                sampling.weight = sampling.weight / prob_diffuse;
            } else {
                return BsdfSample::invalid();
            }
            return sampling;

        } else { // sample the metallic lobe
            BsdfSample sampling = combination.metallic.sample(wo, rng);
            if (1 - prob_diffuse != 0) {
                float prob_metallic = 1 - prob_diffuse;
                sampling.weight     = sampling.weight / prob_metallic;
            } else {
                return BsdfSample::invalid();
            }
            return sampling;
        }

        // hint: sample either `combination.diffuse` (probability
        // `combination.diffuseSelectionProb`) or `combination.metallic`
    }

    std::string toString() const override {
        return tfm::format(
            "Principled[\n"
            "  baseColor = %s,\n"
            "  roughness = %s,\n"
            "  metallic  = %s,\n"
            "  specular  = %s,\n"
            "]",
            indent(m_baseColor),
            indent(m_roughness),
            indent(m_metallic),
            indent(m_specular));
    }
};

} // namespace lightwave

REGISTER_BSDF(Principled, "principled")
