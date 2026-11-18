// Tests: OTA stream filter (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>

#include "firmware.h"
#include "test_helpers.h"

using namespace wr;

static void test_firmware_stream() {
    // Application seule : tout est écrit
    std::vector<uint8_t> app(5000, 0x11);
    app[0] = 0xE9;
    FirmwareStream a;
    const uint8_t* out;
    size_t total = 0;
    for (size_t i = 0; i < app.size(); i += 1436) total += a.feed(app.data() + i, std::min<size_t>(1436, app.size() - i), &out);
    TEST_ASSERT_EQUAL(FirmwareStream::App, a.kind());
    TEST_ASSERT_EQUAL(5000, (int)total);

    // Image complète : seule la partie à partir de 0x10000 est écrite, octets identiques
    std::vector<uint8_t> merged(0x10000 + 3000, 0xFF);
    merged[0x2000] = 0xE9;  // chargeur de démarrage
    for (size_t i = 0x10000; i < merged.size(); ++i) merged[i] = (uint8_t)(i * 7);
    merged[0x10000] = 0xE9;
    FirmwareStream m;
    std::vector<uint8_t> written;
    for (size_t i = 0; i < merged.size(); i += 1436) {
        size_t k = m.feed(merged.data() + i, std::min<size_t>(1436, merged.size() - i), &out);
        written.insert(written.end(), out, out + k);
    }
    TEST_ASSERT_EQUAL(FirmwareStream::Merged, m.kind());
    TEST_ASSERT_EQUAL(3000, (int)written.size());
    TEST_ASSERT_TRUE(std::equal(written.begin(), written.end(), merged.begin() + 0x10000));

    // Image complète sans application valide à 0x10000 : refusée
    std::vector<uint8_t> bad(0x10000 + 100, 0xFF);
    FirmwareStream b;
    for (size_t i = 0; i < bad.size(); i += 1436) b.feed(bad.data() + i, std::min<size_t>(1436, bad.size() - i), &out);
    TEST_ASSERT_EQUAL(FirmwareStream::Invalid, b.kind());
    TEST_ASSERT_EQUAL(0, (int)b.written());

    // Autre fichier (zip, image…) : refusé dès le premier octet
    const uint8_t zip[] = {'P', 'K', 3, 4};
    FirmwareStream z;
    TEST_ASSERT_EQUAL(0, (int)z.feed(zip, sizeof zip, &out));
    TEST_ASSERT_EQUAL(FirmwareStream::Invalid, z.kind());
}

void run_firmware_tests() {
    RUN_TEST(test_firmware_stream);
}
