#include <lightwave.hpp>

namespace lightwave
{

    class AreaLight final : public Light
    {
        ref<Instance> m_instance;

    public:
        AreaLight(const Properties &properties) : Light(properties)
        {
            m_instance = properties.getChild<Instance>();
            m_instance->setLight(this);
        }

        PhotonSample samplePhoton(Sampler &rng, const Bounds &sceneBounds) const override
        {
            AreaSample sample = m_instance->sampleArea(rng);
            if (sample.pdf == 0)
            {
                return PhotonSample::invalid();
            }
            Vector local = squareToCosineHemisphere(rng.next2D()); // direction in the lamps local frame
            Color Le = m_instance->emission()->evaluate(sample.uv, local).value;
            Vector wi = sample.shadingFrame().toWorld(local); // direction in world frame
            Color power = Le * Pi / sample.pdf;
            // Le*cos / pdfpos * pdfdir -> pdfpos = s.pdf, pdfdir = cos/Pi -> cosines cancel

            return PhotonSample{sample.position, wi.normalized(), power};
        }

        DirectLightSample sampleDirect(const Point &origin,
                                       Sampler &rng) const override
        {
            AreaSample sample = m_instance->sampleArea(rng);
            if (sample.pdf == 0)
            {
                return DirectLightSample::invalid();
            }
            Vector to_Light = sample.position - origin;
            float distance2 = to_Light.lengthSquared();
            float distance = sqrt(distance2);
            Vector wi = to_Light / distance;

            Vector wo = sample.shadingFrame().toLocal(-wi);
            float cosTheta = Frame::cosTheta(wo);
            if (cosTheta <= 0)
            {
                return DirectLightSample::invalid();
            }
            Color radiance = m_instance->emission()->evaluate(sample.uv, wo).value;
            Color weight = radiance * cosTheta / (sample.pdf * distance2);

            return DirectLightSample{to_Light.normalized(), weight, distance};
        }

        bool canBeIntersected() const override
        {
            return false;
        }

        std::string toString() const override
        {
            return tfm::format(
                "AreaLight[\n"
                "]");
        }
    };

} // namespace lightwave

REGISTER_LIGHT(AreaLight, "area")