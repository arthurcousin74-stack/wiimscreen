#include "firmware.h"

namespace wr {

size_t FirmwareStream::feed(const uint8_t* d, size_t n, const uint8_t** out) {
    *out = nullptr;
    if (n == 0 || kind_ == Invalid) return 0;
    if (kind_ == Unknown) kind_ = d[0] == 0xE9 ? App : d[0] == 0xFF ? Merged : Invalid;
    size_t keep = 0;
    if (kind_ == App) {
        *out = d;
        keep = n;
    } else if (kind_ == Merged && offset_ + n > kAppOffset) {
        const size_t start = offset_ >= kAppOffset ? 0 : kAppOffset - offset_;
        // Le premier octet de l'application doit être l'octet magique d'une image ESP
        if (offset_ + start == kAppOffset && d[start] != 0xE9) {
            kind_ = Invalid;
        } else {
            *out = d + start;
            keep = n - start;
        }
    }
    offset_ += n;
    if (kind_ == Invalid) {
        *out = nullptr;
        return 0;
    }
    written_ += keep;
    return keep;
}

}  // namespace wr
