#include "dsp.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace wr {

namespace {

constexpr float kPi = 3.14159265358979f;

// FFT radix 2 en place (n puissance de 2)
void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2 * kPi / len;
        std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

}  // namespace

void spectrumBands(const int16_t* x, size_t n, int sampleRate, int bands, float* outDb) {
    std::vector<std::complex<float>> a(n);
    for (size_t i = 0; i < n; ++i) {
        float w = 0.5f - 0.5f * std::cos(2 * kPi * i / (n - 1));  // Hann
        a[i] = std::complex<float>(w * x[i] / 32768.0f, 0);
    }
    fft(a);
    // Amplitude d'une sinusoïde à pleine échelle avec une fenêtre de Hann : n/4
    const float norm = 4.0f / n;
    const float binHz = float(sampleRate) / n;
    const float f0 = 60.0f, f1 = std::min(8000.0f, sampleRate / 2.0f);
    size_t prevEnd = 1;
    for (int b = 0; b < bands; ++b) {
        float lo = f0 * std::pow(f1 / f0, float(b) / bands);
        float hi = f0 * std::pow(f1 / f0, float(b + 1) / bands);
        size_t s = std::max(prevEnd, (size_t)std::lround(lo / binHz));
        size_t e = std::max(s + 1, (size_t)std::lround(hi / binHz));
        e = std::min(e, n / 2);
        s = std::min(s, e - 1);
        prevEnd = e;
        float peak = 0;
        for (size_t k = s; k < e; ++k) peak = std::max(peak, std::abs(a[k]) * norm);
        outDb[b] = 20 * std::log10(peak + 1e-6f);
    }
}

Visualizer::Visualizer(int bands) : levels_(bands, 0), peaks_(bands, 0), peakHold_(bands, 0) {}

void Visualizer::reset() {
    std::fill(levels_.begin(), levels_.end(), 0.0f);
    std::fill(peaks_.begin(), peaks_.end(), 0.0f);
    std::fill(peakHold_.begin(), peakHold_.end(), 0);
    ref_ = -30.0f;
}

void Visualizer::update(const float* db) {
    const int n = (int)levels_.size();
    float loudest = -120;
    for (int i = 0; i < n; ++i) loudest = std::max(loudest, db[i]);
    // Sensibilité automatique : suit vite une hausse, oublie lentement (~5 s)
    ref_ = loudest > ref_ ? ref_ + 0.5f * (loudest - ref_) : ref_ + 0.006f * (loudest - ref_);
    ref_ = std::max(ref_, -55.0f);  // en dessous : silence, barres au repos
    const float range = 36.0f;      // dB affichés sous la référence
    for (int i = 0; i < n; ++i) {
        float target = std::max(0.0f, std::min(1.0f, (db[i] - (ref_ - range)) / range * 0.9f));
        float& l = levels_[i];
        l = target > l ? l + 0.6f * (target - l) : l + 0.15f * (target - l);
        if (l >= peaks_[i]) {
            peaks_[i] = l;
            peakHold_[i] = 15;  // ~0,5 s
        } else if (peakHold_[i] > 0) {
            --peakHold_[i];
        } else {
            peaks_[i] = std::max(l, peaks_[i] - 0.02f);
        }
    }
}

MotionDetector::MotionDetector(float threshold, int sustain, int refractory)
    : threshold_(threshold), sustain_(sustain), refractory_(refractory) {}

bool MotionDetector::feed(float ax, float ay, float az) {
    if (samples_ == 0) bx_ = ax, by_ = ay, bz_ = az;
    ++samples_;
    float dx = ax - bx_, dy = ay - by_, dz = az - bz_;
    float dev = std::sqrt(dx * dx + dy * dy + dz * dz);
    // Moyenne lente (~1 s à 50 Hz) : suit l'orientation au repos
    const float k = 0.02f;
    bx_ += k * dx;
    by_ += k * dy;
    bz_ += k * dz;
    if (cooldown_ > 0) {
        --cooldown_;
        return false;
    }
    if (samples_ < 25) return false;  // capteur en cours de stabilisation
    above_ = dev > threshold_ ? above_ + 1 : 0;
    if (above_ >= sustain_) {
        above_ = 0;
        cooldown_ = refractory_;
        return true;
    }
    return false;
}

}  // namespace wr
