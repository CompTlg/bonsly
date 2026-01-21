#include <lightwave.hpp>

namespace lightwave {

class Thinlens : public Camera {

    float scale_x;
    float scale_y;
    float m_radius;
    float focalDistance;

public:
    Thinlens(const Properties &properties) : Camera(properties) {

        float fov                  = properties.get<float>("fov"); // degree
        const std::string fov_axis = properties.get<std::string>(
            "fovAxis", "x"); // set to x-axis if not specified
        m_radius      = properties.get<float>("radius");
        focalDistance = properties.get<float>("focalDistance");

        fov                 = fov * M_PI / 180.0f; // convert degrees to radians
        const float tan_fov = std::tan(fov * 0.5f);

        const float width        = m_resolution.x();
        const float height       = m_resolution.y();
        const float aspect_ratio = width / height;

        if (fov_axis == "x") {
            scale_x = tan_fov;
            scale_y = tan_fov / aspect_ratio;
        } else if (fov_axis == "y") {
            scale_x = tan_fov * aspect_ratio;
            scale_y = tan_fov;
        }
    }

    CameraSample sample(const Point2 &normalized, Sampler &rng) const override {

        // generate a unit circle (or disk)
        const Point2 sampleUnitDisk =
            squareToUniformDiskConcentric(rng.next2D());
        Point2 lensPoint = { m_radius * sampleUnitDisk.x(),
                             m_radius * sampleUnitDisk.y() };

        // calculate point on image plane
        Point2 imagePoint = { normalized.x() * scale_x,
                              normalized.y() * scale_y };

        // point on focal plane
        Point focusPoint(imagePoint.x() * focalDistance / 1.0f,
                         imagePoint.y() * focalDistance / 1.0f,
                         focalDistance);

        // create ray from lens point through focus point
        Vector direction = Vector(focusPoint.x() - lensPoint.x(),
                                  focusPoint.y() - lensPoint.y(),
                                  focalDistance);
        Point lensOnDisk = { lensPoint.x(), lensPoint.y(), 0 };
        Ray ray          = { lensOnDisk, direction.normalized() };

        ray = m_transform->apply(ray).normalized();

        return CameraSample{ .ray = ray, .weight = Color(1.0f) };
    }

    std::string toString() const override {
        return tfm::format(
            "Thinlens[\n]"
            " width = %d,\n"
            " height = %d,\n"
            " trransform = %s,\n"
            "]",
            m_resolution.x(),
            m_resolution.y(),
            indent(m_transform));
    }
};

} // namespace lightwave

REGISTER_CAMERA(Thinlens, "thinlens")
