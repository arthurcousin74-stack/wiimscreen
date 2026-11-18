#pragma once

#include <Arduino.h>

#include <deque>
#include <vector>

#include "core.h"
#include "settings.h"
#include "qobuz.h"
#include "upnp.h"
#include "voice.h"
#include "wiim_client.h"

// Tout le réseau avec le WiiM passe par une tâche de fond (cœur du Wi-Fi) : chaque
// requête ouvre une connexion TLS (1 à 3 s sur l'ESP32-P4), ce qui figeait l'écran
// et faisait manquer des appuis quand c'était fait dans la boucle principale.
//
// La boucle principale ne bloque jamais : elle envoie des commandes (file d'attente),
// lit le dernier état relevé et les évènements (erreurs, presets, certificat…).

// État relevé
struct WiimReport {
    WiimError error = WiimError::None;
    bool online = false;
    wr::PlayerStatus player;  // titre/artiste/album complétés par getMetaInfo si metaOk
    wr::TrackMeta meta;
    bool metaOk = false;      // getMetaInfo lu à ce relevé (sinon garder le précédent)
    uint32_t at = 0;          // millis() à la fin du relevé
    uint32_t cmdSeq = 0;      // commandes déjà exécutées quand le relevé a commencé
    std::string queueKey;     // clé (wr::queueKey) de la file qui joue, vide si inconnue : le ♥ reconnaît un favori
};

// Liste demandée à la tâche WiiM (recherche ou favoris)
enum class SearchKind {
    QobuzTracks,      // morceaux Qobuz : cherchés par le WiiM (file), ou par le Tab5 avec l'option d'accès direct
    Deezer,           // morceaux Deezer : cherchés par le Tab5 (api.deezer.com), joués par le WiiM (album, airable)
    QobuzFavorites,   // morceaux favoris du compte Qobuz
    DeezerFavorites,  // morceaux favoris Deezer (profil indiqué dans les réglages)
    QobuzAlbums,      // albums Qobuz (accès direct ; `albums` de SearchDone)
};

struct WiimEvent {
    enum Kind { CommandDone, Presets, Pinned, PinMismatch, Moved, Identity, FavSaved, FavPlayed, SearchDone, QobuzFavorites, DeezerFavorites };
    Kind kind = CommandDone;
    WiimError error = WiimError::None;
    String text;  // commande ; empreinte vue (PinMismatch) ; nouvelle adresse (Moved) ; favori ou erreur
    bool ok = false;
    std::vector<wr::Preset> presets;
    String uuid, name;  // Identity ; name : texte cherché (SearchDone)
    std::vector<wr::QueueTrack> tracks;  // SearchDone : résultats
    bool automatic = false;              // FavSaved : tuile ajoutée par la tâche (Flow), pas par un appui
    std::vector<wr::QobuzAlbum> albums;  // QobuzFavorites / DeezerFavorites : albums favoris du compte
};

class WiimWorker {
public:
    // Démarre la tâche (copie de l'adresse, de l'uuid et de l'empreinte du WiiM)
    void begin(const Settings& s);

    // Commande à envoyer (non bloquant). Les changements de volume successifs encore
    // en attente sont fusionnés : seul le dernier part.
    void command(const String& cmd);
    void refreshPresets();
    void pollSoon();
    // Diagnostic : lit la file de lecture du WiiM (service UPnP PlayQueue, non documenté)
    // et écrit un résumé dans le journal ; le XML complet (secrets masqués) part sur le port série.
    void checkQueue();
    // Favoris : copie la file du WiiM qui contient le morceau `title` (évènement FavSaved) ;
    // rejoue un favori enregistré (évènement FavPlayed). Non bloquant.
    void saveFavorite(const String& title, const String& artist, const String& album, const String& artUrl);
    void playFavorite(const String& id);
    // Recherche (évènement SearchDone avec les morceaux trouvés), puis lecture d'un résultat (0 pour le
    // premier). Rien ne joue avant le choix. Non bloquant.
    void search(const String& query, SearchKind kind);
    // Album Qobuz enregistré comme favori (tuile) sans le jouer : file de l'album, le WiiM lira les pistes.
    // Évènement FavSaved. Non bloquant.
    void saveAlbumFavorite(const String& id, const String& title, const String& artist, const String& image);
    // Albums favoris du compte Qobuz (évènement QobuzFavorites), lus avec l'accès donné par le WiiM
    void refreshQobuzFavorites();
    void playSearchResult(int index);

