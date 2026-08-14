#include "ColorDifference.hpp"

#include <cmath>

namespace Slic3r::ImagePaint {

// CIEDE2000 — full implementation per Sharma et al. (2005).
// Reference pairs and validation: see test_image_paint_color.cpp.
double delta_e_2000(const ColorLab& lab1, const ColorLab& lab2) noexcept
{
    constexpr double kPi = 3.14159265358979323846;

    // Step 1: compute C'ab and h'ab for each sample.
    const double C1 = std::sqrt(lab1.a * lab1.a + lab1.b * lab1.b);
    const double C2 = std::sqrt(lab2.a * lab2.a + lab2.b * lab2.b);
    const double C_avg = (C1 + C2) / 2.0;
    const double C_avg7 = std::pow(C_avg, 7.0);
    const double G = 0.5 * (1.0 - std::sqrt(C_avg7 / (C_avg7 + std::pow(25.0, 7.0))));

    const double a1p = lab1.a * (1.0 + G);
    const double a2p = lab2.a * (1.0 + G);

    const double C1p = std::sqrt(a1p * a1p + lab1.b * lab1.b);
    const double C2p = std::sqrt(a2p * a2p + lab2.b * lab2.b);

    auto h_prime = [&](double ap, double bp) -> double {
        if (ap == 0.0 && bp == 0.0) return 0.0;
        double h = std::atan2(bp, ap) * (180.0 / kPi);
        if (h < 0.0) h += 360.0;
        return h;
    };

    const double h1p = h_prime(a1p, lab1.b);
    const double h2p = h_prime(a2p, lab2.b);

    // Step 2: compute ΔL', ΔC', Δh'.
    const double dLp = lab2.l - lab1.l;
    const double dCp = C2p - C1p;

    double dhp;
    if (C1p * C2p == 0.0) {
        dhp = 0.0;
    } else if (std::abs(h2p - h1p) <= 180.0) {
        dhp = h2p - h1p;
    } else if (h2p - h1p > 180.0) {
        dhp = h2p - h1p - 360.0;
    } else {
        dhp = h2p - h1p + 360.0;
    }

    const double dHp = 2.0 * std::sqrt(C1p * C2p) * std::sin(dhp / 2.0 * kPi / 180.0);

    // Step 3: compute CIEDE2000.
    const double Lp_avg = (lab1.l + lab2.l) / 2.0;
    const double Cp_avg = (C1p + C2p) / 2.0;

    double Hp_avg;
    if (C1p * C2p == 0.0) {
        Hp_avg = h1p + h2p;
    } else if (std::abs(h1p - h2p) <= 180.0) {
        Hp_avg = (h1p + h2p) / 2.0;
    } else if (h1p + h2p < 360.0) {
        Hp_avg = (h1p + h2p + 360.0) / 2.0;
    } else {
        Hp_avg = (h1p + h2p - 360.0) / 2.0;
    }

    const double T = 1.0
        - 0.17 * std::cos((Hp_avg - 30.0) * kPi / 180.0)
        + 0.24 * std::cos(2.0 * Hp_avg * kPi / 180.0)
        + 0.32 * std::cos((3.0 * Hp_avg + 6.0) * kPi / 180.0)
        - 0.20 * std::cos((4.0 * Hp_avg - 63.0) * kPi / 180.0);

    const double SL = 1.0 + 0.015 * std::pow(Lp_avg - 50.0, 2.0)
                      / std::sqrt(20.0 + std::pow(Lp_avg - 50.0, 2.0));
    const double SC = 1.0 + 0.045 * Cp_avg;
    const double SH = 1.0 + 0.015 * Cp_avg * T;

    const double Cp_avg7 = std::pow(Cp_avg, 7.0);
    const double RC = 2.0 * std::sqrt(Cp_avg7 / (Cp_avg7 + std::pow(25.0, 7.0)));

    const double d_theta = 30.0 * std::exp(-std::pow((Hp_avg - 275.0) / 25.0, 2.0));
    const double RT = -std::sin(2.0 * d_theta * kPi / 180.0) * RC;

    const double dE = std::sqrt(
        std::pow(dLp  / SL, 2.0) +
        std::pow(dCp  / SC, 2.0) +
        std::pow(dHp  / SH, 2.0) +
        RT * (dCp / SC) * (dHp / SH));

    return dE;
}

double delta_e_76(const ColorLab& a, const ColorLab& b) noexcept
{
    const double dl = a.l - b.l;
    const double da = a.a - b.a;
    const double db = a.b - b.b;
    return std::sqrt(dl*dl + da*da + db*db);
}

} // namespace Slic3r::ImagePaint
