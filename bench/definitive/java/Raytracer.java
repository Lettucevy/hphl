public class Raytracer {
    static class Vec3 {
        double x, y, z;
        Vec3(double x, double y, double z) { this.x = x; this.y = y; this.z = z; }

        double dot(Vec3 o) { return x * o.x + y * o.y + z * o.z; }
        Vec3 add(Vec3 o) { return new Vec3(x + o.x, y + o.y, z + o.z); }
        Vec3 sub(Vec3 o) { return new Vec3(x - o.x, y - o.y, z - o.z); }
        Vec3 mul(double s) { return new Vec3(x * s, y * s, z * s); }
        Vec3 norm() {
            double l = Math.sqrt(dot(this));
            if (l <= 0.0) return this;
            return new Vec3(x / l, y / l, z / l);
        }
    }

    static class Sphere {
        Vec3 center;
        double radius;
        Sphere(Vec3 c, double r) { this.center = c; this.radius = r; }

        double hit(Vec3 ro, Vec3 rd) {
            Vec3 oc = ro.sub(center);
            double a = rd.dot(rd);
            double b = 2.0 * oc.dot(rd);
            double c = oc.dot(oc) - radius * radius;
            double disc = b * b - 4.0 * a * c;
            if (disc < 0.0) return -1.0;
            double sq = Math.sqrt(disc);
            double t = (-b - sq) / (2.0 * a);
            if (t > 0.001) return t;
            t = (-b + sq) / (2.0 * a);
            if (t > 0.001) return t;
            return -1.0;
        }
    }

    public static void main(String[] args) {
        int width = 800;
        int height = 800;

        Sphere s1 = new Sphere(new Vec3(0.0, 0.0, -5.0), 1.0);
        Sphere s2 = new Sphere(new Vec3(2.0, 0.5, -6.0), 1.2);
        Sphere s3 = new Sphere(new Vec3(-2.0, -0.5, -4.5), 0.8);

        Vec3 light = new Vec3(5.0, 10.0, -2.0).norm();
        Vec3 ro = new Vec3(0.0, 0.0, 0.0);

        double totalIntensity = 0.0;

        for (int y = 0; y < height; y++) {
            double vy = (y * 2.0 - height) / (height * 1.0);
            for (int x = 0; x < width; x++) {
                double vx = (x * 2.0 - width) / (width * 1.0);
                Vec3 rd = new Vec3(vx, -vy, -1.0).norm();

                double t1 = s1.hit(ro, rd);
                double t2 = s2.hit(ro, rd);
                double t3 = s3.hit(ro, rd);

                double minT = 999999.0;
                Sphere hitSphere = null;

                if (t1 > 0.0 && t1 < minT) { minT = t1; hitSphere = s1; }
                if (t2 > 0.0 && t2 < minT) { minT = t2; hitSphere = s2; }
                if (t3 > 0.0 && t3 < minT) { minT = t3; hitSphere = s3; }

                if (hitSphere != null) {
                    Vec3 hitPoint = ro.add(rd.mul(minT));
                    Vec3 normal = hitPoint.sub(hitSphere.center).norm();
                    double diff = normal.dot(light);
                    if (diff < 0.0) diff = 0.0;
                    totalIntensity += diff + 0.1;
                }
            }
        }

        System.out.printf("Checksum: %g%n", totalIntensity);
    }
}
