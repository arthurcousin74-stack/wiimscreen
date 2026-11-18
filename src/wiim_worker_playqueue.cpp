// Transport UPnP PlayQueue du WiiM (non documenté) : requêtes SOAP, lecture, création, lancement et
// suppression des files ; diagnostic de la file (runQueueCheck). Partie de la classe WiimWorker (wiim_worker.h).
#include "wiim_worker.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include <algorithm>

#include "favorites.h"
#include "log.h"
#include "upnp.h"
#include "wiim_worker_internal.h"

using namespace wiimq;

// ---------------------------------------------------------------------------
// Diagnostic de la file de lecture (UPnP PlayQueue)

namespace wiimq {

constexpr const char* kPlayQueue = "urn:schemas-wiimu-com:service:PlayQueue:1";
constexpr size_t kMaxBody = 3 * 1024 * 1024;  // en PSRAM ; une file de recherche Qobuz monte à 1000 morceaux, 1,35 Mo
                                              // (vu sur un WiiM, 2026-11-17) : tronquée, sa remise échouait

// GET ou POST SOAP en HTTP simple (l'UPnP du WiiM n'est pas chiffré)
int upnpRequest(const std::string& url, const char* soapAction, const std::string& body, std::string& out,
                bool* truncated) {
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(2500);
    http.setTimeout(6000);
    http.setReuse(false);
    if (!http.begin(client, url.c_str())) return -1;
    int code;
    if (soapAction) {
        http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
        http.addHeader("SOAPACTION", (String("\"") + kPlayQueue + "#" + soapAction + "\"").c_str());
        code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body.data())), body.size());
    } else {
        code = http.GET();
    }
    CapStream s(kMaxBody);
    if (code > 0) http.writeToStream(&s);
    http.end();
    out.swap(s.data);
    if (truncated) *truncated = s.truncated;
    return code;
}

// Journal : lignes courtes (l'écran coupe au-delà), découpées si besoin
// `tag` (facultatif) : première ligne étiquetée (logTagged, src/log.h), avec les paires clé=valeur `kv`
void logLong(const char* label, const std::string& text, const char* tag, const char* kv) {
    const size_t kChunk = 56;  // une ligne de l'écran du journal
    for (size_t i = 0; i == 0 || (i < text.size() && i < kChunk * 6); i += kChunk) {
        const std::string part = text.size() <= kChunk ? text : text.substr(i, kChunk);
        if (i == 0 && tag) logTagged(tag, "%s | " "  %s%s", kv, label, part.c_str());
        else logEvent("  %s%s", i == 0 ? label : "  ", part.c_str());
    }
}

}  // namespace wiimq

namespace {

// Copie complète sur le port série, sans jeton ni compte en clair
void serialDump(const char* title, const std::string& xml, const std::string& user) {
    std::string safe = wr::maskSecrets(xml);
    if (!user.empty()) {
        size_t p = 0;
        const std::string masked = wr::maskAccount(user);
        while ((p = safe.find(user, p)) != std::string::npos) safe.replace(p, user.size(), masked), p += masked.size();
    }
    Serial.printf("----- %s (%u octets) -----\n", title, (unsigned)safe.size());
    for (size_t i = 0; i < safe.size(); i += 256) Serial.write(safe.data() + i, std::min<size_t>(256, safe.size() - i));
    Serial.println("\n----- fin -----");
}

}  // namespace

