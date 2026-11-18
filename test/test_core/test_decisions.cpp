// Tests: App decisions without hardware (decisions.h) (pio test -e native)
#include <unity.h>

#include <string>
#include <vector>
#include <algorithm>

#include "decisions.h"
#include "upnp.h"
#include "voice.h"
#include "test_helpers.h"

using namespace wr;

static void test_decisions() {
    PlayerStatus now;
    now.playing = true;
    now.title = "B";
    TEST_ASSERT_TRUE(pendingConfirmed(Awaited::Track, now, "A", 0, true));
    now.title = "";
    TEST_ASSERT_FALSE(pendingConfirmed(Awaited::Track, now, "A", 0, true));    // titre manqué : pas un changement
    TEST_ASSERT_FALSE(pendingConfirmed(Awaited::Music, now, "A", 0, true));
    now.title = "A";
    now.positionMs = 1000;
    TEST_ASSERT_TRUE(pendingConfirmed(Awaited::Track, now, "A", 60000, true));  // « précédent » : morceau repris
    TEST_ASSERT_TRUE(pendingConfirmed(Awaited::Playing, now, "A", 0, true));
    TEST_ASSERT_FALSE(pendingConfirmed(Awaited::Playing, now, "A", 0, false));
    now.title = "B";
    now.loading = true;
    TEST_ASSERT_FALSE(pendingConfirmed(Awaited::Music, now, "A", 0, true));      // encore en chargement
    TEST_ASSERT_FALSE(pendingConfirmed(Awaited::None, now, "A", 0, true));

    PlaybackBefore before;
    PlayerStatus after;
    TEST_ASSERT_EQUAL((int)RestoreAction::Nothing, (int)restoreAction(before, after));
    after.title = "Search result";  // rien ne jouait ; le WiiM est en pause sur la file de recherche
    TEST_ASSERT_EQUAL((int)RestoreAction::StopSearchQueue, (int)restoreAction(before, after));
    after.playing = true;
    TEST_ASSERT_EQUAL((int)RestoreAction::Stop, (int)restoreAction(before, after));
    before.playing = true;
    before.title = "Song";
    before.posMs = 90000;
    TEST_ASSERT_EQUAL((int)RestoreAction::Pause, (int)restoreAction(before, after));  // hors file (AirPlay…)
    before.inQueue = true;
    TEST_ASSERT_EQUAL((int)RestoreAction::PutBack, (int)restoreAction(before, after));
    after.title = "Song";
    after.positionMs = 92000;
    TEST_ASSERT_EQUAL((int)RestoreAction::Nothing, (int)restoreAction(before, after));
    after.positionMs = 2000;  // même morceau, reparti du début : la recherche l'a relancé
    TEST_ASSERT_EQUAL((int)RestoreAction::PutBack, (int)restoreAction(before, after));

    // En pause dans une file connue (vu sur un WiiM, 2026-11-17 : le morceau en pause était perdu)
    PlaybackBefore paused;
    paused.title = "Song";
    paused.posMs = 240000;
    paused.inQueue = true;
    PlayerStatus searchQ;
    searchQ.title = "Search result";
    TEST_ASSERT_EQUAL((int)RestoreAction::PutBackPaused, (int)restoreAction(paused, searchQ));
    searchQ.playing = true;
    TEST_ASSERT_EQUAL((int)RestoreAction::PutBackPaused, (int)restoreAction(paused, searchQ));
    PlayerStatus same;
    same.title = "Song";
    TEST_ASSERT_EQUAL((int)RestoreAction::Nothing, (int)restoreAction(paused, same));  // rien n'a bougé
    paused.inQueue = false;  // hors file : rien à remettre
    searchQ.playing = false;
    TEST_ASSERT_EQUAL((int)RestoreAction::StopSearchQueue, (int)restoreAction(paused, searchQ));

    // Écriture suivante de la recherche : file vide = une seule autre écriture, et seulement avec plusieurs mots
    TEST_ASSERT_FALSE(searchTryNext(true, true, 0, true));
    TEST_ASSERT_TRUE(searchTryNext(true, false, 0, false));   // résultats sans rapport : requête peut-être mal lue
    TEST_ASSERT_FALSE(searchTryNext(false, false, 1, false));  // un mot, file vide : inutile d'insister (43 s sinon)
    TEST_ASSERT_TRUE(searchTryNext(false, false, 1, true));
    TEST_ASSERT_FALSE(searchTryNext(false, false, 2, true));

    SearchUndo undo;
    TEST_ASSERT_FALSE(parseSearchUndo("", undo));
    TEST_ASSERT_FALSE(parseSearchUndo("n'importe quoi", undo));
    TEST_ASSERT_FALSE(parseSearchUndo(searchUndoText(SearchUndo{}), undo));  // rien à remettre
    SearchUndo both;
    both.unmute = true;
    both.loop = 3;
    TEST_ASSERT_TRUE(parseSearchUndo(searchUndoText(both), undo));
    TEST_ASSERT_TRUE(undo.unmute);
    TEST_ASSERT_EQUAL(3, undo.loop);
    TEST_ASSERT_TRUE(parseSearchUndo("unmute 0 loop 2\n", undo));
    TEST_ASSERT_FALSE(undo.unmute);
    TEST_ASSERT_EQUAL(2, undo.loop);
    TEST_ASSERT_TRUE(parseSearchUndo("unmute 1 loop 99", undo));  // mode inconnu : le son seulement
    TEST_ASSERT_EQUAL(-1, undo.loop);

    TEST_ASSERT_EQUAL(4, loopWithoutShuffle(3));  // aléatoire sans répétition (vu sur un WiiM, 2026-11-17)
    TEST_ASSERT_EQUAL(0, loopWithoutShuffle(2));
    TEST_ASSERT_EQUAL(1, loopWithoutShuffle(5));
    TEST_ASSERT_EQUAL(-1, loopWithoutShuffle(4));
    TEST_ASSERT_EQUAL(-1, loopWithoutShuffle(-1));

    QueueStability st;
    TEST_ASSERT_FALSE(st.feed(0, 700));
    TEST_ASSERT_FALSE(st.feed(2, 1400));
    for (int k = 0; k < 4; ++k) TEST_ASSERT_FALSE(st.feed(7, 2100 + 700 * k));
    TEST_ASSERT_TRUE(st.feed(7, 4900));  // 4 répétitions du même nombre, plus de 3 s
    QueueStability quick;
    for (int k = 0; k < 4; ++k) TEST_ASSERT_FALSE(quick.feed(3, 100 * k));  // trop tôt
    TEST_ASSERT_TRUE(quick.feed(3, 3000));

    QueueInfo q;
    q.items = {{"So What", "", "", "", ""}, {"Blue in Green", "", "", "", ""}};
    TEST_ASSERT_EQUAL(2, queueIndexOf(q, "Blue in Green", false));
    TEST_ASSERT_EQUAL(0, queueIndexOf(q, "blue in green", false));
    TEST_ASSERT_EQUAL(2, queueIndexOf(q, "BLUE IN GREEN!", true));
    TEST_ASSERT_EQUAL(0, queueIndexOf(q, "", true));

    // File longue : au-delà de kQueueItems morceaux, la place se retrouve par les titres seuls
    std::string longXml = "<PlayList><ListName>L</ListName><Tracks>";
    for (int n = 1; n <= 450; ++n)
        longXml += "<Track" + std::to_string(n) + "><Metadata>&lt;DIDL-Lite&gt;&lt;item&gt;&lt;dc:title&gt;" +
                   (n == 300 ? std::string() : "Titre " + std::to_string(n)) +
                   "&lt;/dc:title&gt;&lt;/item&gt;&lt;/DIDL-Lite&gt;</Metadata></Track" + std::to_string(n) + ">";
    longXml += "</Tracks></PlayList>";
    const QueueInfo shortRead = parseQueueContext(longXml);
    TEST_ASSERT_EQUAL(450, shortRead.tracks);
    TEST_ASSERT_EQUAL(200, (int)shortRead.items.size());
    TEST_ASSERT_EQUAL(0, (int)shortRead.moreTitles.size());
    TEST_ASSERT_EQUAL(0, queueIndexOf(shortRead, "Titre 350", false));
    const QueueInfo longRead = parseQueueContext(longXml, kQueueTitlesMax);
    TEST_ASSERT_EQUAL(200, (int)longRead.items.size());
    TEST_ASSERT_EQUAL(250, (int)longRead.moreTitles.size());
    TEST_ASSERT_EQUAL(150, queueIndexOf(longRead, "Titre 150", false));
    TEST_ASSERT_EQUAL(201, queueIndexOf(longRead, "Titre 201", false));
    TEST_ASSERT_EQUAL(350, queueIndexOf(longRead, "titre 350", true));  // un titre vide (300) garde les places
    TEST_ASSERT_EQUAL(450, queueIndexOf(longRead, "Titre 450", false));
    TEST_ASSERT_EQUAL(210, (int)parseQueueContext(longXml, 210).moreTitles.size() + 200);

    TEST_ASSERT_EQUAL_STRING("Miles Davis", cleanTranscript(" \"Miles Davis.\" ").c_str());
    TEST_ASSERT_EQUAL_STRING("caf", cleanTranscript("caf\xC3\xA9", 4).c_str());  // pas de demi-caractère
    TEST_ASSERT_EQUAL_STRING("", cleanTranscript("...").c_str());
}

static void test_restore_stopped() {
    // La recherche a arrêté la musique (DeleteQueue) et sa file est restée vide : remise si la file est connue
    PlaybackBefore before;
    before.playing = true;
    before.title = "Song";
    before.posMs = 60000;
    before.inQueue = true;
    PlayerStatus now;  // arrêt, pas de titre
    TEST_ASSERT_EQUAL((int)RestoreAction::PutBack, (int)restoreAction(before, now));
    now.title = "Song";  // en pause sur le même morceau
    TEST_ASSERT_EQUAL((int)RestoreAction::PutBack, (int)restoreAction(before, now));
    before.inQueue = false;  // AirPlay, Spotify… : impossible à relancer d'ici
    TEST_ASSERT_EQUAL((int)RestoreAction::Nothing, (int)restoreAction(before, now));
    now.title = "";
    TEST_ASSERT_EQUAL((int)RestoreAction::Nothing, (int)restoreAction(before, now));
}

void run_decisions_tests() {
    RUN_TEST(test_decisions);
    RUN_TEST(test_restore_stopped);
}
