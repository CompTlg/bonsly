#include <lightwave.hpp>

namespace lightwave
{
    class Photonmapping : public SamplingIntegrator
    {
    public:
        Photonmapping(const Properties &properties) : SamplingIntegrator(properties)
        {
            int m_photonCount = properties.get<int>("photonCount", 100000);
        }
        Color Li(const Ray &ray, Sampler &rng) override
        {
            // NOT_IMPLEMENTED
            return Color::black();
        }
    };
} // namespace lightwave
REGISTER_INTEGRATOR(Photonmapping, "photonmapping")