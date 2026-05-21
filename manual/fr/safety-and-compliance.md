<!-- translation
source: safety-and-compliance.md
source-commit: eda785a
translated: 2026-05-21
-->

# EnergyMe Home : Informations de sécurité et conformité

Ce document contient les informations réglementaires, de sécurité, de garantie et d'élimination relatives à *EnergyMe Home*. **Lisez-le une fois avant d'installer l'appareil.** Conservez ce document pendant toute la durée de vie du produit ; si le produit est cédé à un tiers, ce document doit être transmis avec lui.

Pour l'installation et la configuration pratiques étape par étape, voir le [Manuel d'installation](README.md).

---

## 1. Fabricant {#1-manufacturer}

| | |
| --- | --- |
| **Société** | EnergyMe S.r.l. |
| **Adresse du siège** | Via Plezzo 56, 20132 Milano (MI), Italie |
| **TVA / Codice Fiscale** | IT14543830963 |
| **Téléphone** | +39 320 8765133 |
| **Site web** | <https://www.energyme.net> |
| **Support** | <support@energyme.net> |

**Service après-vente autorisé.** Le service, la réparation et la maintenance non courante sont effectués exclusivement par le fabricant ou par du personnel autorisé par écrit par le fabricant. Toute intervention par du personnel non autorisé annule la garantie et peut compromettre la sécurité.

---

## 2. Identification du produit {#2-product-identification}

| | |
| --- | --- |
| **Nom du produit** | EnergyMe Home |
| **Code produit** | EM-HOME-1.0-KIT-STANDARD |
| **Révision matérielle** | v6.1 |
| **Certification open-source** | OSHWA `IT000025` |

Le numéro de série, la révision matérielle et les données de fabrication sont imprimés sur l'étiquette à l'intérieur du boîtier de l'appareil. Notez-les avant d'installer le produit ; ils sont requis pour les demandes de support et les réclamations sous garantie.

---

## 3. Déclaration de Conformité {#3-declaration-of-conformity}

*EnergyMe Home* est fourni avec une **Déclaration de Conformité (DoC)** séparée, établie par le fabricant en vertu des directives applicables de l'Union européenne. Avant d'utiliser l'appareil, vérifiez que la DoC est présente dans l'emballage ou disponible auprès du fabricant.

L'appareil est conforme aux exigences essentielles des législations européennes suivantes :

- **2014/35/EU** - Directive Basse Tension (LVD)
- **2014/30/EU** - Compatibilité Électromagnétique (EMC)
- **2014/53/EU** - Directive sur les Équipements Radio (RED), y compris les exigences de cybersécurité de l'Article 3(3)(d)/(e)/(f) abordées via **EN 18031-1** et **EN 18031-2**
- **2011/65/EU** - Limitation des substances dangereuses (RoHS), telle que modifiée par la Directive Déléguée (UE) **2015/863**
- **2012/19/EU** - Déchets d'Équipements Électriques et Électroniques (WEEE)
- **Règlement (UE) 2023/988** - Règlement Général sur la Sécurité des Produits (GPSR)

La liste complète des normes harmonisées appliquées (y compris celles relatives à la sécurité radio, à l'EMC, à l'exposition RF et aux exigences de cybersécurité pour les équipements radio connectés à internet) est donnée dans la Déclaration de Conformité.

> Si la Déclaration de Conformité est manquante, **n'utilisez pas l'appareil**. Contactez le fabricant pour en obtenir une copie.

---

## 4. Symboles et conventions {#4-symbols-and-conventions}

### 4.1 Symboles sur le produit {#41-symbols-on-the-product}

Les symboles suivants peuvent apparaître sur l'étiquette de l'appareil, sur l'emballage ou dans cette documentation. Leur présence est obligatoire et ils ne doivent pas être retirés, recouverts ou masqués.