// Service PlayQueue du WiiM : URL de contrôle (gardée), actions offertes (journal)
bool WiimWorker::findPlayQueue(std::string& control, bool verbose) {
    if (!pqControl_.empty() && !verbose) {
        control = pqControl_;
        return true;
    }
    // Pas de service PlayQueue trouvé il y a peu : pas de nouvel essai (2 requêtes et 2 lignes de journal à chaque
    // changement de morceau sinon) avant 10 min, sauf diagnostic demandé
    if (!verbose && pqFailedAt_ && millis() - pqFailedAt_ < 600000) return false;
    std::string host = cfg_.host.c_str();
    if (host.empty()) {
        logEvent("File : aucun WiiM choisi");
        return false;
    }
    size_t colon = host.find(':');
    if (colon != std::string::npos) host.resize(colon);
    // Description de l'appareil : 49152 (WiiM) ou 59152 (autres LinkPlay)
    std::string base, desc;
    for (int port : {49152, 59152}) {
        std::string b = "http://" + host + ":" + std::to_string(port);
        if (upnpRequest(b + "/description.xml", nullptr, "", desc) == 200 && !desc.empty()) {
            base = b;
            break;
        }
    }
    if (base.empty()) {
        logEvent("File : description UPnP introuvable (ports 49152, 59152)");
        pqFailedAt_ = millis() | 1;
        return false;
    }
    auto services = wr::parseUpnpServices(desc);
    const wr::UpnpService* pq = nullptr;
    std::string types;
    for (auto& s : services) {
        size_t c = s.type.rfind(":service:");
        types += (types.empty() ? "" : ", ") + (c == std::string::npos ? s.type : s.type.substr(c + 9));
        if (s.type == kPlayQueue) pq = &s;
    }
    if (verbose) {
        logEvent("File : %s, %u services", base.c_str(), (unsigned)services.size());
        logLong("", types);
    }
    if (!pq) {
        logEvent("File : pas de service PlayQueue sur ce lecteur");
        pqFailedAt_ = millis() | 1;
        return false;
    }
    pqFailedAt_ = 0;
    pqControl_ = control = wr::upnpUrl(base, pq->controlUrl);
    if (verbose) {
        std::string scpd;
        if (upnpRequest(wr::upnpUrl(base, pq->scpdUrl), nullptr, "", scpd) == 200) {
            std::string names;
            for (auto& a : wr::parseScpdActions(scpd)) names += (names.empty() ? "" : " ") + a;
            logEvent("File : actions PlayQueue");
            logLong("", names);
            // Arguments des actions utiles (pour trouver comment préparer une file sans la lancer)
            for (const char* a : {"CreateQueue", "SetQueuePolicy", "SearchQueueOnline", "GetQueueOnline", "BackUpQueue",
                                  "AppendQueue", "BrowseQueueEx"}) {
                std::string list;
                for (auto& g : wr::parseScpdArguments(scpd, a)) list += (list.empty() ? "" : ", ") + g;
                if (!list.empty()) logLong((std::string(a) + "(").c_str(), list + ")");
            }
        }
    }
    return true;
}

int wiimq::browseQueue(const std::string& control, const std::string& name, std::string& ctx) {
    std::string resp;
    bool cut = false;
    int code = upnpRequest(control, "BrowseQueue", wr::soapBody(kPlayQueue, "BrowseQueue", {{"QueueName", name}}), resp,
                           &cut);
    if (cut) logEvent("File : r\xC3\xA9ponse tronqu\xC3\xA9" "e \xC3\xA0 %u Ko", (unsigned)(kMaxBody / 1024));
    std::string raw;
    const bool ok = code == 200 && !cut && wr::xmlElement(resp, "QueueContext", raw);
    std::string().swap(resp);  // réponse libérée avant le décodage : deux copies au plus d'un corps de plusieurs Mo
    ctx = ok ? wr::xmlUnescape(raw) : std::string();
    return code;
}

// Files présentes sur le WiiM : vue d'ensemble « TotalQueue », sinon noms connus
std::vector<std::string> wiimq::queueNames(const std::string& control, bool verbose) {
    std::vector<std::string> names;
    std::string total;
    int code = browseQueue(control, "TotalQueue", total);
    if (code == 200 && !total.empty()) {
        if (verbose) serialDump("BrowseQueue TotalQueue", total, "");
        // Le WiiM liste ses files sous <List1><Name>…</Name> (vu sur un WiiM, 2026-11-16) ; <ListName> par prudence
        for (const char* tag : {"Name", "ListName"})
            for (auto& n : wr::xmlElements(total, tag)) {
                std::string v = wr::xmlUnescape(n);
                if (!v.empty() && v != "TotalQueue" && std::find(names.begin(), names.end(), v) == names.end())
                    names.push_back(v);
            }
    } else if (verbose) {
        logEvent("File : BrowseQueue(TotalQueue) -> %d", code);
    }
    for (const char* n : {"CurrentQueue", "USBDiskQueue", "MyFavouriteQueue", "LocalPlayList"})
        if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
    return names;
}

