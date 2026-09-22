#include <cstdio>

int main() {
    int width = 1200;
    int height = 1200;
    int maxIter = 200;

    long long totalIter = 0;

    double x0 = -2.0;
    double x1 = 0.5;
    double y0 = -1.25;
    double y1 = 1.25;

    double dx = (x1 - x0) / (width * 1.0);
    double dy = (y1 - y0) / (height * 1.0);

    for (int py = 0; py < height; py++) {
        double ci = y0 + py * dy;
        for (int px = 0; px < width; px++) {
            double cr = x0 + px * dx;

            double zr = 0.0;
            double zi = 0.0;
            int iter = 0;

            while (iter < maxIter) {
                double zr2 = zr * zr;
                double zi2 = zi * zi;
                if (zr2 + zi2 > 4.0) {
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

    std::printf("Checksum: %lld\n", totalIter);
    return 0;
}
