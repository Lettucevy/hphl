#include <cstdio>
#include <vector>

struct Particle {
    double x, y, z;
    double vx, vy, vz;

    Particle(double x, double y, double z, double vx, double vy, double vz)
        : x(x), y(y), z(z), vx(vx), vy(vy), vz(vz) {}

    void Step() {
        vy -= 0.1;
        x += vx;
        y += vy;
        z += vz;

        if (x < -100.0) { x = -100.0; vx = 0.0 - (vx * 0.9); }
        if (x > 100.0)  { x = 100.0;  vx = 0.0 - (vx * 0.9); }
        if (y < -100.0) { y = -100.0; vy = 0.0 - (vy * 0.9); }
        if (y > 100.0)  { y = 100.0;  vy = 0.0 - (vy * 0.9); }
        if (z < -100.0) { z = -100.0; vz = 0.0 - (vz * 0.9); }
        if (z > 100.0)  { z = 100.0;  vz = 0.0 - (vz * 0.9); }
    }
};

int main() {
    int n = 100000;
    int steps = 100;
    std::vector<Particle*> parts;
    parts.reserve(n);
    long long seed = 12345;
    for (int i = 0; i < n; i++) {
        seed = (seed * 1103515245 + 12345) & 2147483647;
        double rx = (seed % 200 - 100) * 1.0;
        seed = (seed * 1103515245 + 12345) & 2147483647;
        double ry = (seed % 200 - 100) * 1.0;
        seed = (seed * 1103515245 + 12345) & 2147483647;
        double rz = (seed % 200 - 100) * 1.0;
        seed = (seed * 1103515245 + 12345) & 2147483647;
        double rvx = (seed % 20 - 10) * 0.1;
        seed = (seed * 1103515245 + 12345) & 2147483647;
        double rvy = (seed % 20 - 10) * 0.1;
        seed = (seed * 1103515245 + 12345) & 2147483647;
        double rvz = (seed % 20 - 10) * 0.1;

        parts.push_back(new Particle(rx, ry, rz, rvx, rvy, rvz));
    }

    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < n; i++) {
            parts[i]->Step();
        }
    }

    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        sum += parts[i]->x + parts[i]->y + parts[i]->z;
    }
    std::printf("Checksum: %g\n", sum);
    for (int i = 0; i < n; i++) {
        delete parts[i];
    }
    return 0;
}
