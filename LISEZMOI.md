# Télécommande WiiM pour M5Stack Tab5

Firmware pour le M5Stack Tab5 (ESP32-P4, écran tactile couleur 5" 1280×720) qui pilote un streamer WiiM sur le réseau local.

![Écran principal](docs/apercu.png)

- **En cours de lecture** : grande pochette ; à côté, source et qualité audio en pastilles (« Spotify », « 16 bits · 44,1 kHz », « EN DIRECT » pour une radio), titre, artiste, album, progression, lecture/pause et volume regroupés près de ce qu'ils commandent
- **Couleurs tirées de la pochette** : la lueur du fond, le bouton lecture et les barres prennent la teinte dominante de l'album
- **Presets en tuiles** (image + nom) repris automatiquement du WiiM, par pages de 6 (glisser pour changer de page) ; sans image, des initiales sur une couleur propre à chaque preset. Le preset en cours est entouré et marqué d'une petite icône d'égaliseur
- **Retours visuels** : anneau qui tourne autour du bouton lecture pendant le chargement d'un preset, notifications posées sur la pochette sans cacher le titre, commandes grisées quand le WiiM est injoignable
- **Gestes directs** : toucher la barre de volume règle le volume, toucher la barre de progression avance ou recule dans le morceau, toucher le haut-parleur coupe le son
- **Vue ambiance** : après 30 s sans appui pendant la lecture, grande pochette et grands textes, lisibles de loin ; un appui ramène les commandes
- **Visualiseur** : en vue ambiance, un spectre animé suit la musique, capté par les micros du Tab5. Le son est analysé sur place (24 bandes de fréquence) : rien n'est enregistré ni envoyé. Le micro ne fonctionne qu'en vue ambiance
- **Réveil par mouvement** : soulever ou déplacer le Tab5 rallume l'écran et affiche les commandes ; les petites vibrations (enceinte sur le même meuble) sont ignorées

  ![Vue ambiance](docs/ambiance.png)

- **Économie d'écran** : atténué après 10 min sans appui, éteint après 20 min si rien ne joue ; un appui le rallume
- **Sons** par le haut-parleur : un clic à chaque appui, deux notes graves si le WiiM ne répond pas, deux notes aiguës quand l'appui long ouvre la configuration (réglable ou désactivé)
- **Aucune appli à installer** : tout se règle depuis un téléphone, via un portail Wi-Fi affiché à l'écran (avec QR code)
- **API locale du WiiM** (`httpapi.asp`), aucun cloud
- **TLS épinglé** : le certificat auto-signé du WiiM est enregistré à la première connexion, puis comparé (SHA-256) avant chaque requête
- **Réglages conservés** lors des mises à jour du firmware (mémoire NVS)

## 1. Obtenir le firmware

La compilation se lance toute seule à chaque envoi sur GitHub :
onglet **Actions** > dernière exécution > artefact **wiim-remote-tab5**. Tu y trouves `wiim-remote-tab5.bin`.

## 2. Flasher depuis le navigateur, sans rien installer

1. Ouvre **Chrome ou Edge** (Firefox ne gère pas le port série web) sur https://espressif.github.io/esptool-js/
2. Branche le Tab5 en USB-C, puis passe-le en **mode téléchargement** : appui long (~2 s) sur le bouton d'alimentation/reset jusqu'à ce que la LED **verte clignote rapidement**.
3. Clique **Connect** et choisis le port qui apparaît.
4. Dans *Flash Address* mets **0x0**, choisis `wiim-remote-tab5.bin`, clique **Program**.
5. À la fin, appuie brièvement sur le bouton reset pour redémarrer.

Pas de batterie ? Le Tab5 fonctionne très bien branché en USB-C. La batterie (type Sony NP-F550, 7,4 V) est vendue à part ; sans elle, l'indicateur de batterie n'apparaît pas.

## 3. Configurer avec ton téléphone

Au premier démarrage, l'écran affiche « Configuration » avec un QR code.

1. Scanne le QR code (ou rejoins le Wi-Fi `WiiM-Remote-XXXX` avec le mot de passe affiché).
2. La page de configuration s'ouvre ; sinon va sur **http://192.168.4.1**.
3. Renseigne :
   - ton **Wi-Fi** (2,4 GHz)
   - l'**IP du WiiM** : app WiiM Home > ton appareil > Paramètres > Informations (pense à la réserver dans ta box)
   - les **presets** : laisse vide pour reprendre ceux de l'app WiiM Home (noms et images). Sinon, une ligne par bouton, `numéro=nom` (12 max) :
   ```
   1=Radio Swiss Jazz
   2=Couleur 3
   ```
4. **Enregistrer** : la télécommande redémarre, se connecte et affiche l'interface. Le premier échange enregistre le certificat du WiiM (« Certificat du WiiM enregistré »).

**Reconfigurer plus tard** : appui long (3 s) sur la pochette ou le titre.

## Utilisation

| Zone | Action |
|---|---|
| Barre de progression | Avancer / reculer dans le morceau |
| Barre de volume | Régler le volume directement |
| Haut-parleur (à gauche du volume) | Couper / rétablir le son |
| Tuiles du bas | Lancer le preset (au relâcher) |
| Glisser sur les tuiles, ou toucher les points | Page de presets suivante / précédente |
| − / + (bouts de la barre de volume) | Volume (pas réglable) |
| ◀◀ / ▶▶ | Piste précédente / suivante |
| ▶ / II | Lecture / pause |
| Pochette ou titre, 3 s | Configuration |
| N'importe où, en vue ambiance | Revenir aux commandes |
| Soulever / déplacer le Tab5 | Rallumer l'écran, revenir aux commandes |

## En cas de souci

- **« WiiM injoignable »** : WiiM éteint, ou son IP a changé → appui long sur le bandeau, corrige l'IP.
- **« Certificat du WiiM inattendu »** : après une réinitialisation ou une mise à jour du WiiM, son certificat peut changer. Si c'est le cas, reconfigure et coche « Oublier le certificat enregistré ». Si rien n'a changé côté WiiM, quelqu'un s'intercale peut-être sur ton réseau : ne l'accepte pas à l'aveugle.
- **« Wi-Fi injoignable »** : touche l'écran pour rouvrir la configuration. Si le Wi-Fi ne démarre jamais, le module Wi-Fi (ESP32-C6) du Tab5 a peut-être besoin de son firmware d'origine : M5Stack fournit un guide de restauration.
- **Pas de pochette** (deux croches à la place) : la source n'en fournit pas (entrée ligne, Bluetooth, certaines radios), ou l'image est dans un format non pris en charge (WebP, JPEG progressif).
- **Le visualiseur reste plat** : le micro capte peu de son (Tab5 loin des enceintes, volume bas). La sensibilité s'adapte en quelques secondes.
- **L'écran se rallume tout seul** : le Tab5 est peut-être posé sur un meuble qui vibre fort ; désactive le réveil par mouvement dans la configuration.
- **Journal** : moniteur série à 115200 bauds (`pio device monitor`).

## Développement

```bash
pip install platformio "click<8.2"   # esptool de la plateforme pioarduino exige click < 8.2
pio test -e native                   # tests de la logique sur PC
pio run -e tab5                      # compilation
pio run -e tab5 -t upload
```

### Aperçu de l'interface sur PC

Pour retoucher l'interface sans l'appareil : le vrai `src/ui.cpp` est compilé pour le PC (M5GFX + SDL2) et les écrans sont enregistrés en PNG.

```bash
sudo apt install libsdl2-dev          # Linux ; macOS : brew install sdl2
pio run -e preview
# images/ : cover.jpg, et preset1..6 en .jpg ou .png (facultatifs)
SDL_VIDEODRIVER=dummy .pio/build/preview/program images/ apercu/
python tools/preview/topng.py apercu/*.ppm
```

```
src/main.cpp         démarrage, boucle, appuis, vue ambiance, rétroéclairage
src/ui.*             dessin (canevas hors écran), couleur d'accent, mise en page
src/art.*            téléchargement des images (tâche de fond, HTTPS vérifié)
src/wiim_client.*    API WiiM, TLS avec épinglage
src/portal.*         portail de configuration (point d'accès + DNS + formulaire)
src/settings.*       réglages en NVS
src/sound.*          sons (M5.Speaker) ; le bus audio est partagé avec le micro
src/fonts/           polices VLW générées (tools/make_vlw.py)
lib/core/            logique pure testée : réponses du WiiM, mise en page, couleur d'accent,
                     spectre (FFT), lissage du visualiseur, détection de mouvement
certs/               certificats racines Mozilla (images en HTTPS)
tools/preview/       aperçu de l'interface sur PC
```

Régénérer les polices :
```bash
python tools/make_vlw.py fonts/DejaVuSans.ttf 24 font_ui_24 src/fonts/font_ui_24.h
python tools/make_vlw.py fonts/DejaVuSans.ttf 28 font_ui_28 src/fonts/font_ui_28.h
python tools/make_vlw.py fonts/DejaVuSans.ttf 40 font_ui_40 src/fonts/font_ui_40.h
python tools/make_vlw.py fonts/DejaVuSans.ttf 56 font_ui_56 src/fonts/font_ui_56.h
```

Mettre à jour les certificats racines (une fois par an environ) :
```bash
curl -o certs/cacrt_all.pem https://raw.githubusercontent.com/espressif/esp-idf/master/components/mbedtls/esp_crt_bundle/cacrt_all.pem
python tools/make_ca_bundle.py certs/cacrt_all.pem certs/x509_crt_bundle.bin
```

## Notes de sécurité

- Le Wi-Fi de configuration est protégé par un mot de passe aléatoire de 10 caractères (générateur matériel), affiché uniquement sur l'écran. Il n'existe que pendant la configuration.
- Le mot de passe Wi-Fi n'est jamais renvoyé dans la page de configuration.
- Toutes les valeurs réaffichées dans la page sont échappées (HTML).
- Micro : actif uniquement en vue ambiance avec le visualiseur activé ; chaque bloc de 32 ms est réduit à 24 niveaux de fréquence puis effacé. Aucun son n'est conservé ni transmis.
- Réponses du WiiM plafonnées à 64 Ko.
- Images (pochettes, presets) : certificat du serveur vérifié (certificats racines Mozilla) pour les adresses HTTPS ; taille plafonnée à 768 Ko ; seuls JPEG et PNG sont décodés, après vérification de leur signature.
