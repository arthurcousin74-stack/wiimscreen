# wiimscreen — télécommande WiiM sur ESP32

*English: [README.md](README.md)*

Firmware libre (MIT) pour cartes ESP32 à écran tactile, qui pilote un streamer WiiM sur le réseau local et affiche ce
qui est en cours de lecture. **Pour l'instant, seul le M5Stack Tab5** (ESP32-P4, écran tactile couleur 5" 1280×720)
**a été testé** : c'est la carte prise en charge, et celle de ce guide. Le portage vers d'autres cartes ESP32 est
décrit dans le [README](README.md#other-esp32-boards) (en anglais).

![Écran principal](docs/apercu.png)

- **En cours de lecture** : grande pochette ; à côté, source et qualité audio en pastilles (« Spotify », « 16 bits · 44,1 kHz », « EN DIRECT » pour une radio), titre, artiste, album, progression, lecture/pause et volume regroupés près de ce qu'ils commandent
- **Couleurs tirées de la pochette** : la lueur du fond, le bouton lecture et les barres prennent la teinte dominante de l'album
- **Presets en tuiles** (image + nom) repris automatiquement du WiiM, par pages de 6 (glisser pour changer de page) ; sans image, des initiales sur une couleur propre à chaque preset. Le preset en cours est entouré et marqué d'une petite icône d'égaliseur
- **Retours visuels** : anneau qui tourne autour du bouton lecture pendant le chargement d'un preset, notifications posées sur la pochette sans cacher le titre, commandes grisées quand le WiiM est injoignable
- **Commande vocale** (facultative, en français) : bouton micro → « Joue FIP », « Mets France Culture », « Plus fort », « Volume 30 », « Suivant ». Tes presets, les commandes de lecture, et n'importe quelle radio de l'annuaire libre Radio Browser
- **Gestes directs** : toucher la barre de volume règle le volume, toucher la barre de progression avance ou recule dans le morceau, toucher le haut-parleur coupe le son
- **Vue ambiance** : après 60 s sans appui pendant la lecture, grande pochette et grands textes, lisibles de loin ; un appui ramène les commandes
- **Visualiseur** (expérimental, à activer dans la configuration) : en vue ambiance, un spectre animé suit la musique, capté par les micros du Tab5. Le son est analysé sur place (24 bandes de fréquence) : rien n'est enregistré ni envoyé. Le micro ne fonctionne qu'en vue ambiance
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
   - ton **Wi-Fi** (2,4 GHz) : choisis-le dans la liste et tape son mot de passe. Astuce Android : **Paramètres > Wi-Fi > ton réseau > Partager** affiche le mot de passe sous le QR code
   - l'**IP du WiiM** : **laisse vide**. Le Tab5 cherche les WiiM sur ton Wi-Fi ; s'il y en a plusieurs, il te demande lequel commander (par leur nom dans l'app WiiM Home). Si son adresse change plus tard, le Tab5 retrouve le même WiiM tout seul
   - les **presets** : laisse vide pour reprendre ceux de l'app WiiM Home (noms et images). Sinon, une ligne par bouton, `numéro=nom` (12 max) :
   ```
   1=Radio Swiss Jazz
   2=Couleur 3
   ```
4. **Enregistrer** : la télécommande redémarre, se connecte et affiche l'interface. Le premier échange enregistre le certificat du WiiM (« Certificat du WiiM enregistré »).

## 4. Commande vocale (facultatif)

![Écoute](docs/voix.png)

La transcription de la voix est faite par OpenAI : le Tab5 seul ne sait pas reconnaître du français libre (le moteur hors ligne d'Espressif ne connaît que des commandes en anglais et en chinois).

1. Crée un compte sur https://platform.openai.com, ajoute quelques dollars de crédit, puis crée une clé API (*API keys* > *Create new secret key*).
2. Ouvre la configuration du Tab5 (bouton ⚙ en bas de la barre de gauche → **Configuration**) et colle la clé dans **Commande vocale : clé API OpenAI**.
3. Un bouton micro apparaît à droite des commandes de lecture.

**Coût** : modèle `gpt-4o-mini-transcribe`, facturé à la durée d'audio (environ 0,3 centime de dollar la minute au moment de l'écriture) ; une phrase dure quelques secondes, soit quelques centimes par mois même en usage quotidien.

**Ce qui est envoyé** : uniquement le son enregistré pendant l'écoute (8 s au plus) et la liste des noms de tes presets (pour aider la transcription), à OpenAI ; puis, pour une radio, le nom cherché à Radio Browser. Le micro est coupé le reste du temps. La clé reste dans la mémoire du Tab5 et n'est jamais réaffichée dans la page de configuration.

**Utilisation** : touche le micro, attends le bip, parle. L'écoute s'arrête toute seule après la phrase (ou touche l'écran) ; « Annuler » abandonne. Pendant l'écoute, la musique est baissée puis remise à son volume.

| Tu dis | Résultat |
|---|---|
| « Joue FIP », « Mets Radio Swiss Jazz », « Preset numéro 3 » | Lance le preset (nom approché accepté) |
| « Mets France Culture », « Je veux écouter Nova » | Cherche la radio dans Radio Browser et la lance sur le WiiM |
| « Pause », « Reprends », « Suivant », « Précédent » | Commandes de lecture |
| « Plus fort », « Moins fort », « Volume 30 », « Coupe le son », « Remets le son » | Volume |

**Reconfigurer plus tard** : bouton ⚙ (barre de gauche), puis **Configuration**.

## 5. Mettre à jour par le Wi-Fi

Une fois ce firmware installé par USB, les versions suivantes s'installent sans câble :

1. Télécharge l'artefact **wiim-remote-tab5** de la dernière exécution (onglet **Actions** de GitHub) et dézippe-le.
2. Sur le Tab5 : bouton ⚙ (barre de gauche) → **Mise à jour**. L'écran affiche une adresse (`http://192.168.x.x`) et un code à 6 chiffres.
3. Sur un ordinateur du même Wi-Fi, ouvre cette adresse, saisis le code, choisis `wiim-remote-tab5.bin`, **Installer**.
4. Une barre de progression s'affiche sur l'ordinateur et sur le Tab5, puis il redémarre sur la nouvelle version (affichée en bas du menu Réglages).

C'est le même fichier que pour le flash USB : le Tab5 n'en garde que la partie utile. Le nouveau firmware est écrit à côté de l'ancien et vérifié avant d'être utilisé : un fichier incomplet, pour un autre appareil ou interrompu en route laisse l'ancien en place. Le mode mise à jour se ferme avec **Annuler**, après 5 codes faux ou au bout de 10 minutes.

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
| Cœur (à gauche de ◀◀) | Ajouter l'album ou la playlist en cours aux favoris (ou l'en retirer) |
| 🏠 en haut de la barre de gauche | Accueil : écran principal (première page de presets) |
| ⚙ en bas de la barre de gauche | Réglages : configuration, mise à jour, test tactile, journal |
| Pochette ou titre, 3 s | Réglages (même menu) |
| Maison (en haut à droite), ou n'importe où, en vue ambiance | Revenir à l'écran principal |
| Soulever / déplacer le Tab5 | Rallumer l'écran, revenir aux commandes |

## En cas de souci

- **Une zone tactile semble décalée** : ⚙ → **Test tactile**. Un rond doit apparaître sous ton doigt ; une photo de cet écran aide beaucoup à corriger.
- **« WiiM injoignable »** : WiiM éteint ou débranché. Si seule son adresse a changé, le Tab5 le retrouve en moins de deux minutes (« WiiM retrouvé à sa nouvelle adresse »).
- **« Aucun WiiM trouvé »** au démarrage : WiiM éteint, ou sur un autre réseau (Wi-Fi invité, autre box). Certains routeurs bloquent la recherche entre appareils (« isolation des clients ») : saisis alors l'IP du WiiM dans la configuration.
- **« Certificat du WiiM inattendu »** : après une réinitialisation ou une mise à jour du WiiM, son certificat peut changer. Si c'est le cas, reconfigure et coche « Oublier le certificat enregistré ». Si rien n'a changé côté WiiM, quelqu'un s'intercale peut-être sur ton réseau : ne l'accepte pas à l'aveugle.
- **« Wi-Fi injoignable »** : touche l'écran pour rouvrir la configuration. Si le Wi-Fi ne démarre jamais, le module Wi-Fi (ESP32-C6) du Tab5 a peut-être besoin de son firmware d'origine : M5Stack fournit un guide de restauration.
- **Pas de pochette** (deux croches à la place) : la source n'en fournit pas (entrée ligne, Bluetooth, certaines radios), ou l'image est dans un format non pris en charge (WebP, JPEG progressif).
- **Le visualiseur reste plat** : le micro capte peu de son (Tab5 loin des enceintes, volume bas). La sensibilité s'adapte en quelques secondes.
- **L'écran se rallume tout seul** : le Tab5 est peut-être posé sur un meuble qui vibre fort ; désactive le réveil par mouvement dans la configuration.
- **« Clé OpenAI refusée »** : clé mal collée ou supprimée → recolle-la dans la configuration. **« Quota OpenAI dépassé »** : plus de crédit sur le compte OpenAI.
- **« Je n'ai rien entendu »** : parle plus près du Tab5 ; si la musique est très forte, baisse-la d'abord.
- **« Aucune radio … »** : la station n'est pas dans Radio Browser sous ce nom ; essaie le nom exact (« RTS Couleur 3 »).
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
src/voice_service.*  transcription OpenAI, recherche Radio Browser (HTTPS vérifié)
src/ota.*            mise à jour par le Wi-Fi (page web + code affiché à l'écran)
src/discovery.*      recherche des WiiM sur le Wi-Fi (SSDP + getStatusEx)
src/fonts/           polices VLW générées (tools/make_vlw.py)
lib/core/            logique pure testée : réponses du WiiM, mise en page, couleur d'accent,
                     spectre (FFT), lissage du visualiseur, détection de mouvement,
                     compréhension des phrases, fin de parole, choix de la radio
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
- Recherche du WiiM : lecture seule (getStatusEx). Quand le Tab5 suit son WiiM à une nouvelle adresse, l'empreinte du certificat enregistrée doit toujours correspondre : un appareil qui se ferait passer pour lui est refusé.
- Mise à jour par le Wi-Fi : uniquement après un geste sur le Tab5 (menu Réglages), avec un code à 6 chiffres affiché seulement à l'écran, 5 essais au plus, 10 minutes au plus ; l'image est vérifiée par l'ESP32 avant d'être activée.
- Clé OpenAI : refusée si elle contient des espaces ou des retours à la ligne (elle va dans un en-tête HTTP) ; jamais réaffichée ni écrite dans le journal.
- Micro : actif uniquement pendant une commande vocale ou en vue ambiance avec le visualiseur activé ; chaque bloc de 32 ms est réduit à 24 niveaux de fréquence puis effacé. Aucun son n'est conservé ni transmis.
- Réponses du WiiM plafonnées à 64 Ko.
- Images (pochettes, presets) : certificat du serveur vérifié (certificats racines Mozilla) pour les adresses HTTPS ; taille plafonnée à 768 Ko ; seuls JPEG et PNG sont décodés, après vérification de leur signature.

## Licence

[MIT](LICENSE) © wiimscreen contributors. Composants tiers : [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Projet indépendant, sans lien avec WiiM / Linkplay ni M5Stack.