    // Thread principal : dernier relevé (s'il est nouveau) et évènements
    bool takeReport(WiimReport& out);
    bool takeEvent(WiimEvent& out);
    uint32_t queued() const { return queued_; }  // commandes envoyées depuis le début
    // Place libre minimale sur la pile de la tâche (octets), pour le journal
    uint32_t stackLeft() const { return task_ ? uxTaskGetStackHighWaterMark(task_) * sizeof(StackType_t) : 0; }
    // Attente dans une tâche longue de la tâche WiiM : commandes (et relevé si `poll`) traitées pendant ce temps
    void pause(uint32_t ms, bool poll);

private:
    static void taskMain(void* self);
    void run();
    void execute(const String& cmd);
    void drainCommands();
    void pollOnce();
    bool rediscover();
    void learnIdentity();
    void runQueueCheck();
    struct FavRequest {
        bool wanted = false;
        String title, artist, album, artUrl;
    };
    bool findPlayQueue(std::string& control, bool verbose);
    void runSaveFavorite(const FavRequest& r);
    void liveFavorite(const wr::QueueInfo& q, std::string& ctx, wr::Favorite& f);
    void autoFlowTile(const wr::QueueInfo& q, const std::string& current);
    struct AlbumFavRequest {
        bool wanted = false;
        String id, title, artist, image;
    };
    AlbumFavRequest albumSave_;
    void runSaveAlbumFavorite(const AlbumFavRequest& r);
    void runQobuzAlbumSearch(const String& query);
    std::vector<wr::QobuzAlbum> qobuzAlbums_;  // ResultsFrom::QobuzAlbums : albums trouvés
    void runPlayFavorite(const String& id);
    void runQobuzFavoriteList(const String& title);
    bool playAlbumAt(const std::string& control, const std::string& templ, const std::string& url,
                     const std::string& title, const char* label);
    bool qobuzAccess(const std::string& control, bool refresh);
    int qobuzGet(const std::string& control, const std::string& path, std::string& out);
    void runQobuzFavorites();
    void runPlayQobuzAlbum(const std::string& albumId);
    void runDeezerFavorites();
    void runDeezerFavoriteList(const String& title);
    std::string deezerUserPath() const;  // « user/<n> » (profil configuré) ou vide
    void runPlayDeezerAlbum(const std::string& albumId);
    void runSearch(const String& query);
    void runPlaySearchResult(int index);
    bool qobuzModel(const std::string& control, std::string& templ);
    struct Playback {
        bool known = false, playing = false, muted = false;
        std::string title, ctx, listName;  // morceau en cours ; file qui le contient (si trouvée)
        int index = 0;                      // place du morceau dans cette file (1 = premier), 0 si inconnue
        uint32_t posMs = 0;
        int loop = -1;  // mode de répétition / lecture aléatoire avant la recherche
    };
    Playback snapshot(const std::string& control);
    bool shuffleOff(const Playback& before);  // pendant une recherche par file : sinon le WiiM mélange les résultats
    void shuffleBack(const Playback& before, bool changed);
    void noteSearchChanges(const Playback& before, bool muted, bool unshuffled);  // gardé si le Tab5 redémarre
    void undoInterruptedSearch(const wr::PlayerStatus& now);
    bool undoChecked_ = false;
    void restoreAfterSearch(const std::string& control, const Playback& before);
    bool deezerModel(const std::string& control, std::string& templ);
    void runDeezerSearch(const String& query);
    void runPlayDeezer(int index);
    void push(WiimEvent&& e);
    void notePinning(WiimError e);
    bool commandsWaiting();

    WiimClient client_;
    Settings cfg_;  // copie propre à la tâche (hôte, uuid, https, empreinte)
    SemaphoreHandle_t lock_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::deque<String> cmds_;
    uint32_t queued_ = 0;    // écrit par le thread principal seulement
    uint32_t executed_ = 0;  // sous verrou
    bool presetsWanted_ = false;
    bool pollWanted_ = false;
    bool queueWanted_ = false;
    FavRequest favSave_;
    String favPlay_;
    String search_;
    int searchPlay_ = 0;
    int searchCount_ = 0;
    SearchKind searchReq_ = SearchKind::QobuzTracks;  // recherche demandée
    // D'où viennent les derniers résultats, donc comment jouer l'un d'eux
    enum class ResultsFrom { WiimQueue, Deezer, QobuzDirect, QobuzAlbums };
    ResultsFrom resultsFrom_ = ResultsFrom::WiimQueue;
    std::vector<wr::DeezerTrack> deezerResults_;
    std::vector<wr::QobuzTrack> qobuzResults_;  // ResultsFrom::QobuzDirect : recherche Qobuz directe
    wr::QobuzAccess qobuz_;                      // identifiant d'application et jeton (mémoire seulement)
    bool haveQobuz_ = false;
    bool qobuzFavsWanted_ = false;
    bool qobuzFavDenied_ = false;  // Qobuz refuse l'accès du WiiM aux favoris (401) : liste par une file du WiiM
    std::string queueTitle_, queueKey_;  // file en cours relue à chaque changement de morceau (et toutes les 60 s)
    uint32_t queueReadAt_ = 0;
    std::string searchQueue_;  // file des derniers résultats affichés
    std::string searchUrl_;    // son adresse (SearchUrl), pour la recréer au moment du choix
    std::vector<std::string> searchTitles_;  // titres des résultats affichés, dans l'ordre
    std::vector<std::string> searchAlbums_;  // album Qobuz de chacun (song:albumid), recherche par file du WiiM
    std::string pqControl_;  // URL de contrôle du service PlayQueue (trouvée une fois)
    bool hasReport_ = false;
    WiimReport report_;
    std::deque<WiimEvent> events_;
    uint32_t nextPoll_ = 0;
    int netFailures_ = 0;
    uint32_t lastRediscover_ = 0;
    uint32_t identityTriedAt_ = 0;  // getStatusEx demandé (0 = jamais) ; nouvel essai après 5 min si échec
    uint32_t pqFailedAt_ = 0;       // service PlayQueue introuvable à cet instant (0 = pas d'échec récent)
    uint32_t modelScanAt_ = 0;      // dernière recherche des modèles de file Qobuz/Deezer dans les files du WiiM
    WiimError lastPollError_ = WiimError::None;
    bool polledOnce_ = false;
};

// Nom court d'une erreur, pour le journal
const char* wiimErrorName(WiimError e);
