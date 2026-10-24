#include "plugins/ToneStack.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace northlight::plugins::tonestack
{

namespace
{
using Complex = std::complex<double>;
constexpr int nodes = 5; // the treble pot's top and wiper, the bass pot's top and bottom, the slope node

// The circuit's nodal matrix at `s` (columns 0..4) and its source vector
// (column 5) for 1 V in: C1 from the input to the treble pot's top (T); the
// treble pot from T to its bottom (B) with the output on its wiper (W); the
// slope resistor from the input to node N; C2 from N to B; C3 from N to the
// bass pot's bottom (M); the bass pot from B to M; the middle pot from M to
// ground.
using Matrix = std::array<std::array<Complex, nodes + 1>, nodes>;

Matrix nodal(const Components& c, double t, double m, double l, Complex s) noexcept
{
    enum
    {
        T,
        W,
        B,
        M,
        N,
        source
    };
    Matrix a{};
    const auto link = [&](int i, int j, Complex y)
    {
        a[static_cast<size_t>(i)][static_cast<size_t>(i)] += y;
        a[static_cast<size_t>(j)][static_cast<size_t>(j)] += y;
        a[static_cast<size_t>(i)][static_cast<size_t>(j)] -= y;
        a[static_cast<size_t>(j)][static_cast<size_t>(i)] -= y;
    };
    const auto fromInput = [&](int i, Complex y)
    {
        a[static_cast<size_t>(i)][static_cast<size_t>(i)] += y;
        a[static_cast<size_t>(i)][source] += y;
    };
    fromInput(T, s * c.trebleCap);
    link(T, W, 1.0 / ((1.0 - t) * c.treblePot));
    link(W, B, 1.0 / (t * c.treblePot));
    fromInput(N, 1.0 / c.slope);
    link(N, B, s * c.bassCap);
    link(N, M, s * c.middleCap);
    link(B, M, 1.0 / (l * c.bassPot));
    a[M][M] += 1.0 / (m * c.middlePot);
    return a;
}

// The determinant of the first five columns, with column `replace` swapped for
// the source column (or none, for -1). Gaussian elimination with pivoting.
Complex determinant(Matrix a, int replace) noexcept
{
    if (replace >= 0)
        for (auto& row : a)
            row[static_cast<size_t>(replace)] = row[nodes];
    Complex det = 1.0;
    for (int col = 0; col < nodes; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < nodes; ++r)
            if (std::abs(a[static_cast<size_t>(r)][static_cast<size_t>(col)]) >
                std::abs(a[static_cast<size_t>(pivot)][static_cast<size_t>(col)]))
                pivot = r;
        if (pivot != col)
        {
            std::swap(a[static_cast<size_t>(pivot)], a[static_cast<size_t>(col)]);
            det = -det;
        }
        const Complex p = a[static_cast<size_t>(col)][static_cast<size_t>(col)];
        if (std::abs(p) == 0.0)
            return 0.0;
        det *= p;
        for (int r = col + 1; r < nodes; ++r)
        {
            const Complex f = a[static_cast<size_t>(r)][static_cast<size_t>(col)] / p;
            for (int k = col; k < nodes; ++k)
                a[static_cast<size_t>(r)][static_cast<size_t>(k)] -=
                    f * a[static_cast<size_t>(col)][static_cast<size_t>(k)];
        }
    }
    return det;
}
} // namespace

Analog analog(const Components& c, double treble, double middle, double bass) noexcept
{
    // Pots never quite reach their ends (a dead-zero middle makes a pole and a
    // zero meet and the filter ill-conditioned).
    const double t = std::clamp(treble, 1.0e-3, 1.0 - 1.0e-3);
    const double m = std::clamp(middle, 1.0e-3, 1.0);
    const double l = std::clamp(bass, 1.0e-3, 1.0);
    // By Cramer's rule the output is det(output column replaced) / det. Both are
    // polynomials in s of degree at most five (three capacitors make it three),
    // found exactly from six values on a circle of radius s0 (an inverse DFT).
    constexpr int points = 6;
    const double s0 = 2.0 * std::numbers::pi * 500.0;
    std::array<Complex, points> num{}, den{};
    for (int k = 0; k < points; ++k)
    {
        const Complex z = std::polar(1.0, 2.0 * std::numbers::pi * k / points);
        const auto a = nodal(c, t, m, l, s0 * z);
        const Complex dn = determinant(a, 1), dd = determinant(a, -1);
        for (int n = 0; n < points; ++n)
        {
            const Complex w = std::polar(1.0, -2.0 * std::numbers::pi * k * n / points) / double(points);
            num[static_cast<size_t>(n)] += dn * w;
            den[static_cast<size_t>(n)] += dd * w;
        }
    }
    Analog h;
    const double norm = den[0].real();
    double scale = 1.0;
    for (size_t n = 0; n < 4; ++n)
    {
        h.b[n] = num[n].real() / (norm * scale);
        h.a[n] = den[n].real() / (norm * scale);
        scale *= s0;
    }
    h.b[0] = 0.0; // the treble cap and the bass and middle caps block DC
    return h;
}

std::complex<double> response(const Analog& h, double hz) noexcept
{
    const std::complex<double> s(0.0, 2.0 * std::numbers::pi * hz);
    const auto num = h.b[0] + s * (h.b[1] + s * (h.b[2] + s * h.b[3]));
    const auto den = h.a[0] + s * (h.a[1] + s * (h.a[2] + s * h.a[3]));
    return num / den;
}

double audioTaper(double position) noexcept
{
    // (e^(kx) - 1) / (e^k - 1) with k = 2 ln 9: 10% of the resistance at half way.
    constexpr double k = 4.394449154672439;
    const double x = std::clamp(position, 0.0, 1.0);
    return std::expm1(k * x) / std::expm1(k);
}

void Filter::design(const Analog& h, double sampleRate) noexcept
{
    // The bilinear transform, s = K (1 - z^-1) / (1 + z^-1), K = 2 fs.
    const double K = 2.0 * sampleRate, K2 = K * K, K3 = K2 * K;
    const auto map = [&](const std::array<double, 4>& p)
    {
        return std::array<double, 4>{p[0] + p[1] * K + p[2] * K2 + p[3] * K3,
                                     3.0 * p[0] + p[1] * K - p[2] * K2 - 3.0 * p[3] * K3,
                                     3.0 * p[0] - p[1] * K - p[2] * K2 + 3.0 * p[3] * K3,
                                     p[0] - p[1] * K + p[2] * K2 - p[3] * K3};
    };
    const auto b = map(h.b), a = map(h.a);
    for (size_t i = 0; i < 4; ++i)
    {
        b_[i] = b[i] / a[0];
        a_[i] = a[i] / a[0];
    }
}

double Filter::magnitude(double hz, double sampleRate) const noexcept
{
    const double w = 2.0 * std::numbers::pi * hz / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const auto num = b_[0] + z1 * (b_[1] + z1 * (b_[2] + z1 * b_[3]));
    const auto den = a_[0] + z1 * (a_[1] + z1 * (a_[2] + z1 * a_[3]));
    return std::abs(num / den);
}

} // namespace northlight::plugins::tonestack
