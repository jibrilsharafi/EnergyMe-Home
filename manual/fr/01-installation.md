# Installation

> Lisez le [préambule de sécurité](README.md#before-you-start-read-this-page) avant de commencer.

---

## 1. Contenu de la boîte {#1-whats-in-the-box}

### 1.1 Kit de démarrage (toujours inclus) {#11-starter-kit-always-included}

| # | Article | Qté | Notes |
| --- | --- | --- | --- |
| 1 | Appareil *EnergyMe Home* | 1 | Préassemblé, montage sur rail DIN 3 modules, avec fils L/N déjà raccordés. **Les canaux sont numérotés de 0 à 15 sur les autocollants en façade** (16 canaux au total, 4 en haut et 12 en bas) |
| 2 | Pinces ampèremétriques (30 A, câble 1 m, prise jack 3,5 mm) | 4 | Toutes identiques ; le calibre « 30 A » est imprimé sur le corps de chaque pince. 1 sera utilisée pour le **Channel 0** (ligne principale) et 3 pour les circuits dérivés |

Vous trouverez également un **autocollant « Let's get started » avec un QR code** placé à l'intérieur du couvercle de la boîte.

> *Et si vous lisez ces instructions : bien joué, vous avez déjà trouvé le QR code. 🎯*

### 1.2 Kit d'extension (optionnel, si commandé) {#12-expansion-kit-optional-if-ordered}

Si vous avez commandé des TC supplémentaires pour surveiller plus de 3 circuits dérivés, vous les trouverez dans un sachet séparé dans la boîte. L'appareil supporte jusqu'à **16 canaux au total** (Channel 0 pour le principal et Channels 1 à 15 pour les dérivés).

> **ⓘ NOTE : À propos des TC**  
> Les TC standard sont **calibrés 30 A** (imprimé sur chaque pince). Ils fonctionnent sans problème pour les **installations monophasées jusqu'à ~7 kW à 230 V, ou ~3,5 kW à 120 V**.
>
> Avant d'installer une pince, vérifiez le calibre de courant imprimé sur le disjoncteur que le TC va surveiller ; il ne doit pas dépasser le calibre du TC. Si vous avez des circuits tirant un courant supérieur (alimentation électrique, pompe à chaleur), ou une alimentation triphasée, vous aurez peut-être besoin de TC de calibre supérieur (75 A ou 150 A). Contactez-nous à `support@energyme.net` pour les commander.

> **ⓘ NOTE : Fréquence du réseau**  
> *EnergyMe Home* fonctionne sur les réseaux **50 Hz** (Europe, Asie, Afrique, Océanie) et **60 Hz** (Amérique du Nord, certaines parties de l'Amérique du Sud et du Japon) sans aucune configuration nécessaire.

> **ⓘ NOTE : S'il manque quelque chose ou si un article est endommagé**  
> N'installez pas l'appareil. Contactez le support à `support@energyme.net` avec une photo du contenu de la boîte.

> **⚠ Charges triphasées : lisez ceci avant de continuer**  
> Si votre alimentation principale est **triphasée**, ou si vous avez des **charges triphasées spécifiques** à surveiller (par exemple, une borne de recharge VE triphasée), le câblage est légèrement différent. **Lisez [Annexe B](appendices.md#appendix-b-three-phase-configuration) avant de commencer l'installation.**

---

## 2. Ce dont vous avez besoin (non inclus) {#2-what-you-need-not-included}

**L'électricien aura besoin de :**

- Tournevis isolé
- **3 modules DIN contigus libres** dans votre tableau électrique

**Vous aurez besoin de :**

- Un smartphone, une tablette ou un ordinateur portable avec Wi-Fi (2,4 GHz)
- Le nom (SSID) et le mot de passe de votre réseau Wi-Fi domestique

---

## 3. Installation électrique {#3-electrical-installation}

> **Avant d'ouvrir le tableau :**
>
> 1. Ouvrez le couvercle du tableau et identifiez visuellement **3 modules DIN contigus libres** pour l'appareil.
> 2. **Coupez le disjoncteur général.**
>
> À partir de ce point, le travail est réellement simple ; la plupart des installateurs le terminent en **10 à 15 minutes**.

> **⚠ AVERTISSEMENT : À partir de cette section, le travail doit être effectué par un électricien qualifié.**  
> Vérifiez l'absence de tension avec un testeur sur chaque conducteur que vous allez toucher.

### 3.1 Monter l'appareil sur le rail DIN {#31-mount-the-device-on-the-din-rail}

1. Dans l'espace de 3 modules que vous avez identifié, accrochez d'abord le **haut** de l'appareil sur le rail.
2. Poussez le **bas** jusqu'à ce que le clip à ressort s'enclenche.
3. Tirez doucement vers le bas pour vérifier que l'appareil est bien verrouillé.

### 3.2 Connecter l'alimentation (L et N) {#32-connect-the-power-supply-l-and-n}

L'appareil est livré avec **deux fils déjà raccordés** au bornier d'alimentation interne : un **fil marron (phase, Line)** et un **fil bleu (neutre, Neutral)**. Vous n'avez qu'à connecter les extrémités libres de ces fils aux **références de phase et de neutre les plus proches** dans le tableau.

1. Connectez le **fil marron (phase)** à la référence de phase disponible la plus proche, typiquement le côté phase de n'importe quel disjoncteur du tableau.
2. Connectez le **fil bleu (neutre)** à la **barre de neutre** du tableau (le bornier de neutre commun).
3. Serrez fermement les vis.

> **ⓘ NOTE : Câblage personnalisé**  
> Si le câblage fourni ne convient pas à votre application, il est possible d'utiliser vos propres fils.
>
> Pour ce faire, soulevez le couvercle plastique du bornier d'alimentation sur l'appareil, dévissez et débranchez les fils marron et bleu, puis connectez vos propres fils aux mêmes bornes en respectant la même convention phase/neutre. Remettez ensuite le couvercle en place.
>
> Veillez à utiliser des fils de section et d'isolation appropriées pour la tension du secteur.

![Connexion des fils L et N](../assets/connection_l_n.jpg)

### 3.3 Installer le TC sur le Channel 0 (ligne principale) {#33-install-the-ct-on-channel-0-main-line}

Le TC sur le **Channel 0** est le canal le plus précis et doit être utilisé pour mesurer l'énergie totale entrant dans votre maison.

1. Prenez **une** des pinces ampèremétriques du kit (n'importe laquelle, elles sont toutes identiques).
2. Identifiez le **conducteur de la ligne principale** en aval du disjoncteur général (typiquement le fil marron/noir provenant du compteur kWh et entrant dans le tableau).
3. Ouvrez la pince ampèremétrique.
4. Installez-la **autour du seul conducteur de phase ou du seul conducteur de neutre** - jamais les deux ensemble. Pincer L et N ensemble fait s'annuler les champs magnétiques et la mesure tombe à zéro.
5. Fermez la pince jusqu'à entendre/sentir le clic.
6. Branchez la prise jack 3,5 mm dans la prise marquée **`0`** sur l'appareil (les numéros de canaux sont imprimés sur l'autocollant supérieur en façade).

> **ⓘ NOTE : Le sens de la pince n'a pas d'importance**  
> Les pinces ampèremétriques ne sont pas directionnelles. Si vous découvrez plus tard (en [§4.7](02-setup.md#47-verification)) que les mesures sur un canal sont inversées (par exemple, négatives alors que vous attendez du positif), vous n'avez pas besoin de rouvrir le tableau. Cochez simplement la case **Reverse** pour ce canal dans l'interface web et le signe s'inverse instantanément.

> **⚠ Alimentation principale triphasée**  
> Si votre alimentation principale est triphasée, le TC sur le Channel 0 va sur **une des trois phases**, et vous aurez besoin de TC supplémentaires sur les deux autres phases (sur des canaux dérivés libres). Voir **[Annexe B](appendices.md#appendix-b-three-phase-configuration)**.

### 3.4 Installer les TC sur les canaux dérivés (1 à 15) : étape critique ⚠ {#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-}

C'est l'étape où la plupart des erreurs d'installation se produisent. **Lisez cette section avant d'installer quoi que ce soit.**

#### 3.4.1 Le problème du « fil d'entrée partagé » {#341-the-shared-input-wire-problem}

À l'intérieur d'un tableau électrique, les disjoncteurs peuvent être alimentés en **cascade** sur le côté **entrée** : la ligne d'alimentation entre dans le disjoncteur n°1, puis saute au disjoncteur n°2, puis au n°3, et ainsi de suite. Cela a une conséquence majeure pour la mesure :

> **⚠ Le fil du côté ENTRÉE (haut) d'un disjoncteur transporte le courant de CE disjoncteur ET de tous les disjoncteurs en aval dans la chaîne.**  
> Si vous installez le TC sur le fil d'entrée, vous lirez la **somme de plusieurs circuits**, ce qui est faux.

Le fil du **côté SORTIE (bas)** du disjoncteur transporte **uniquement** le courant des charges connectées à ce disjoncteur précis. **C'est là que le TC doit aller.**

![Placement du TC](../assets/ct_placement_breaker.svg)

#### 3.4.2 La règle {#342-the-rule}

> **Installez toujours les TC dérivés sur le côté SORTIE (charge) du disjoncteur, c'est-à-dire le fil qui quitte le disjoncteur et va vers les charges dans la maison. Jamais sur le peigne d'entrée.**

#### 3.4.3 Étape par étape pour chaque TC dérivé {#343-step-by-step-for-each-branch-ct}

Pour chaque circuit que vous voulez surveiller :

1. Décidez quel disjoncteur vous voulez surveiller (par exemple, « cuisine », « lumières », « machine à laver »).
2. Localisez le fil de **sortie** de ce disjoncteur, celui qui quitte la borne inférieure vers les charges. **Pas** le fil d'entrée en haut.
3. Ouvrez la pince ampèremétrique.
4. Installez-la **autour du seul conducteur de phase ou du seul conducteur de neutre** de ce fil de sortie - jamais les deux ensemble, jamais autour du conducteur de protection (fil jaune/vert).
5. Fermez la pince jusqu'au clic.
6. Branchez la prise jack 3,5 mm dans une prise libre de l'appareil, **numérotée de 1 à 15** (les numéros de canaux sont imprimés sur les autocollants en façade).
7. **Si vous savez de quel circuit il s'agit, notez le numéro du canal et l'étiquette du disjoncteur** (par exemple, « Channel 2 → Lumières du jardin »). Vous l'utiliserez en [§4.5](02-setup.md#45-configure-each-channel) pour donner à chaque canal un nom parlant dans l'interface. Utilisez le tableau de l'[Annexe A](appendices.md#appendix-a-channel-map).

> **✅ ASTUCE : Vous ne savez pas ce que contrôle chaque disjoncteur ?**  
> Pas de problème. Branchez les TC dans n'importe quels canaux libres et poursuivez l'installation. Une fois l'appareil en ligne, vous pourrez renommer les canaux à tout moment depuis l'interface web.

> **✅ ASTUCE : Ne vous inquiétez pas de l'orientation du TC**  
> Vous avez installé un TC à l'envers par accident ? Pas de problème. EnergyMe détecte automatiquement les TC inversés à la première mesure après l'activation d'un canal et inverse la polarité pour vous. Pas besoin de rouvrir le tableau ni d'ajuster quoi que ce soit manuellement.

> **⚠ AVERTISSEMENT : Erreurs courantes à éviter**
>
> - ❌ Installer la pince sur le fil d'**entrée** d'un disjoncteur (lit plusieurs circuits)
> - ❌ Installer la pince sur le **peigne de distribution** (lit la somme de tous les disjoncteurs du peigne)
> - ❌ Installer la pince autour de **L et N ensemble** (lit zéro)
> - ❌ Installer la pince autour du **conducteur de protection** (lit zéro en conditions normales, peut lire des courants de défaut sinon)
> - ❌ Forcer la pince sur un fil trop épais ; les mâchoires doivent se fermer complètement et cliquer

> **⚠ Charges dérivées triphasées**  
> Si une charge spécifique que vous voulez surveiller est triphasée (par exemple, une borne de recharge VE ou pompe à chaleur triphasée), voir **[Annexe B](appendices.md#appendix-b-three-phase-configuration)** avant d'installer la pince.

### 3.5 Vérification finale avant de refermer le tableau {#35-final-check-before-closing-the-panel}

Avant de réenclencher le disjoncteur général :

| Vérification | OK ? |
| --- | --- |
| Toutes les prises jack des TC sont entièrement insérées dans l'appareil (1 sur Channel 0, les autres sur Channels 1-15) | ☐ |
| Les fils marron (L) et bleu (N) de l'appareil sont serrés, pas de cuivre visible | ☐ |
| Le TC du Channel 0 est sur le conducteur de phase principal | ☐ |
| Chaque TC dérivé est sur le côté SORTIE de son disjoncteur (pas sur le peigne de distribution) | ☐ |
| Pas d'outils ni de vis oubliés à l'intérieur du tableau | ☐ |
| Plan des canaux ([Annexe A](appendices.md#appendix-a-channel-map)) rempli (là où c'est connu) et photographié | ☐ |
| Couvercle du tableau prêt à être remis en place | ☐ |

### 3.6 Première mise sous tension {#36-first-power-on}

1. Refermez le couvercle du tableau.
2. Réenclenchez le disjoncteur général **ON**.
3. Observez la LED en façade de l'appareil.

> **ⓘ NOTE : Couleurs de la séquence de démarrage**  
> Pendant les ~10 premières secondes, la LED parcourt plusieurs couleurs en succession rapide (jaune, orange, violet et d'autres). **C'est tout à fait normal ;** chaque couleur marque une étape de la séquence de démarrage. N'agissez sur aucune d'entre elles ; attendez que la LED se stabilise.

Après le démarrage, trois cas sont possibles :

| Comportement de la LED | Signification | Étape suivante |
| --- | --- | --- |
| 🔵 Bleu, clignotement rapide | Pas de Wi-Fi configuré ; portail captif actif | Aller à **[§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal)** |
| 🔵 Bleu, pulsation lente | Wi-Fi connu trouvé mais pas encore connecté (ex. routeur encore en démarrage) | Attendez jusqu'à 60 s ; la LED devrait se stabiliser en vert fixe |
| 🟢 Vert fixe | Connecté au Wi-Fi domestique, surveillance en cours | Aller à **[§4.3](02-setup.md#43-access-the-web-interface)** (sautez [§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal) et [§4.2](02-setup.md#42-configure-your-home-wi-fi)) |

> **⚠ AVERTISSEMENT**  
> Si vous sentez une odeur de brûlé, entendez un bourdonnement ou voyez de la fumée : **coupez immédiatement le disjoncteur général** et contactez le support avant toute autre action.

---

**Suivant :** [Configuration →](02-setup.md)
