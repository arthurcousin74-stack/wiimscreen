#pragma once

// Retours sonores par le haut-parleur du Tab5 (M5.Speaker).
// Le clic confirme l'appui même quand la commande au WiiM prend un instant.
// Tous les sons sont joués en tâche de fond (non bloquants).
namespace sound {

// niveau : 0 désactivé, 1 discret, 2 normal, 3 fort
void begin(int level);
void tap();      // clic court à chaque appui
void error();    // deux notes descendantes : le WiiM n'a pas répondu ou a refusé
void confirm();  // deux notes montantes : appui long reconnu (configuration)

}  // namespace sound
