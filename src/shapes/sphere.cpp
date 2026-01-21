#include <lightwave.hpp>

namespace lightwave {
class Sphere : public Shape {

    inline void populate(SurfaceEvent &surf, const Point &position) const {
        surf.position = position;

        // Equirectangular Projection (r, theta, phi)
        // https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-sphere-intersection.html

        // float theta = acos(position.z() /
        //     sqrt(position.x() * position.x() + position.y() * position.y() +
        //     position.z() * position.z()));
        float theta = acos(position.y());
        float phi   = atan2(-position.z(), position.x());

        // surf.uv.x() = 0.5 + phi * Inv2Pi;
        surf.uv.x() = (phi - Pi) * Inv2Pi;
        // upper was 2*pi, same thing but changed it still
        surf.uv.y() = theta * InvPi;

        surf.shadingNormal  = Vector(position).normalized();
        surf.geometryNormal = surf.shadingNormal;

        // surf.tangent = Vector{1, 0, 0};
        Vector x_axis = { 1.0f, 0.0f, 0.0f };
        if (surf.shadingNormal == x_axis) {
            surf.tangent = Vector{ 0.0f, 0.0f, 1.0f };
        } else {
            // surf.tangent = surf.shadingNormal.cross(x_axis).normalized();
            surf.tangent = Vector{ 1, 0, 0 };
        }
        surf.tangent = Vector{ 1, 0, 0 };

        surf.pdf = 1.0f / (4.0f * M_PI);
    }

public:
    Sphere(const Properties &properties) {}

    bool intersect(const Ray &ray, Intersection &its,
                   Sampler &rng) const override {
        // NOT_IMPLEMENTED

        Vector co = Vector(ray.origin); // vector of (origin - center)
        float a   = ray.direction.dot(ray.direction); // a = D^2 = 1 (usually)
        float b   = 2 * ray.direction.dot(co);        // b = 2D.(O - C)
        float c   = co.dot(co) - 1;                   // c = | O - C |^2 - R^2
        float D   = sqr(b) - 4 * a * c;               // D = b^2 - 4ac

        if (D > 0) { // two roots
            float intersect_1 =
                (-b - sqrt(D)) / 2.0f *
                a; // the distance of ray.origin to first intersection
            float intersect_2 =
                (-b + sqrt(D)) / 2.0f * a; // intersect_1 < intersect_2

            if (intersect_2 < 0)
                return false; // if both intersections are behind the camera

            if (intersect_1 >= Epsilon && intersect_1 < its.t) {
                its.t                = intersect_1;
                const Point position = ray(intersect_1);
                populate(its, position);
                return true;

            } else if (intersect_2 >= Epsilon &&
                       intersect_2 < its.t) { // its.t initialized as infinity
                its.t                = intersect_2;
                const Point position = ray(intersect_2);
                populate(its, position);
                return true;

            } else
                return false;

        } else if (!D) { // one root
            float intersect = -b / (2 * a);
            if (intersect < Epsilon || intersect > its.t)
                return false;
            else {
                // compute the hitpoint
                const Point position = ray(intersect);
                its.t                = intersect;
                populate(its, position);
                return true;
            }

        } else { // no solution
            return false;
        }

        return false;
    }

    Bounds getBoundingBox() const override {
        return Bounds(Point{ -1, -1, -1 }, Point{ +1, +1, +1 });
    }

    Point getCentroid() const override { return Point(0); }

    AreaSample sampleArea(Sampler &rng) const override {
        // NOT_IMPLEMENTED // leave it for later

        Point2 next = rng.next2D();

        Point position = squareToUniformSphere(next);

        AreaSample sample;
        populate(sample, position);
        return sample;
    }

    std::string toString() const override { return "Sphere[]"; }
};
} // namespace lightwave
REGISTER_SHAPE(Sphere, "sphere")
