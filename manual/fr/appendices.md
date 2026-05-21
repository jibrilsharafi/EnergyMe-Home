<!-- translation
source: appendices.md
source-commit: f2923d3
translated: 2026-05-21
-->

# Annexes

## Annexe A : Plan des canaux {#appendix-a-channel-map}

À remplir pendant l'installation (là où c'est connu), prenez une photo avant de refermer le tableau.

| Channel # | Étiquette du disjoncteur | Description (pièce/charge) | Calibre TC | Role | Phase | Grouping |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Disjoncteur général | Ligne entrante de toute la maison | 30 A | Grid | 1 | Group 0 |
| 1 | | | 30 A | Load | 1 | Group 1 |
| 2 | | | 30 A | Load | 1 | Group 2 |
| 3 | | | 30 A | Load | 1 | Group 3 |
| 4 | | | | | | |
| ... | | | | | | |
| 15 | | | | | | |

---

## Annexe B : Configurations multiphases {#appendix-b-three-phase-configuration}

*EnergyMe Home* est fondamentalement un appareil monophasé, mais il peut surveiller une **alimentation principale triphasée**, des **charges triphasées spécifiques**, ainsi que les **circuits biphasés à point milieu 120/240 V nord-américains**, en réglant le champ `Phase` sur chaque canal et en combinant les mesures sur le tableau de bord via le champ `Grouping`.

### B.1 Alimentation principale triphasée {#b1-three-phase-main-supply}

#### Matériel {#hardware}

