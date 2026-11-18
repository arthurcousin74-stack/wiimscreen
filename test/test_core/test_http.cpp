// Tests: HTTP framing and parsing, URLs, hosts, certificate fingerprints (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>

#include "core.h"
#include "test_helpers.h"

using namespace wr;

static void test_http_frame() {
    // Content-Length : complet seulement quand tout le corps est là
    std::string h = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\n";
    TEST_ASSERT_FALSE(httpFrame("HTTP/1.1 200 OK\r\nContent-Len").complete);
    TEST_ASSERT_FALSE(httpFrame(h + "O").complete);
    HttpFrame f = httpFrame(h + "OK");
    TEST_ASSERT_TRUE(f.complete && f.reusable);
    // Le serveur ferme : complet mais pas réutilisable
    f = httpFrame("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\nOK");
    TEST_ASSERT_TRUE(f.complete);
    TEST_ASSERT_FALSE(f.reusable);
    // HTTP/1.0 sans keep-alive explicite : fermeture
    TEST_ASSERT_FALSE(httpFrame("HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nOK").reusable);
    TEST_ASSERT_TRUE(httpFrame("HTTP/1.0 200 OK\r\nConnection: Keep-Alive\r\nContent-Length: 2\r\n\r\nOK").reusable);
    // Chunked : complet au bloc de taille 0
    std::string c = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nOK\r\n";
    TEST_ASSERT_FALSE(httpFrame(c).complete);
    TEST_ASSERT_TRUE(httpFrame(c + "0\r\n\r\n").complete);
    TEST_ASSERT_TRUE(httpFrame("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n").complete);
    // Sans longueur : lire jusqu'à la fermeture
    f = httpFrame("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK");
    TEST_ASSERT_FALSE(f.complete);
    TEST_ASSERT_FALSE(f.reusable);
    // Corps vide (Content-Length: 0)
    TEST_ASSERT_TRUE(httpFrame("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n").complete);
    // Espaces en plus après les deux-points (Qobuz : « Transfer-Encoding:  chunked », vu le 2026-11-17)
    TEST_ASSERT_TRUE(httpFrame("HTTP/1.1 200 OK\r\nTransfer-Encoding:  chunked\r\n\r\n0\r\n\r\n").complete);
    TEST_ASSERT_TRUE(httpFrame("HTTP/1.1 200 OK\r\nContent-Length:2\r\n\r\nOK").reusable);
    TEST_ASSERT_FALSE(httpFrame("HTTP/1.1 200 OK\r\nConnection:  close\r\nContent-Length: 2\r\n\r\nOK").reusable);
}

static void test_urls_and_images() {
    TEST_ASSERT_TRUE(isHttpUrl("https://i.scdn.co/image/ab67616d"));
    TEST_ASSERT_TRUE(isHttpUrl("http://192.168.1.20:8080/cover.jpg"));
    TEST_ASSERT_FALSE(isHttpUrl("https://"));
    TEST_ASSERT_FALSE(isHttpUrl("https:///path"));
    TEST_ASSERT_FALSE(isHttpUrl("ftp://host/x"));
    TEST_ASSERT_FALSE(isHttpUrl("/tmp/cover.jpg"));
    TEST_ASSERT_FALSE(isHttpUrl("http://host/a b"));
    TEST_ASSERT_FALSE(isHttpUrl("http://host/\r\nX: y"));

    const uint8_t jpg[] = {0xFF, 0xD8, 0xFF, 0xE0};
    const uint8_t png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0};
    const uint8_t gif[] = {'G', 'I', 'F', '8', '9', 'a'};
    TEST_ASSERT_TRUE(sniffImage(jpg, sizeof jpg) == ImageType::Jpeg);
    TEST_ASSERT_TRUE(sniffImage(png, sizeof png) == ImageType::Png);
    TEST_ASSERT_TRUE(sniffImage(gif, sizeof gif) == ImageType::Unknown);
    TEST_ASSERT_TRUE(sniffImage(jpg, 2) == ImageType::Unknown);
}

