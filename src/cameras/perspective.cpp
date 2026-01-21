#include <lightwave.hpp>

namespace lightwave {

/**
 * @brief A perspective camera with a given field of view angle and transform.
 *
 * In local coordinates (before applying m_transform), the camera looks in
 * positive z direction [0,0,1]. Pixels on the left side of the image ( @code
 * normalized.x < 0 @endcode ) are directed in negative x direction ( @code
 * ray.direction.x < 0 ), and pixels at the bottom of the image ( @code
 * normalized.y < 0 @endcode ) are directed in negative y direction ( @code
 * ray.direction.y < 0 ).
 */
class Perspective : public Camera {

    // pre-computed values
    float scale_x;
    float scale_y;

public:
    Perspective(const Properties &properties) : Camera(properties) {

        const float fov =
            properties.get<float>("fov"); // Get the value of FOV (in degree)
        const std::string fov_axis = properties.get<std::string>(
            "fovAxis", "x"); // set to x-axis if not specified

        // convert degrees to radians: deg * (M_PI / 180.0f)
        const float tan_fov = std::tan(fov * 0.5f * (M_PI / 180.0f));
        scale_x = tan_fov; // pre-compute scale vector, scale_x based on FOV

        const float width        = m_resolution.x();
        const float height       = m_resolution.y();
        const float aspect_ratio = width / height;

        // calculate x and y for direction Vector
        if (fov_axis == "y") {
            scale_x = tan_fov * aspect_ratio;
            scale_y = tan_fov;
        } else { // (default)
            scale_x = tan_fov;
            scale_y = tan_fov / aspect_ratio;
        }

        // hints:
        // * precompute any expensive operations here (most importantly
        // trigonometric functions)
        // * use m_resolution to find the aspect ratio of the image
    }

    CameraSample sample(const Point2 &normalized, Sampler &rng) const override {

        CameraSample result;

        Vector direction =
            Vector(normalized.x() * scale_x, normalized.y() * scale_y, 1.0f);

        // in local camera coordinate, the origin of ray is always (0,0,0)
        result.ray.origin = { 0.0f, 0.0f, 0.0f }; // -> camera.hpp -> math.hpp
                                                  // for struct Ray

        // Compute ray direction in local coordinates
        // Scale normalized coordinates by FOV factors
        const float scaled_x = normalized.x() * scale_x;
        const float scaled_y = normalized.y() * scale_y;

        // // Create direction vector pointing to scaled point on z=1 plane
        // // Create direction vector (needs to be normalized)
        // float length = std::sqrt(scaled_x * scaled_x + scaled_y * scaled_y
        // + 1.0f); result.ray.direction ={
        //     scaled_x / length, scaled_y / length, 1.0f / length
        // };

        // Changed it so that we dont calculate the length and get the direction
        // We just get the ray direction, and normalize the ray at the end
        result.ray.direction = { scaled_x, scaled_y, 1.0f };

        // Transform ray to world coordinates
        result.ray = m_transform->apply(result.ray).normalized();

        // // Set sample weight to 1
        result.weight = Color(1.0f);

        return result;

        // hints:
        // * use m_transform to transform the local camera coordinate system
        // into the world coordinate system
    }

    std::string toString() const override {
        return tfm::format(
            "Perspective[\n"
            "  width = %d,\n"
            "  height = %d,\n"
            "  transform = %s,\n"
            "]",
            m_resolution.x(),
            m_resolution.y(),
            indent(m_transform));
    }
};

} // namespace lightwave

REGISTER_CAMERA(Perspective, "perspective")
