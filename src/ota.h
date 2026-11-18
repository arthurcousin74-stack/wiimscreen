#pragma once

#include "ui.h"

// Mise à jour par le Wi-Fi, depuis un navigateur du réseau local.
//
// Le Tab5 affiche son adresse et un code à 6 chiffres ; la page web demande ce
// code et le fichier du firmware (l'image complète de la CI ou l'application
// seule). Le firmware est écrit dans la partition inactive puis vérifié par
// l'ESP32 ; s'il est incomplet ou invalide, l'ancien reste en place.
// Sortie : Annuler sur l'écran, ou 10 minutes sans mise à jour (un code faux ne fait pas sortir : il retarde
// l'essai suivant, d'une attente qui double à chaque code faux).
// Ne revient pas après une mise à jour réussie (redémarrage).
void runUpdateMode(Ui& ui);
