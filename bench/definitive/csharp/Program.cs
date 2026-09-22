using System;

namespace DefinitiveBenchmark
{
    class Program
    {
        static void Main(string[] args)
        {
            if (args.Length == 0)
            {
                Console.WriteLine("Usage: Benchmark <01_physics|02_matrix|03_raytracer|04_binary_trees|05_mandelbrot>");
                return;
            }

            switch (args[0].ToLower())
            {
                case "01_physics":
                case "01_physics_particles":
                    RunPhysics();
                    break;
                case "02_matrix":
                case "02_matrix_transforms":
                    RunMatrix();
                    break;
                case "03_raytracer":
                    RunRaytracer();
                    break;
                case "04_binary_trees":
                    RunBinaryTrees();
                    break;
                case "05_mandelbrot":
                    RunMandelbrot();
                    break;
                default:
                    Console.WriteLine("Unknown benchmark: " + args[0]);
                    break;
            }
        }

        // ==========================================
        // 01_physics_particles
        // ==========================================
        class Particle
        {
            public double x, y, z;
            public double vx, vy, vz;

            public Particle(double x, double y, double z, double vx, double vy, double vz)
            {
                this.x = x; this.y = y; this.z = z;
                this.vx = vx; this.vy = vy; this.vz = vz;
            }