| Symbole | Signification |
| --- | --- |
| **CE** | Conformité aux directives UE applicables (voir §3) |
| **Poubelle barrée sur roues** (WEEE) | Collecte séparée des équipements électriques et électroniques requise en fin de vie (voir §11) |
| **Éclair dans un triangle** | Risque d'électrocution ; consulter le manuel avant toute intervention |
| **Livre ouvert / « i » dans un cercle** | Lire le manuel d'utilisation avant d'utiliser l'appareil |
| **Classe II (double carré)** | Équipement avec isolation renforcée ou double ; aucune connexion à la terre de protection requise |

### 4.2 Conventions utilisées dans la documentation {#42-conventions-used-in-the-documentation}

Les manuels destinés à l'utilisateur (installation, configuration, dépannage) utilisent les encadrés en ligne suivants :

| Encadré | Objet |
| --- | --- |
| `⚠ WARNING` | Procédures dont le non-respect peut causer des dommages à l'appareil, aux biens, ou exposer des personnes à un danger |
| `⚠ NOTE` | Conditions ou contraintes importantes mises en évidence en dehors du texte courant |
| `ⓘ NOTE` | Information utile qui complète une procédure |
| `✅ TIP` | Recommandations et bonnes pratiques |

Un marqueur rouge `🔴` ou « DANGER », là où il est utilisé, indique des procédures dont le non-respect peut causer des blessures ou des dommages graves à l'appareil.

---

## 5. Usage prévu {#5-intended-use}

*EnergyMe Home* est un dispositif IoT monté sur rail DIN pour mesurer et surveiller la consommation et la production d'énergie électrique dans les environnements **résidentiels**, **prosommateurs** et **petits commerciaux**. Il prend en charge jusqu'à 16 canaux de mesure (un direct et quinze multiplexés) en utilisant des transformateurs de courant (TC) à pince non invasifs.

L'appareil est destiné à être installé par un **électricien qualifié** à l'intérieur d'un tableau électrique fermé, dans un environnement intérieur, sur un rail DIN standard (EN 60715, DIN 43880), en conformité avec le code et les règlements électriques locaux du pays d'installation.

L'appareil surveille les installations électriques monophasées et triphasées (voir [Annexe B](appendices.md#appendix-b-three-phase-configuration)). Il n'agit pas sur l'installation électrique : c'est uniquement un dispositif de mesure et de surveillance.

L'appareil communique via **Wi-Fi 2,4 GHz** (IEEE 802.11 b/g/n) et prend en charge l'intégration via REST API, MQTT, Modbus TCP et InfluxDB.

---

## 6. Usage non prévu {#6-non-intended-use}

Toute utilisation autre que celle décrite au §5 est considérée comme impropre. Le fabricant décline toute responsabilité pour les dommages aux personnes, aux animaux ou aux biens résultant d'un usage impropre. Un usage impropre annule également la garantie.

En particulier, les usages suivants sont **strictement interdits** :

- Utilisation dans des environnements autres que des tableaux électriques fermés en intérieur (pas d'utilisation extérieure, pas d'environnement ATEX ou atmosphères potentiellement explosives, pas d'environnements corrosifs ou très poussiéreux, pas d'exposition à la lumière directe du soleil, aux flammes ou aux sources de chaleur).
- Utilisation avec une tension ou une fréquence du secteur hors de la plage nominale (voir [fiche technique](../../hardware/datasheet.md)).
- Utilisation avec des transformateurs de courant, accessoires ou pièces détachées non fournis ou expressément approuvés par le fabricant.
- Utilisation comme compteur de facturation pour des transactions commerciales d'énergie.
- Installation ou intervention par du personnel non qualifié.
- Utilisation par des enfants (personnes de moins de 14 ans) ou par des personnes à capacités physiques, sensorielles ou cognitives réduites, sauf sous la supervision d'une personne responsable de leur sécurité.
- Toute modification, altération ou manipulation de l'appareil, de son boîtier, de ses circuits internes, du micrologiciel ou des étiquettes.
- Fonctionnement de l'appareil avec un boîtier endommagé, des conducteurs exposés ou des signes visibles d'impact, de déformation ou de brûlure.
- Immersion dans l'eau ou tout autre liquide ; exposition à des gouttes ou éclaboussures d'eau.
- Divulgation des identifiants d'accès à l'appareil à des personnes non autorisées.
- Utilisation continue de l'appareil après détection d'une anomalie.

