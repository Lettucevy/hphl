public class MatrixTransforms {
    static class Mat4 {
        double m00, m01, m02, m03;
        double m10, m11, m12, m13;
        double m20, m21, m22, m23;
        double m30, m31, m32, m33;

        Mat4() {
            m00 = 1.0; m11 = 1.0; m22 = 1.0; m33 = 1.0;
        }

        static Mat4 translation(double tx, double ty, double tz) {
            Mat4 m = new Mat4();
            m.m03 = tx;
            m.m13 = ty;
            m.m23 = tz;
            return m;
        }

        static Mat4 rotationZ(double rad) {
            Mat4 m = new Mat4();
            double c = Math.cos(rad);
            double s = Math.sin(rad);
            m.m00 = c;
            m.m01 = -s;
            m.m10 = s;
            m.m11 = c;
            return m;
        }

        Mat4 mul(Mat4 o) {
            Mat4 r = new Mat4();
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

    public static void main(String[] args) {
        int iters = 2000000;
        double sumX = 0.0;
        double sumY = 0.0;
        double sumZ = 0.0;

        Mat4 t = Mat4.translation(10.0, 20.0, 30.0);
        Mat4 r = Mat4.rotationZ(0.7853981633974483);
        Mat4 combined = t.mul(r);

        double vx = 1.0;
        double vy = 2.0;
        double vz = 3.0;

        for (int i = 0; i < iters; i++) {
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
        System.out.printf("Checksum: %g%n", total);
    }
}