            public void Step()
            {
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

        static void RunPhysics()
        {
            int n = 100000;
            int steps = 100;
            Particle[] parts = new Particle[n];
            int seed = 12345;

            for (int i = 0; i < n; i++)
            {
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

            for (int s = 0; s < steps; s++)
            {
                for (int i = 0; i < n; i++)
                {
                    parts[i].Step();
                }
            }

            double sum = 0.0;
            for (int i = 0; i < n; i++)
            {
                sum += parts[i].x + parts[i].y + parts[i].z;
            }

            Console.WriteLine($"Checksum: {sum:G}");
        }

        // ==========================================
        // 02_matrix_transforms
        // ==========================================
        struct Mat4
        {
            public double m00, m01, m02, m03;
            public double m10, m11, m12, m13;
            public double m20, m21, m22, m23;
            public double m30, m31, m32, m33;

            public static Mat4 Identity()
            {
                Mat4 m = default;
                m.m00 = 1.0; m.m11 = 1.0; m.m22 = 1.0; m.m33 = 1.0;
                return m;
            }

            public static Mat4 Translation(double tx, double ty, double tz)
            {
                Mat4 m = Identity();
                m.m03 = tx;
                m.m13 = ty;
                m.m23 = tz;
                return m;
            }

            public static Mat4 RotationZ(double rad)
            {
                Mat4 m = Identity();
                double c = Math.Cos(rad);
                double s = Math.Sin(rad);
                m.m00 = c;
                m.m01 = -s;
                m.m10 = s;
                m.m11 = c;
                return m;
            }

            public Mat4 Mul(in Mat4 o)
            {
                Mat4 r = default;
                r.m00 = m00*o.m00 + m01*o.m10 + m02*o.m20 + m03*o.m30;
                r.m01 = m00*o.m01 + m01*o.m11 + m02*o.m21 + m03*o.m31;
                r.m02 = m00*o.m02 + m01*o.m12 + m02*o.m22 + m03*o.m32;
                r.m03 = m00*o.m03 + m01*o.m13 + m02*o.m23 + m03*o.m33;

                r.m10 = m10*o.m00 + m11*o.m10 + m12*o.m20 + m13*o.m30;
                r.m11 = m10*o.m01 + m11*o.m11 + m12*o.m21 + m13*o.m31;
                r.m12 = m10*o.m02 + m11*o.m12 + m12*o.m22 + m13*o.m32;
                r.m13 = m10*o.m03 + m11*o.m13 + m12*o.m23 + m13*o.m33;

                r.m20 = m20*o.m00 + m21*o.m10 + m22*o.m20 + m23*o.m30;
                r.m21 = m20*o.m01 + m21*o.m11 + m22*o.m21 + m23*o.m31;
                r.m22 = m20*o.m02 + m21*o.m12 + m22*o.m22 + m23*o.m32;
                r.m23 = m20*o.m03 + m21*o.m13 + m22*o.m23 + m23*o.m33;

                r.m30 = m30*o.m00 + m31*o.m10 + m32*o.m20 + m33*o.m30;
                r.m31 = m30*o.m01 + m31*o.m11 + m32*o.m21 + m33*o.m31;
                r.m32 = m30*o.m02 + m31*o.m12 + m32*o.m22 + m33*o.m32;
                r.m33 = m30*o.m03 + m31*o.m13 + m32*o.m23 + m33*o.m33;
                return r;
            }
        }

        static void RunMatrix()
        {
            int iters = 2000000;
            double sumX = 0.0;
            double sumY = 0.0;
            double sumZ = 0.0;

            Mat4 t = Mat4.Translation(10.0, 20.0, 30.0);
            Mat4 r = Mat4.RotationZ(0.7853981633974483);
            Mat4 combined = t.Mul(r);

            double vx = 1.0;
            double vy = 2.0;
            double vz = 3.0;

            for (int i = 0; i < iters; i++)
            {
                double nx = combined.m00*vx + combined.m01*vy + combined.m02*vz + combined.m03;
                double ny = combined.m10*vx + combined.m11*vy + combined.m12*vz + combined.m13;
                double nz = combined.m20*vx + combined.m21*vy + combined.m22*vz + combined.m23;

                sumX += nx;
                sumY += ny;
                sumZ += nz;

                vx = nx * 0.0001 + 1.0;
                vy = ny * 0.0001 + 2.0;
                vz = nz * 0.0001 + 3.0;
            }

            double total = sumX + sumY + sumZ;
            Console.WriteLine($"Checksum: {total:G}");
        }

        // ==========================================
        // 03_raytracer
        // ==========================================
        struct Vec3
        {
            public double x, y, z;
            public Vec3(double x, double y, double z) { this.x = x; this.y = y; this.z = z; }
            public double Dot(in Vec3 o) => x * o.x + y * o.y + z * o.z;
            public Vec3 Add(in Vec3 o) => new Vec3(x + o.x, y + o.y, z + o.z);
            public Vec3 Sub(in Vec3 o) => new Vec3(x - o.x, y - o.y, z - o.z);
            public Vec3 Mul(double s) => new Vec3(x * s, y * s, z * s);
            public Vec3 Norm()
            {
                double l = Math.Sqrt(Dot(this));
                if (l <= 0.0) return this;
                return new Vec3(x / l, y / l, z / l);
            }
        }

        struct Sphere
        {
            public Vec3 center;
            public double radius;
            public Sphere(Vec3 c, double r) { this.center = c; this.radius = r; }

            public double Hit(in Vec3 ro, in Vec3 rd)
            {
                Vec3 oc = ro.Sub(center);
                double a = rd.Dot(rd);
                double b = 2.0 * oc.Dot(rd);
                double c = oc.Dot(oc) - radius * radius;
                double disc = b * b - 4.0 * a * c;
                if (disc < 0.0) return -1.0;
                double sq = Math.Sqrt(disc);
                double t = (-b - sq) / (2.0 * a);
                if (t > 0.001) return t;
                t = (-b + sq) / (2.0 * a);
                if (t > 0.001) return t;
                return -1.0;
            }
        }

        static void RunRaytracer()
        {
            int width = 800;
            int height = 800;

            Sphere s1 = new Sphere(new Vec3(0.0, 0.0, -5.0), 1.0);
            Sphere s2 = new Sphere(new Vec3(2.0, 0.5, -6.0), 1.2);
            Sphere s3 = new Sphere(new Vec3(-2.0, -0.5, -4.5), 0.8);

            Vec3 light = new Vec3(5.0, 10.0, -2.0).Norm();
            Vec3 ro = new Vec3(0.0, 0.0, 0.0);

            double totalIntensity = 0.0;

            for (int y = 0; y < height; y++)
            {
                double vy = (y * 2.0 - height) / (height * 1.0);
                for (int x = 0; x < width; x++)
                {
                    double vx = (x * 2.0 - width) / (width * 1.0);
                    Vec3 rd = new Vec3(vx, -vy, -1.0).Norm();

                    double t1 = s1.Hit(ro, rd);
                    double t2 = s2.Hit(ro, rd);
                    double t3 = s3.Hit(ro, rd);

                    double minT = 999999.0;
                    int hitIdx = 0;

                    if (t1 > 0.0 && t1 < minT) { minT = t1; hitIdx = 1; }
                    if (t2 > 0.0 && t2 < minT) { minT = t2; hitIdx = 2; }
                    if (t3 > 0.0 && t3 < minT) { minT = t3; hitIdx = 3; }

                    if (hitIdx > 0)
                    {
                        Vec3 center = hitIdx == 1 ? s1.center : (hitIdx == 2 ? s2.center : s3.center);
                        Vec3 hitPoint = ro.Add(rd.Mul(minT));
                        Vec3 normal = hitPoint.Sub(center).Norm();
                        double diff = normal.Dot(light);
                        if (diff < 0.0) diff = 0.0;
                        totalIntensity += diff + 0.1;
                    }
                }
            }

            Console.WriteLine($"Checksum: {totalIntensity:G}");
        }

        // ==========================================
        // 04_binary_trees
        // ==========================================
        class TreeNode
        {
            public int item;
            public TreeNode left, right;
            public TreeNode(int i) { this.item = i; }
            public TreeNode(int i, TreeNode l, TreeNode r) { this.item = i; this.left = l; this.right = r; }
        }

        static int ItemCheck(TreeNode n)
        {
            if (n.left == null) return n.item;
            return n.item + ItemCheck(n.left) - ItemCheck(n.right);
        }

        static TreeNode BottomUpTree(int i, int d)
        {
            if (d > 0) return new TreeNode(i, BottomUpTree(2 * i - 1, d - 1), BottomUpTree(2 * i, d - 1));
            return new TreeNode(i);
        }

        static void RunBinaryTrees()
        {
            int minD = 4, maxD = 14, sD = maxD + 1;
            Console.WriteLine($"Stretching tree of depth {sD}");
            TreeNode st = BottomUpTree(0, sD);
            int ch = ItemCheck(st);
            Console.WriteLine($"Item check: {ch}");

            for (int depth = minD; depth <= maxD; depth += 2)
            {
                int it = 1 << (maxD - depth + minD);
                int sum = 0;
                for (int i = 1; i <= it; i++)
                {
                    TreeNode t1 = BottomUpTree(i, depth);
                    TreeNode t2 = BottomUpTree(-i, depth);
                    sum += ItemCheck(t1) - ItemCheck(t2);
                }
                Console.WriteLine($"{it * 2} trees of depth {depth} item check: {sum}");
            }

            TreeNode ll = BottomUpTree(0, maxD);
            Console.WriteLine($"Long lived tree of depth {maxD} item check: {ItemCheck(ll)}");
        }

        // ==========================================
        // 05_mandelbrot
        // ==========================================
        static void RunMandelbrot()
        {
            int width = 1200;
            int height = 1200;
            int maxIter = 200;

            long totalIter = 0;

            double x0 = -2.0;
            double x1 = 0.5;
            double y0 = -1.25;
            double y1 = 1.25;

            double dx = (x1 - x0) / (width * 1.0);
            double dy = (y1 - y0) / (height * 1.0);

            for (int py = 0; py < height; py++)
            {
                double ci = y0 + py * dy;
                for (int px = 0; px < width; px++)
                {
                    double cr = x0 + px * dx;

                    double zr = 0.0;
                    double zi = 0.0;
                    int iter = 0;

                    while (iter < maxIter)
                    {
                        double zr2 = zr * zr;
                        double zi2 = zi * zi;
                        if (zr2 + zi2 > 4.0)
                        {
                            break;
                        }
                        double newZi = 2.0 * zr * zi + ci;
                        zr = zr2 - zi2 + cr;
                        zi = newZi;
                        iter++;
                    }
                    totalIter += iter;
                }
            }

            Console.WriteLine($"Checksum: {totalIter}");
        }
    }
}
