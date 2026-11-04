// Traitement du signal sans Arduino (testable sur PC) : spectre pour le
// visualiseur et détection de mouvement pour le réveil de l'écran.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wr {

// Spectre d'un bloc audio mono (n puissance de 2) en `bands` bandes espacées
// logarithmiquement de 60 Hz à min(8 kHz, Nyquist). Sortie en dB pleine échelle
// (0 dB : sinusoïde à pleine échelle), fenêtre de Hann.
void spectrumBands(const int16_t* samples, size_t n, int sampleRate, int bands, float* outDb);

// Lissage pour l'affichage : montée rapide, descente douce, repères de crête,
// et sensibilité automatique (la bande la plus forte récente atteint ~90 %).
class Visualizer {
public:
    explicit Visualizer(int bands);
    void reset();
    void update(const float* bandDb);  // un bloc audio
    const std::vector<float>& levels() const { return levels_; }  // 0..1
    const std::vector<float>& peaks() const { return peaks_; }    // 0..1

private:
    std::vector<float> levels_, peaks_;
    std::vector<int> peakHold_;
    float ref_ = -30.0f;  // dB de la bande la plus forte récente
};

// Réveil par mouvement : l'accélération (en g) est comparée à sa moyenne lente ;
// un écart franc et soutenu (soulever, déplacer, taper la table) déclenche.
// Les vibrations faibles (enceinte posée sur le même meuble) sont ignorées.
class MotionDetector {
public:
    // threshold : écart en g ; sustain : nombre d'échantillons consécutifs
    explicit MotionDetector(float threshold = 0.15f, int sustain = 3, int refractory = 50);
    bool feed(float ax, float ay, float az);  // vrai au moment du déclenchement

private:
    float threshold_;
    int sustain_, refractory_;
    float bx_ = 0, by_ = 0, bz_ = 0;
    int samples_ = 0, above_ = 0, cooldown_ = 0;
};

}  // namespace wr
