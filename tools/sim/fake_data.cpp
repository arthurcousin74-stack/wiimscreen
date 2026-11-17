// Simulator, fake mode only: album art drawn on the fly, favorites in memory, two fake WiiMs on the network.
#include <cmath>
#include <map>
#include <mutex>

#include "M5Unified.h"
#include "art.h"
#include "discovery.h"
#include "favorites.h"
#include "pc.h"

// ------------------------------------------------------------------ discovery: two WiiMs on the network

std::vector<WiimDevice> discoverWiims(uint32_t waitMs) {
    delay(std::min<uint32_t>(waitMs, 800));
    return {{"wiim.sim", "WiiM simul\xC3\xA9", "uuid:sim-1"}, {"wiim2.sim", "WiiM du salon (simul\xC3\xA9)", "uuid:sim-2"}};
}

// ------------------------------------------------------------------ album art: drawn, not downloaded

ArtImage& ArtImage::operator=(ArtImage&& o) noexcept {
    if (this != &o) {
        reset();
        url = std::move(o.url);
        data = o.data;
        len = o.len;
        o.data = nullptr;
        o.len = 0;
    }
    return *this;
}
void ArtImage::reset() {
    free(data);
    data = nullptr;
    len = 0;
}

void ArtFetcher::begin() { lock_ = xSemaphoreCreateMutex(); }

void ArtFetcher::request(const std::string& url, bool) {
    if (url.compare(0, 6, "sim://") != 0) return;  // nothing else exists in the simulator
    ArtImage img;
    img.url = url;
    const std::vector<uint8_t> png = pc::coverPng(url, 300);
    img.data = static_cast<uint8_t*>(malloc(png.size()));
    memcpy(img.data, png.data(), png.size());
    img.len = png.size();
    xSemaphoreTake(lock_, portMAX_DELAY);
    done_.push_back(std::move(img));
    while (done_.size() > 24) done_.pop_front();
    xSemaphoreGive(lock_);
}

bool ArtFetcher::poll(ArtImage& out) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    const bool any = !done_.empty();
    if (any) {
        out = std::move(done_.front());
        done_.pop_front();
    }
    xSemaphoreGive(lock_);
    return any;
}

namespace {
uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return c;
}
void be32(std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s));
}
void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
    be32(png, (uint32_t)data.size());
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    be32(png, crc32(body.data(), body.size()) ^ 0xFFFFFFFFu);
}
void hsv(float h, float s, float v, uint8_t* rgb) {
    const float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0f, 2) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) r = c, g = x;
    else if (h < 120) r = x, g = c;
    else if (h < 180) g = c, b = x;
    else if (h < 240) g = x, b = c;
    else if (h < 300) r = x, b = c;
    else r = c, b = x;
    rgb[0] = (uint8_t)((r + m) * 255), rgb[1] = (uint8_t)((g + m) * 255), rgb[2] = (uint8_t)((b + m) * 255);
}
}  // namespace

// Gradient of the album's hue with circles or stripes: each album gets its own colours (and accent colour)
std::vector<uint8_t> pc::coverPng(const std::string& url, int n) {
    int hue = 200, pattern = 0;
    sscanf(url.c_str(), "sim://cover/%d/%d", &hue, &pattern);
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(n * 3 + 1) * n);
    for (int y = 0; y < n; ++y) {
        raw.push_back(0);  // filter: none
        for (int x = 0; x < n; ++x) {
            const float fx = (float)x / n, fy = (float)y / n;
            float v = 0.35f + 0.5f * (1 - fy), s = 0.75f, h = (float)hue;
            const float dx = fx - 0.62f, dy = fy - 0.4f, d = std::sqrt(dx * dx + dy * dy);
            if (pattern % 3 == 0 && d < 0.22f) v = 0.95f, s = 0.35f;                     // sun
            if (pattern % 3 == 1 && ((int)((fx + fy) * 9) % 2)) h = std::fmod(h + 25, 360);  // stripes
            if (pattern % 3 == 2 && std::fabs(fy - 0.7f) < 0.04f) v = 0.95f, s = 0.1f;   // horizon line
            uint8_t rgb[3];
            hsv(std::fmod(h + 360, 360), s, std::min(1.0f, v), rgb);
            raw.insert(raw.end(), rgb, rgb + 3);
        }
    }
    // zlib with "stored" blocks (no compression): simple and valid
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) a = (a + c) % 65521, b = (b + a) % 65521;
    for (size_t i = 0; i < raw.size(); i += 65535) {
        const size_t len = std::min<size_t>(65535, raw.size() - i);
        z.push_back(i + len >= raw.size() ? 1 : 0);
        z.push_back(len & 0xFF), z.push_back(len >> 8);
        z.push_back(~len & 0xFF), z.push_back((~len >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + i, raw.begin() + i + len);
    }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, n), be32(ihdr, n);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 bits, RGB
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    return png;
}

// ------------------------------------------------------------------ favorites saved on the remote (in memory)

namespace fav {
namespace {
std::mutex g_m;
std::vector<wr::Favorite> g_list;
std::map<std::string, std::string> g_ctx;
std::string g_backup;
uint32_t g_rev = 1;
int g_next = 1;
}  // namespace

bool begin() { return true; }
std::vector<wr::Favorite> list() {
    std::lock_guard<std::mutex> l(g_m);
    return g_list;
}
bool add(wr::Favorite& f, const std::string& ctx, std::string& error) {
    std::lock_guard<std::mutex> l(g_m);
    for (auto& x : g_list)
        if (x.key == f.key) {
            f.id = x.id;
            x = f;
            g_ctx[f.id] = ctx;
            ++g_rev;
            return true;
        }
    if (g_list.size() >= kMax) {
        error = "trop de favoris";
        return false;
    }
    f.id = "f" + std::to_string(g_next++);
    g_list.push_back(f);
    g_ctx[f.id] = ctx;
    ++g_rev;
    return true;
}
bool remove(const std::string& id) {
    std::lock_guard<std::mutex> l(g_m);
    for (auto it = g_list.begin(); it != g_list.end(); ++it)
        if (it->id == id) {
            g_list.erase(it);
            g_ctx.erase(id);
            ++g_rev;
            return true;
        }
    return false;
}
bool context(const std::string& id, std::string& out) {
    std::lock_guard<std::mutex> l(g_m);
    auto it = g_ctx.find(id);
    if (it == g_ctx.end()) return false;
    out = it->second;
    return true;
}
uint32_t revision() { return g_rev; }
bool saveQobuzTemplate(const std::string&) { return true; }
bool qobuzTemplate(std::string& out) { return !(out = "sim").empty(); }
bool saveSourceModel(const std::string&, const std::string&) { return true; }
bool sourceModel(const std::string&, std::string& out) { return !(out = "sim").empty(); }
bool hasDeezerModel() { return true; }
bool hasQobuzModel() { return true; }
bool saveSettingsBackup(const std::string& text) { return !(g_backup = text).empty() || true; }
bool settingsBackup(std::string& out) { return !(out = g_backup).empty(); }
bool eraseAll() {
    std::lock_guard<std::mutex> l(g_m);
    g_list.clear(), g_ctx.clear(), g_backup.clear();
    ++g_rev;
    return true;
}
}  // namespace fav
