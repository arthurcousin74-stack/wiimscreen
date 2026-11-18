#include "voice.h"

#include <cstring>

namespace wr {

void wavHeader(uint8_t h[44], uint32_t dataBytes, uint32_t sampleRate) {
    auto le32 = [&](int at, uint32_t v) {
        for (int k = 0; k < 4; ++k) h[at + k] = static_cast<uint8_t>(v >> (8 * k));
    };
    auto le16 = [&](int at, uint16_t v) {
        h[at] = static_cast<uint8_t>(v);
        h[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    std::memcpy(h, "RIFF", 4);
    le32(4, 36 + dataBytes);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    le32(16, 16);           // taille du bloc fmt
    le16(20, 1);            // PCM
    le16(22, 1);            // mono
    le32(24, sampleRate);
    le32(28, sampleRate * 2);
    le16(32, 2);            // octets par échantillon
    le16(34, 16);           // bits
    std::memcpy(h + 36, "data", 4);
    le32(40, dataBytes);
}

}  // namespace wr