La réutilisation de tout composant après la mise hors service de l'appareil relève de la seule responsabilité de l'utilisateur et dégage le fabricant de toute responsabilité.

---

## 7. Risques résiduels {#7-residual-risks}

Malgré la conception et les mesures de protection adoptées par le fabricant, les risques résiduels suivants subsistent :

- **Risque d'électrocution** lors de l'installation, de la maintenance et du démantèlement. L'appareil se connecte directement au secteur AC. Coupez toujours l'alimentation via le disjoncteur général et vérifiez l'absence de tension avec un testeur avant toute intervention.
- **Risque de mesure incorrecte** si les transformateurs de courant sont mal installés (sur un fil d'entrée partagé, sur un peigne de distribution, autour à la fois de L et N, ou autour du conducteur de protection). Voir [Installation §3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-).
- **Risque de dommage à l'appareil** si la tension d'alimentation dépasse la plage nominale, si la tension de sortie du TC dépasse 0,5 V au courant maximum, ou si l'appareil subit un impact mécanique.
- **Risque pour la sécurité réseau** si le mot de passe par défaut n'est pas changé après la première connexion ou si l'appareil est exposé à des réseaux non fiables.

L'utilisateur est responsable de la lecture intégrale de ce document et du Manuel d'installation avant d'installer ou d'utiliser l'appareil.

---

## 8. Conditions environnementales et de stockage {#8-environmental-and-storage-conditions}

L'appareil doit être installé et utilisé **en intérieur uniquement**, dans des conditions environnementales compatibles avec ses spécifications.

| Paramètre | Fonctionnement | Stockage |
| --- | --- | --- |
| Température | 0 °C à +50 °C | -10 °C à +60 °C |
| Humidité relative | ≤ 80 %, sans condensation | ≤ 80 %, sans condensation |
| Indice IP | IP20 (tableau fermé uniquement) | - |
| Altitude | ≤ 2000 m | - |
| Degré de pollution | 2 | - |

La zone de stockage doit être protégée de la lumière directe du soleil, des flammes, des sources de chaleur, de la vapeur, des vapeurs corrosives, et accessible uniquement au personnel autorisé.

---

## 9. Manutention et déballage {#9-handling-and-unpacking}

L'appareil est fourni dans un emballage en carton et plastique conçu pour préserver son intégrité pendant le stockage et le transport. Malgré cela, manipulez l'appareil avec soin pour éviter les chocs et les chutes.

**Au déballage, avant l'installation :**

1. Vérifiez que l'emballage contient tous les articles listés en [Installation §1.1](01-installation.md#11-starter-kit-always-included).
2. Inspectez visuellement l'appareil, les pinces ampèremétriques et les câbles pour détecter tout dommage (fissures, déformation, conducteurs exposés, signes d'impact).
3. Vérifiez que la Déclaration de Conformité (§3) et ce document sont présents.

**Si un élément manque, est endommagé ou non conforme, n'installez pas l'appareil.** Contactez `support@energyme.net` avec une photographie du contenu de l'emballage et du problème. Ne tentez pas de réparer ou de modifier l'appareil.

Manipulez toujours l'appareil par son boîtier ABS ; ne le tirez jamais par les câbles. Après le déballage, éliminez l'emballage conformément aux règlements locaux et tenez-le hors de portée des enfants.

---

## 10. Maintenance {#10-maintenance}