void WiimWorker::runQueueCheck() {
    logEvent("File du WiiM : recherche du service UPnP\xE2\x80\xA6");
    std::string control;
    if (!findPlayQueue(control, true)) return;
    {  // Services liés au WiiM (noms à donner à GetUserInfo), sans jeton
        std::string resp, result, list;
        const int c = upnpRequest(control, "GetBasicUserInfo", wr::soapBody(kPlayQueue, "GetBasicUserInfo", {}), resp);
        if (c == 200 && wr::xmlElement(resp, "Result", result))
            for (auto& n : wr::jsonValuesOf(wr::xmlUnescape(result), "showName")) list += (list.empty() ? "" : ", ") + n;
        logEvent("File : services li\xC3\xA9s (GetBasicUserInfo %d)", c);
        if (!list.empty()) logLong("", list);
    }
    auto names = queueNames(control, true);
    std::vector<std::string> seen;  // une même file peut répondre sous plusieurs noms
    for (auto& n : names) {
        if (seen.size() >= 4) break;
        std::string ctx;
        if (browseQueue(control, n, ctx) != 200 || ctx.empty()) continue;
        wr::QueueInfo q = wr::parseQueueContext(ctx);
        const std::string key = wr::queueKey(q);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        serialDump(("BrowseQueue " + n).c_str(), ctx, q.user);
        if (q.sourceName == "Qobuz") fav::saveQobuzTemplate(ctx);
        else if (!q.sourceName.empty()) fav::saveSourceModel(q.sourceName, ctx);  // Deezer… pour plus tard
        logEvent("File \xC2\xAB %s \xC2\xBB (nom %s) : source %s, %d piste(s)%s%s",
                 q.listName.empty() ? "?" : q.listName.c_str(), n.c_str(),
                 q.sourceName.empty() ? "?" : q.sourceName.c_str(), q.declaredTracks >= 0 ? q.declaredTracks : q.tracks,
                 q.quality.empty() ? "" : ", qualit\xC3\xA9 ", q.quality.c_str());
        if (!q.searchUrl.empty()) logLong("SearchUrl ", wr::maskSecrets(q.searchUrl));
        if (!q.user.empty()) logEvent("  compte %s", wr::maskAccount(q.user).c_str());
        for (size_t i = 0; i < q.titles.size() && i < 3; ++i) logLong("- ", q.titles[i]);
    }
    if (seen.empty()) logEvent("File : aucune file lisible (lance un album depuis l'app, puis r\xC3\xA9" "essaie)");
    logEvent("File du WiiM : termin\xC3\xA9 (XML complet sur le port s\xC3\xA9rie)");
}

// Crée une file. Sur un WiiM Pro, CreateQueue n'a qu'un argument (QueueContext) et lance toujours la file
// (vérifié 2026-11-12 : aucun « QueuePolicy »).
int wiimq::createQueue(const std::string& control, const std::string& ctx) {
    std::string resp;
    return upnpRequest(control, "CreateQueue", wr::soapBody(kPlayQueue, "CreateQueue", {{"QueueContext", ctx}}), resp);
}

// Lance la file `name` à sa piste `index` (à partir de 1, dans l'ordre relu de la file) ; code HTTP
int wiimq::playQueueAt(const std::string& control, const std::string& name, int index) {
    std::string resp;
    return upnpRequest(control, "PlayQueueWithIndex",
                       wr::soapBody(kPlayQueue, "PlayQueueWithIndex", {{"QueueName", name}, {"Index", std::to_string(index)}}),
                       resp);
}

int wiimq::deleteQueue(const std::string& control, const std::string& name) {
    std::string resp;
    return upnpRequest(control, "DeleteQueue", wr::soapBody(kPlayQueue, "DeleteQueue", {{"QueueName", name}}), resp);
}

// Envoie une file au WiiM puis la lance ; renvoie le code HTTP de PlayQueueWithIndex (-1 si non tenté)
int wiimq::sendQueue(const std::string& control, const std::string& ctx, const std::string& listName, int& create) {
    std::string resp;
    create = createQueue(control, ctx);
    if (create != 200) return -1;
    return playQueueAt(control, listName, 1);
}
