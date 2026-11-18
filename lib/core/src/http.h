// HTTP without Arduino: response parsing and framing on a kept-alive connection, URL and host checks,
// certificate fingerprints, image type of a downloaded body.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace wr {

// URL http:// ou https:// avec un hôte, sans espace ni caractère de contrôle.
bool isHttpUrl(const std::string& url);

enum class ImageType { Unknown, Jpeg, Png };
ImageType sniffImage(const uint8_t* data, size_t len);

struct HttpResponse {
    int status = 0;
    std::string body;
};
// Réponse HTTP/1.x brute (en-têtes + corps), corps "chunked" accepté.
bool parseHttpResponse(const std::string& raw, HttpResponse& out);

// Où en est la lecture d'une réponse sur une connexion gardée ouverte (keep-alive) :
// complete : la réponse entière est arrivée (Content-Length atteint, ou fin "chunked") ;
// reusable : la connexion peut servir à la requête suivante (le serveur ne la ferme
// pas et la fin du corps est délimitée). Sans longueur connue, il faut lire jusqu'à
// la fermeture : complete et reusable restent faux.
struct HttpFrame {
    bool complete = false;
    bool reusable = false;
};
HttpFrame httpFrame(const std::string& raw);

std::string toHex(const uint8_t* data, size_t len);
// Accepte "AA:BB:..." ou "aabb..." (64 caractères hexa utiles).
bool parseFingerprint(const std::string& s, uint8_t out[32]);
bool constantTimeEq(const uint8_t* a, const uint8_t* b, size_t len);

// IPv4 ou nom d'hôte, avec ":port" optionnel.
bool isValidHost(const std::string& host);
void splitHostPort(const std::string& host, std::string& name, uint16_t& port, uint16_t defaultPort);

}  // namespace wr