L'appareil est conçu pour une longue durée de vie opérationnelle avec une maintenance minimale. **Il n'y a aucune pièce réparable par l'utilisateur à l'intérieur.** N'ouvrez pas le boîtier ; l'ouverture annule la garantie et expose l'utilisateur à un risque d'électrocution.

### 10.1 Maintenance courante (par l'utilisateur) {#101-routine-maintenance-by-the-user}

**Au moins une fois par an**, avec l'appareil hors tension et déconnecté du secteur :

1. Inspectez visuellement l'appareil, les câbles et les pinces ampèremétriques pour repérer tout signe de dommage, de déformation ou de surchauffe.
2. Vérifiez que les étiquettes sur l'appareil sont lisibles et intactes. Si une étiquette est endommagée ou illisible, contactez le fabricant pour en obtenir une de remplacement.
3. Retirez la poussière du boîtier à l'aide d'une brosse douce, d'un aspirateur muni d'un embout souple, ou d'air comprimé à basse pression.

> **⚠ AVERTISSEMENT : Produits d'entretien**
>
> - ❌ N'utilisez **pas** de solvants, d'alcool, d'ammoniaque, ni de produits d'entretien abrasifs ou corrosifs.
> - ❌ N'utilisez **pas** de chiffons ni d'éponges abrasifs.
> - ❌ Ne **pulvérisez pas** de liquides directement sur l'appareil.
> - ✅ Utilisez un chiffon doux sec ou à peine humide sur l'extérieur du boîtier uniquement, si nécessaire.

### 10.2 Maintenance non courante (par le fabricant) {#102-non-routine-maintenance-by-the-manufacturer}

La maintenance non courante (réparations, remplacement de composants internes, restauration du micrologiciel après défaut physique) est effectuée exclusivement par le fabricant ou par du personnel autorisé. Contactez `support@energyme.net` pour toute intervention au-delà du nettoyage courant.

### 10.3 Mises à jour du micrologiciel {#103-firmware-updates}

Les mises à jour du micrologiciel font partie de la vie normale du produit et sont fournies over-the-air (OTA) via l'interface web (voir [Configuration §4.8](02-setup.md#48-firmware-updates)). Appliquez les mises à jour disponibles dans un délai raisonnable pour maintenir l'appareil sécurisé et aligné avec les dernières fonctionnalités.

### 10.4 Pièces détachées {#104-spare-parts}

N'utilisez que des pièces détachées d'origine. L'utilisation de pièces non d'origine annule la garantie et dégage le fabricant de toute responsabilité pour les dommages aux personnes ou aux biens. Commandez les pièces détachées à `support@energyme.net`.

---

## 11. Élimination (WEEE) {#11-disposal-weee}

<img src="../assets/weee-symbol.svg" alt="Symbole WEEE" width="60">

Le symbole de la **poubelle barrée sur roues** imprimé sur l'étiquette de l'appareil indique qu'à la fin de sa vie opérationnelle, l'appareil doit être éliminé séparément des déchets ménagers, conformément à :

- la **Directive 2012/19/EU** sur les Déchets d'Équipements Électriques et Électroniques (DEEE), telle que modifiée ;
- le **D.Lgs. 14 marzo 2014, n. 49** (transposition italienne de la Directive 2012/19/EU).

**N'éliminez pas l'appareil ou ses accessoires avec les déchets ménagers généraux.** Remettez-les à un point de collecte WEEE agréé dans votre région, ou retournez-les au fabricant ou au revendeur lors de l'achat d'un appareil équivalent neuf, conformément aux règlements locaux.

L'élimination inappropriée est punissable par la loi nationale applicable et peut causer des dommages environnementaux et nuire à la santé humaine en raison de la présence de substances dangereuses dans les équipements électriques et électroniques.

Les matériaux d'emballage (carton, plastique) doivent également être éliminés conformément aux règlements locaux de recyclage.

---

