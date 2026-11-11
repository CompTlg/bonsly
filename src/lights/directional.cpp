#include <lightwave.hpp>

namespace lightwave
{

    class DirectionalLight final : public Light
    {
        Vector direction;
        Color power;

    public:
        DirectionalLight(const Properties &properties) : Light(properties)
        {
            direction = properties.get<Vector>("direction").normalized();
            power = properties.get<Color>("intensity");
        }

        PhotonSample samplePhoton(Sampler &rng, const Bounds &sceneBounds) const override
        {
            Point center = sceneBounds.center();
            float R = sceneBounds.diagonal().length() * 0.5f;
            auto d = squareToUniformDiskConcentric(rng.next2D());
            Frame f = Frame(direction);
            Point origin = center + R * (d.x() * f.tangent + d.y() * f.bitangent) + R * direction;
            // power by area of the disk by light
            // negative direction because its the direction of light
            return PhotonSample{origin, -direction, power * Pi * R * R};
        }

        DirectLightSample sampleDirect(const Point &origin,
                                       Sampler &rng) const override
        {
            return DirectLightSample{direction, power, Infinity};
        }

        bool canBeIntersected() const override { return false; }

        std::string toString() const override
        {
            return tfm::format(
                "DirectionalLight[\n"
                "]");
        }
    };

} // namespace lightwave

REGISTER_LIGHT(DirectionalLight, "directional")
