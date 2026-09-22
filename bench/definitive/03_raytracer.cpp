#include <cstdio>
#include <cmath>

struct Vec3 {
    double x, y, z;

    Vec3(double x, double y, double z) : x(x), y(y), z(z) {}

    double Dot(const Vec3& o) const {
        return x * o.x + y * o.y + z * o.z;
    }

    Vec3 Add(const Vec3& o) const {
        return Vec3(x + o.x, y + o.y, z + o.z);
    }

    Vec3 Sub(const Vec3& o) const {
        return Vec3(x - o.x, y - o.y, z - o.z);
    }

    Vec3 Mul(double s) const {
        return Vec3(x * s, y * s, z * s);
    }

    Vec3 Norm() const {
        double d = Dot(*this);
        double l = std::sqrt(d);
        if (l <= 0.0) return *this;
        return Vec3(x / l, y / l, z / l);
    }
};

struct Sphere {
    Vec3 center;
    double radius;

    Sphere(const Vec3& c, double r) : center(c), radius(r) {}

    double Hit(const Vec3& ro, const Vec3& rd) const {
        Vec3 oc = ro.Sub(center);
        double a = rd.Dot(rd);
        double b = 2.0 * oc.Dot(rd);
        double c = oc.Dot(oc) - radius * radius;
        double disc = b * b - 4.0 * a * c;
        if (disc < 0.0) return -1.0;
        double sq = std::sqrt(disc);
        double t = (0.0 - b - sq) / (2.0 * a);
        if (t > 0.001) return t;
        t = (0.0 - b + sq) / (2.0 * a);
        if (t > 0.001) return t;
        return -1.0;
    }
};

int main() {
    int width = 800;
    int height = 800;

    Sphere s1(Vec3(0.0, 0.0, -5.0), 1.0);
    Sphere s2(Vec3(2.0, 0.5, -6.0), 1.2);
    Sphere s3(Vec3(-2.0, -0.5, -4.5), 0.8);

    Vec3 light = Vec3(5.0, 10.0, -2.0).Norm();
    Vec3 ro(0.0, 0.0, 0.0);

    double totalIntensity = 0.0;

    for (int y = 0; y < height; y++) {
        double vy = (y * 2.0 - height) / (height * 1.0);
        for (int x = 0; x < width; x++) {
            double vx = (x * 2.0 - width) / (width * 1.0);
            Vec3 rd = Vec3(vx, 0.0 - vy, -1.0).Norm();

            double t1 = s1.Hit(ro, rd);
            double t2 = s2.Hit(ro, rd);
            double t3 = s3.Hit(ro, rd);

            double minT = 999999.0;
            const Sphere* hitSphere = nullptr;

            if (t1 > 0.0 && t1 < minT) { minT = t1; hitSphere = &s1; }
            if (t2 > 0.0 && t2 < minT) { minT = t2; hitSphere = &s2; }
            if (t3 > 0.0 && t3 < minT) { minT = t3; hitSphere = &s3; }

            if (hitSphere != nullptr) {
                Vec3 hitPoint = ro.Add(rd.Mul(minT));
                Vec3 normal = hitPoint.Sub(hitSphere->center).Norm();
                double diff = normal.Dot(light);
                if (diff < 0.0) diff = 0.0;
                totalIntensity += diff + 0.1;
            }
        }
    }

    std::printf("Checksum: %g\n", totalIntensity);
    return 0;
}