static void test_http_response() {
    HttpResponse r;
    TEST_ASSERT_TRUE(parseHttpResponse("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK", r));
    TEST_ASSERT_EQUAL(200, r.status);
    TEST_ASSERT_EQUAL_STRING("OK", r.body.c_str());

    TEST_ASSERT_TRUE(parseHttpResponse(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n", r));
    TEST_ASSERT_EQUAL_STRING("Wikipedia", r.body.c_str());
    // Qobuz (vu le 2026-11-17) : deux espaces, tailles de bloc avec des zéros devant
    TEST_ASSERT_TRUE(parseHttpResponse(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding:  chunked\r\nConnection: close\r\n\r\n00000002\r\n{}\r\n0\r\n\r\n", r));
    TEST_ASSERT_EQUAL_STRING("{}", r.body.c_str());

    TEST_ASSERT_TRUE(parseHttpResponse("HTTP/1.0 404 Not Found\r\n\r\n", r));
    TEST_ASSERT_EQUAL(404, r.status);
    TEST_ASSERT_FALSE(parseHttpResponse("garbage", r));
    TEST_ASSERT_FALSE(parseHttpResponse("HTTP/1.1 200 OK\r\nno-end", r));
    // Longueur de morceau hostile (débordement si additionnée) : se termine, corps borné par l'entrée
    const std::string evil = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\nffffffffffffffec\r\nxyz\r\n0\r\n\r\n";
    parseHttpResponse(evil, r);
    TEST_ASSERT_TRUE(r.body.size() <= evil.size());
}

static void test_fingerprint() {
    uint8_t fp[32], other[32];
    std::string hex(64, 'a');
    TEST_ASSERT_TRUE(parseFingerprint(hex, fp));
    TEST_ASSERT_EQUAL_HEX8(0xaa, fp[0]);
    std::string colon;
    for (int i = 0; i < 32; ++i) colon += (i ? ":AA" : "AA");
    TEST_ASSERT_TRUE(parseFingerprint(colon, other));
    TEST_ASSERT_TRUE(constantTimeEq(fp, other, 32));
    other[31] ^= 1;
    TEST_ASSERT_FALSE(constantTimeEq(fp, other, 32));
    TEST_ASSERT_FALSE(parseFingerprint("abcd", fp));
    TEST_ASSERT_FALSE(parseFingerprint(std::string(64, 'z'), fp));
    TEST_ASSERT_EQUAL_STRING(hex.c_str(), toHex(fp, 32).c_str());
}

static void test_hosts() {
    TEST_ASSERT_TRUE(isValidHost("192.168.1.50"));
    TEST_ASSERT_TRUE(isValidHost("wiim.local"));
    TEST_ASSERT_TRUE(isValidHost("192.168.1.50:8443"));
    TEST_ASSERT_FALSE(isValidHost(""));
    TEST_ASSERT_FALSE(isValidHost("192.168.1.50:99999"));
    TEST_ASSERT_FALSE(isValidHost("evil host"));
    TEST_ASSERT_FALSE(isValidHost("a/b"));
    std::string name;
    uint16_t port;
    splitHostPort("10.0.0.2:8443", name, port, 443);
    TEST_ASSERT_EQUAL_STRING("10.0.0.2", name.c_str());
    TEST_ASSERT_EQUAL(8443, port);
    splitHostPort("10.0.0.2", name, port, 443);
    TEST_ASSERT_EQUAL(443, port);
}

static void test_http_chunks() {
    const std::string h = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    // Données d'un morceau qui contiennent « \r\n0\r\n\r\n » : pas la fin
    const std::string data = "\r\n0\r\n\r\nxyz";  // 10 octets
    TEST_ASSERT_FALSE(httpFrame(h + "a\r\n" + data).complete);
    TEST_ASSERT_FALSE(httpFrame(h + "a\r\n" + data + "\r\n").complete);
    HttpFrame f = httpFrame(h + "a\r\n" + data + "\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(f.complete && f.reusable);
    HttpResponse r;
    TEST_ASSERT_TRUE(parseHttpResponse(h + "a\r\n" + data + "\r\n0\r\n\r\n", r));
    TEST_ASSERT_TRUE(r.body == data);
    // Extensions de morceau, en-têtes de fin, taille en majuscules
    TEST_ASSERT_TRUE(parseHttpResponse(h + "A;x=1\r\n0123456789\r\n0\r\nX-T: 1\r\n\r\n", r));
    TEST_ASSERT_EQUAL_STRING("0123456789", r.body.c_str());
    TEST_ASSERT_FALSE(httpFrame(h + "0\r\nX-T: 1\r\n").complete);
    // Tronqué : refusé (avant : corps partiel accepté)
    TEST_ASSERT_FALSE(parseHttpResponse(h + "4\r\nWi", r));
    TEST_ASSERT_FALSE(parseHttpResponse(h + "4\r\nWiki\r\n", r));
    TEST_ASSERT_FALSE(parseHttpResponse(h, r));
    // Abîmé : fin de lecture (inutile d'attendre), connexion non réutilisée, réponse refusée
    f = httpFrame(h + "zz\r\n");
    TEST_ASSERT_TRUE(f.complete && !f.reusable);
    f = httpFrame(h + "2\r\nabcd\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(f.complete && !f.reusable);
    TEST_ASSERT_FALSE(parseHttpResponse(h + "2\r\nabcd\r\n0\r\n\r\n", r));
    TEST_ASSERT_FALSE(parseHttpResponse(h + "ffffffffffffffec\r\nxyz\r\n0\r\n\r\n", r));
    // Content-Length illisible : longueur inconnue (avant : 0, réponse « complète » tout de suite)
    f = httpFrame("HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\nOK");
    TEST_ASSERT_FALSE(f.complete || f.reusable);
    TEST_ASSERT_FALSE(httpFrame("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n").complete);
    TEST_ASSERT_FALSE(httpFrame("HTTP/1.1 200 OK\r\nContent-Length: 2x\r\n\r\nOK").complete);
    TEST_ASSERT_TRUE(httpFrame("HTTP/1.1 200 OK\r\ncontent-length:2\r\n\r\nOK").complete);
    // Transfer-Encoding l'emporte sur Content-Length
    TEST_ASSERT_FALSE(httpFrame("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n").complete);
    TEST_ASSERT_FALSE(httpFrame("HTTP/1.1 200 OK\r\nConnection: Close\r\nContent-Length: 0\r\n\r\n").reusable);
}

void run_http_tests() {
    RUN_TEST(test_http_frame);
    RUN_TEST(test_urls_and_images);
    RUN_TEST(test_http_response);
    RUN_TEST(test_fingerprint);
    RUN_TEST(test_hosts);
    RUN_TEST(test_http_chunks);
}