## 12. Démantèlement {#12-decommissioning}

À la fin de la vie opérationnelle de l'appareil, ou avant tout retrait permanent :

1. Déconnectez l'appareil du secteur via le disjoncteur général.
2. Vérifiez l'absence de tension avec un testeur.
3. Retirez l'appareil du rail DIN et débranchez les fils L/N et les pinces ampèremétriques.
4. **Effacez toutes les données de configuration et les identifiants** via une réinitialisation aux paramètres d'usine avant l'élimination ou la cession ([Annexe D](appendices.md#appendix-d-user-button-reference)). Ceci protège vos identifiants Wi-Fi et toute configuration personnelle.
5. Éliminez l'appareil conformément au §11.

Si l'appareil est cédé à un tiers (vente, don, location), **toute la documentation accompagnant l'appareil doit être transmise avec lui**, y compris ce document, le Manuel d'installation et la Déclaration de Conformité.

---

## 13. Garantie {#13-warranty}

Le produit est couvert par les conditions de garantie énoncées dans le contrat d'achat et résumées ci-dessous. La garantie ne s'applique que lorsque l'appareil est utilisé dans les conditions d'usage prévu du §5 et conformément à cette documentation.

### 13.1 Durée {#131-duration}

- **12 mois** pour les clients professionnels, conformément à la législation européenne et nationale applicable.
- **24 mois** pour les consommateurs finaux, conformément à la Directive 1999/44/CE et au D.Lgs. 206/2005 (Codice del Consumo, code de la consommation italien).

La période de garantie commence à la date de livraison, attestée par la facture ou le reçu d'achat.

### 13.2 Couverture {#132-coverage}

Le fabricant s'engage, à sa discrétion, à réparer ou remplacer les composants jugés défectueux en matériaux ou en fabrication, après sa propre inspection. Les pièces remplacées deviennent la propriété du fabricant.

### 13.3 Exclusions {#133-exclusions}

La garantie **ne couvre pas** :

- Les dommages causés par un usage impropre, une négligence, un accident ou le non-respect de cette documentation.
- Les dommages causés par des événements externes (foudre, surtensions, inondations, incendies, catastrophes naturelles).
- Les dommages causés par l'utilisation d'accessoires, de pièces détachées ou de transformateurs de courant non d'origine.
- Les dommages au boîtier, aux étiquettes ou aux composants internes causés par une manipulation, une ouverture ou une modification.
- L'usure normale liée à l'utilisation.
- Les frais de transport, d'expédition et d'installation, qui sont à la charge de l'acheteur.
- Les difficultés découlant de la réglementation de pays autres que celui où l'appareil a été vendu.

### 13.4 Réclamations {#134-claims}

Pour faire valoir la garantie, contactez `support@energyme.net` en fournissant :

1. Le code produit et le numéro de série (imprimés sur l'étiquette à l'intérieur du boîtier de l'appareil).
2. La preuve d'achat (facture ou reçu).
3. Une description détaillée du problème et, si possible, des photographies ou vidéos.

Tout produit retourné sans preuve d'achat ou avec des étiquettes d'identification altérées sera réparé aux frais de l'acheteur.

### 13.5 Limitation de responsabilité {#135-limitation-of-liability}

Le fabricant n'est pas responsable des incidents ou dommages découlant d'un usage impropre, d'une modification non autorisée ou du non-respect de cette documentation. Les conditions complètes de responsabilité sont énoncées dans le contrat d'achat.

---

## 14. Conservation des documents {#14-document-retention}

Le fabricant conserve le dossier technique de ce produit (y compris ce document, la DoC, la documentation de conception et les enregistrements d'évaluation de la conformité) pendant **10 ans** à compter de la date à laquelle la dernière unité est mise sur le marché, conformément aux directives UE applicables.

Pendant cette période, le dossier technique est disponible exclusivement pour les autorités de surveillance du marché sur demande.

---
