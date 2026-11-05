#include "art.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <algorithm>

#include "core.h"

#include "ca_bundle.h"

static constexpr size_t kMaxImage = 768 * 1024;

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
    if (data) heap_caps_free(data);
    data = nullptr;
    len = 0;
}

namespace {

// Flux d'écriture vers un tampon borné en PSRAM.
// HTTPClient::writeToStream s'occupe de "chunked" et de Content-Length.
class BoundedBuffer : public Stream {
public:
    explicit BoundedBuffer(size_t cap)
        : buf_(static_cast<uint8_t*>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))),
          cap_(buf_ ? cap : 0) {}
    ~BoundedBuffer() override {
        if (buf_) heap_caps_free(buf_);
    }
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* d, size_t n) override {
        if (!buf_ || len_ + n > cap_) {
            overflow_ = true;
            return 0;
        }
        memcpy(buf_ + len_, d, n);
        len_ += n;
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

    bool ok() const { return buf_ && !overflow_; }
    const uint8_t* data() const { return buf_; }
    size_t size() const { return len_; }
    uint8_t* release() {
        uint8_t* p = buf_;
        buf_ = nullptr;
        return p;
    }

private:
    uint8_t* buf_;
    size_t cap_;
    size_t len_ = 0;
    bool overflow_ = false;
};

bool download(const std::string& url, ArtImage& out) {
    if (!wr::isHttpUrl(url) || WiFi.status() != WL_CONNECTED) return false;
    const bool https = url.compare(0, 8, "https://") == 0;

    WiFiClientSecure secure;
    WiFiClient plain;
    if (https) {
        secure.setCACertBundle(ca_bundle_start, caBundleSize());
        secure.setHandshakeTimeout(8);
    }
    WiFiClient& client = https ? static_cast<WiFiClient&>(secure) : plain;

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(3);
    http.setUserAgent("wiim-remote-tab5");
    if (!http.begin(client, url.c_str())) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        log_w("pochette : HTTP %d pour %s", code, url.c_str());
        http.end();
        return false;
    }
    int size = http.getSize();  // -1 si inconnue (chunked)
    if (size == 0 || size > (int)kMaxImage) {
        http.end();
        return false;
    }
    BoundedBuffer buf(size > 0 ? (size_t)size : kMaxImage);
    int n = http.writeToStream(&buf);
    http.end();
    if (n <= 0 || !buf.ok() || wr::sniffImage(buf.data(), buf.size()) == wr::ImageType::Unknown) {
        log_w("pochette : téléchargement ou format refusé (%d) pour %s", n, url.c_str());
        return false;
    }
    out.len = buf.size();
    out.data = buf.release();
    return true;
}

}  // namespace

void ArtFetcher::begin() {
    lock_ = xSemaphoreCreateMutex();
    // Cœur 0 (celui du Wi-Fi) ; la boucle Arduino tourne sur le cœur 1
    xTaskCreatePinnedToCore(taskMain, "art", 16 * 1024, this, 1, &task_, 0);
}

void ArtFetcher::request(const std::string& url, bool urgent) {
    if (!lock_ || url.empty()) return;
    xSemaphoreTake(lock_, portMAX_DELAY);
    auto it = std::find(queue_.begin(), queue_.end(), url);
    if (it != queue_.end()) queue_.erase(it);
    if (urgent) queue_.push_front(url);
    else queue_.push_back(url);
    xSemaphoreGive(lock_);
    xTaskNotifyGive(task_);
}

bool ArtFetcher::poll(ArtImage& out) {
    if (!lock_) return false;
    bool got = false;
    xSemaphoreTake(lock_, portMAX_DELAY);
    if (!done_.empty()) {
        out = std::move(done_.front());
        done_.pop_front();
        got = true;
    }
    xSemaphoreGive(lock_);
    return got;
}

void ArtFetcher::taskMain(void* self) { static_cast<ArtFetcher*>(self)->run(); }

void ArtFetcher::run() {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            xSemaphoreTake(lock_, portMAX_DELAY);
            bool has = !queue_.empty();
            std::string url;
            if (has) {
                url = queue_.front();
                queue_.pop_front();
            }
            xSemaphoreGive(lock_);
            if (!has) break;

            ArtImage img;
            img.url = url;
            if (!download(url, img)) img.reset();

            xSemaphoreTake(lock_, portMAX_DELAY);
            done_.push_back(std::move(img));
            while (done_.size() > 16) done_.pop_front();
            xSemaphoreGive(lock_);
        }
    }
}