1. Prenez **3 TC** dans le kit (ou le kit d'extension).
2. Installez chaque TC autour de **l'un des trois conducteurs de phase** (L1, L2, L3), jamais autour du neutre.
3. Branchez-les dans l'appareil :
   - **Phase L1** → Channel `0` (le canal de référence principal)
   - **Phase L2** → n'importe quel canal dérivé libre (par exemple, Channel `1`)
   - **Phase L3** → n'importe quel canal dérivé libre (par exemple, Channel `2`)
4. **Notez quelle phase physique est sur quel canal ;** vous en aurez besoin pour l'étape logicielle.

#### Logiciel {#software}

Dans **Channels** ([§4.5](02-setup.md#45-configure-each-channel)), configurez les trois canaux comme ceci :

| Channel | Label | Phase | Active | Grouping | Role |
| --- | --- | --- | --- | --- | --- |
| 0 | `Grid L1` | `1` | ☑ | `Grid` | `Grid (+ import, - export)` |
| 1 (ou la dérivation choisie) | `Grid L2` | `2` | ☑ | `Grid` | `Grid (+ import, - export)` |
| 2 (ou la dérivation choisie) | `Grid L3` | `3` | ☑ | `Grid` | `Grid (+ import, - export)` |

Les trois canaux partagent la même valeur **Grouping** (`Grid`), donc le tableau de bord affichera une seule carte « Grid » avec la **puissance triphasée totale** de l'alimentation principale.

> **ⓘ Remplacer le groupe par défaut.** Le champ Grouping est pré-rempli avec `Group 0`, `Group 1`, `Group 2`, etc., par défaut. Pour le regroupement triphasé, vous devez **remplacer** la valeur par défaut par le nom de groupe partagé (par exemple, `Grid`) sur chacun des trois canaux.

### B.2 Charge dérivée triphasée (ex. borne VE, pompe à chaleur, four) {#b2-three-phase-branch-load-eg-ev-charger-heat-pump-oven}

#### Matériel {#hardware-1}

1. Prenez **3 TC**.
2. Installez chaque TC sur une phase de la charge.
3. Branchez-les dans 3 canaux dérivés libres (par exemple, Channels `3`, `4`, `5`).
4. Notez quelle phase est sur quel canal.

#### Logiciel {#software-1}

Exemple pour une borne de recharge VE triphasée :

| Channel | Label | Phase | Active | Grouping | Role |
| --- | --- | --- | --- | --- | --- |
| 3 | `EV charger L1` | `1` | ☑ | `EV charger` | `Load` |
| 4 | `EV charger L2` | `2` | ☑ | `EV charger` | `Load` |
| 5 | `EV charger L3` | `3` | ☑ | `EV charger` | `Load` |

Le tableau de bord affichera une seule carte « EV charger » avec la puissance triphasée totale de l'appareil.

> **✅ ASTUCE : Nommer le groupe**  
> Utilisez un nom de groupe simple (par exemple, `Oven`, `Heat pump`, `EV charger`) sans suffixe de phase ; c'est ce qui apparaîtra sur le tableau de bord. Mettez le suffixe de phase uniquement dans le `Label` de chaque canal, afin de pouvoir toujours inspecter les phases individuelles si nécessaire.

### B.3 Biphasé à point milieu 120/240 V nord-américain {#b3-north-american-120240-v-split-phase}

Le service résidentiel standard en Amérique du Nord est un système **biphasé à point milieu** (split-phase) : un transformateur à point milieu fournit deux « branches » de 120 V (L1 et L2) plus un neutre. La plupart des circuits fonctionnent en phase-neutre à 120 V (éclairage, prises) ; les gros appareils (cuisinière électrique, chauffe-eau, sèche-linge, climatisation centrale, borne de recharge VE de niveau 2) fonctionnent en phase-phase à 240 V à cheval sur les deux branches.

*EnergyMe Home* mesure la tension **phase-neutre** (≈120 V), donc un TC sur un circuit 240 V sous-estimerait par défaut la puissance d'un facteur deux. Le micrologiciel s'en charge pour vous : sélectionnez **`240V (Split Phase)`** dans le champ `Phase` du canal et un **multiplicateur de tension ×2** est appliqué automatiquement.

> **⚠ AVERTISSEMENT : L'alimentation L et N de l'appareil lui-même doit toujours être câblée en phase-neutre (≈120 V).**  
>
> Le réglage `240V (Split Phase)` dans le champ `Phase` **modifie uniquement la façon dont la mesure de courant du TC de ce canal est interprétée**. Il **ne change pas** la façon dont l'appareil lui-même est alimenté, ni comment la tension est mesurée en interne.
>
> - Les fils **marron (L)** et **bleu (N)** de l'appareil doivent toujours être raccordés entre **une branche 120 V et le Neutre**, exactement comme pour un circuit 120 V.
> - Ne câblez jamais le L et le N de l'appareil à cheval sur les deux branches à 240 V. L'alimentation interne est prévue pour 100-240 V AC et ne serait pas endommagée, mais la **référence de tension utilisée pour calculer la puissance sur chaque canal** ne correspondrait plus à l'hypothèse du micrologiciel et **toutes les mesures seraient fausses**.
> - Ceci s'applique quel que soit le nombre de canaux réglés sur `240V (Split Phase)` : le câblage d'alimentation de l'appareil est indépendant du réglage `Phase` par canal.

#### Quand utiliser chaque réglage de phase (Amérique du Nord) {#when-to-use-each-phase-setting-north-america}

| Type de circuit | Sur quoi le TC est installé | Réglage Phase |
| --- | --- | --- |
| Dérivation 120 V (une branche, phase-neutre) | L'une ou l'autre branche du circuit 120 V | `1` |
| Dérivation 240 V (phase-phase, les deux branches) | L'une ou l'autre branche du circuit 240 V | `240V (Split Phase)` |
| Alimentation principale, une seule branche | L'une des deux branches principales | `1` |
| Alimentation principale, les deux branches séparément | Un TC sur chaque branche, sur des canaux séparés | `1` sur chacun |

#### Matériel {#hardware-2}

Pour une dérivation 240 V (par exemple, un sèche-linge ou une borne de recharge VE de niveau 2) :

1. Prenez **un** TC à pince.
2. Installez-le autour de **l'un ou l'autre** des deux conducteurs de ligne du circuit 240 V (L1 ou L2). Ne pincez pas le neutre, et jamais les deux branches ensemble.
3. Refermez la pince jusqu'au clic.
4. Branchez la fiche 3,5 mm dans un canal libre de l'appareil.

#### Logiciel {#software-2}

Dans **Channels** ([§4.5](02-setup.md#45-configure-each-channel)), réglez :

| Champ | Valeur |
| --- | --- |
| Label | par exemple, `Dryer` ou `EV charger` |
| Phase | **`240V (Split Phase)`** |
| Active | ☑ |
| Grouping | Un groupe par appareil (le `Group N` par défaut convient) |
| Role | `Load` (ou `Battery` / `Inverter` selon le cas) |

> **ⓘ NOTE : Pourquoi ×2 et pas une mesure directe de la tension ?**  
> Le transformateur de tension à l'intérieur d'*EnergyMe Home* est câblé entre la phase et le neutre, ce qui correspond à ≈120 V sur un service biphasé à point milieu nord-américain. Un circuit 240 V phase-phase a exactement le double de cette tension RMS, donc multiplier le courant par 2× les 120 V mesurés donne la puissance correcte. C'est préférable à l'ajout d'une référence de tension supplémentaire, et cela fonctionne pour tout service split-phase standard.

> **⚠ N'utilisez pas `240V (Split Phase)` en dehors des systèmes biphasés à point milieu nord-américains.** C'est un raccourci numérique pour la topologie 120/240 V split-phase spécifique, et il produirait des lectures erronées sur un système 230 V monophasé européen ou sur tout système triphasé.

---

## Annexe C : Référence d'état des LED {#appendix-c-led-status-reference}

La LED en façade de l'appareil communique l'état du système en un coup d'œil.

> **ⓘ Couleurs de démarrage**  
> Pendant les ~10 premières secondes après la mise sous tension, la LED parcourt brièvement le jaune, l'orange, le violet et d'autres couleurs. **C'est normal.** Chaque couleur marque une étape interne de démarrage. Attendez que la LED se stabilise avant de tirer la moindre conclusion.

### Fonctionnement normal {#normal-operation}

| LED | État | Signification |
| --- | --- | --- |
| 🟢 Vert fixe | Connecté | Wi-Fi connecté, surveillance normale |
| 🔵 Bleu, pulsation lente | Connexion en cours | Identifiants Wi-Fi configurés mais non actuellement connectés (premier démarrage, routeur en cours de démarrage, ou perte temporaire de connexion). Se reconnecte automatiquement ; se résout généralement en 60 s |
| 🔵 Bleu, clignotement rapide | Portail actif | Pas de Wi-Fi configuré ; le portail captif est ouvert ([§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal)) |

### Alertes {#alerts}

| LED | État | Signification | Que faire |
| --- | --- | --- | --- |
| 🟣 Violet fixe | Mode sans échec | Protection anti-plantage déclenchée ; l'appareil surveille toujours et est entièrement accessible via l'interface web | Allez dans **Logs** dans le menu supérieur pour investiguer ; l'appareil récupérera automatiquement |
| 🔴 Rouge, clignotement rapide | Erreur critique | Défaillance persistante après tentatives de récupération | Redémarrer via le bouton ([Annexe D](#appendix-d-user-button-reference)) ; si cela se reproduit, contactez le support |

### Retour visuel du bouton (pendant que le bouton est maintenu ; voir [Annexe D](#appendix-d-user-button-reference)) {#button-feedback-while-the-button-is-held-see-appendix-d-user-button-reference}

| Couleur de la LED pendant le maintien | Action déclenchée au relâchement |
| --- | --- |
| ⚪ Blanc | Appui enregistré mais sous le seuil d'action. Relâchez pour aucune action |
| 🔵 Cyan | Redémarrage |
| 🟡 Jaune | Réinitialisation du mot de passe par défaut |
| 🟠 Orange | Réinitialisation Wi-Fi (rouvre le portail captif) |
| 🔴 Rouge | Réinitialisation aux paramètres d'usine : toutes les données et tous les paramètres seront effacés |
| ⚪ Blanc (à nouveau) | Maintenu trop longtemps. Relâchez et réessayez |

---

## Annexe D : Référence du bouton utilisateur {#appendix-d-user-button-reference}

Le bouton en façade de l'appareil vous permet de récupérer des situations courantes sans avoir besoin d'un téléphone ou d'un ordinateur portable.

**Comment ça marche :** appuyez et maintenez. La LED change de couleur au fur et à mesure que vous maintenez, indiquant quelle action sera déclenchée au relâchement. **Relâchez le bouton dès que vous voyez la couleur correspondant à l'action que vous voulez.**

| Durée de maintien | Couleur de la LED | Action au relâchement |
| --- | --- | --- |
| < 2 s | Blanc | Aucune action |
| 2-5 s | **Cyan** | **Redémarrage :** équivalent à une coupure de l'alimentation |
| 5-10 s | **Jaune** | **Réinitialisation du mot de passe :** réinitialise le mot de passe web à `energyme` |
| 10-15 s | **Orange** | **Réinitialisation Wi-Fi :** efface les identifiants Wi-Fi et rouvre le portail captif ([§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal)). Les données d'énergie et la configuration des canaux sont préservées |
| 15-20 s | **Rouge** | **Réinitialisation aux paramètres d'usine** ⚠ : efface toutes les données, la configuration et les identifiants. À n'utiliser qu'en dernier recours |
| > 20 s | Blanc | Aucune action. Relâchez et réessayez |

> **⚠ AVERTISSEMENT : La réinitialisation aux paramètres d'usine est irréversible**  
> Une réinitialisation aux paramètres d'usine efface toutes les données d'énergie accumulées, les noms de canaux, l'étalonnage, les identifiants Wi-Fi et votre mot de passe personnalisé. Il n'y a pas de retour en arrière. Ne maintenez jusqu'au rouge que si vous êtes certain.

> **✅ ASTUCE : La couleur comme confirmation**  
> Vous n'avez pas besoin de compter les secondes. Surveillez la LED : relâchez au moment où elle affiche la couleur que vous voulez. Si vous dépassez et revenez au blanc, continuez à maintenir ; relâchez simplement sans déclencher d'action. Puis réessayez depuis le début.

---

**Précédent :** [← Dépannage](03-troubleshooting.md)
