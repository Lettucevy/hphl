public class PhysicsParticles {
    static class Particle {
        double x, y, z;
        double vx, vy, vz;

        Particle(double x, double y, double z, double vx, double vy, double vz) {
            this.x = x; this.y = y; this.z = z;
            this.vx = vx; this.vy = vy; this.vz = vz;
        }

        void step() {
            vy -= 0.1;
            x += vx;
            y += vy;
            z += vz;

            if (x < -100.0) { x = -100.0; vx = -(vx * 0.9); }
            if (x > 100.0)  { x = 100.0;  vx = -(vx * 0.9); }
            if (y < -100.0) { y = -100.0; vy = -(vy * 0.9); }
            if (y > 100.0)  { y = 100.0;  vy = -(vy * 0.9); }
            if (z < -100.0) { z = -100.0; vz = -(vz * 0.9); }
            if (z > 100.0)  { z = 100.0;  vz = -(vz * 0.9); }
        }
    }

    public static void main(String[] args) {
        int n = 100000;
        int steps = 100;
        Particle[] parts = new Particle[n];
        int seed = 12345;

        for (int i = 0; i < n; i++) {
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
            double rx = (seed % 200 - 100) * 1.0;
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
            double ry = (seed % 200 - 100) * 1.0;
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
            double rz = (seed % 200 - 100) * 1.0;
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
            double rvx = (seed % 20 - 10) * 0.1;
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
            double rvy = (seed % 20 - 10) * 0.1;
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
            double rvz = (seed % 20 - 10) * 0.1;

            parts[i] = new Particle(rx, ry, rz, rvx, rvy, rvz);
        }

        for (int s = 0; s < steps; s++) {
            for (int i = 0; i < n; i++) {
                parts[i].step();
            }
        }

        double sum = 0.0;
        for (int i = 0; i < n; i++) {
            sum += parts[i].x + parts[i].y + parts[i].z;
        }

        System.out.printf("Checksum: %g%n", sum);
    }
}
