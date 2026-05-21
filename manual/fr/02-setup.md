<!-- translation
source: 02-setup.md
source-commit: dbf89c0
translated: 2026-05-21
-->

# Configuration

À partir de maintenant, le travail peut être effectué par l'utilisateur final. Le tableau électrique peut rester fermé.

---

## 4. Configuration logicielle {#4-software-configuration}

### 4.1 Se connecter au Wi-Fi de l'appareil (portail captif) {#41-connect-to-the-devices-wi-fi-captive-portal}

Lors de la première mise sous tension, ou après une réinitialisation Wi-Fi, l'appareil diffuse son propre réseau Wi-Fi pour que vous puissiez le configurer.

1. Sur votre téléphone ou ordinateur portable, ouvrez les paramètres Wi-Fi.
2. Cherchez un réseau nommé **`EnergyMe-<DEVICE_ID>`**, où `<DEVICE_ID>` est le code à 12 caractères imprimé sur l'étiquette à l'intérieur du boîtier de l'appareil.
3. Connectez-vous à ce réseau. Le mot de passe est :
   - **Appareils EnergyMe** (achetés chez nous) : imprimé sur les autocollants du boîtier, à la fois sous forme de QR code scannable et en texte clair.
   - **Appareils communautaires** (auto-construits) : le `DEVICE_ID` lui-même (le même code à 12 caractères que le nom du réseau).
4. Une page de configuration s'ouvre automatiquement. Si ce n'est pas le cas, ouvrez un navigateur et accédez à **`http://192.168.4.1`**.

> **ⓘ NOTE : Si le réseau n'apparaît pas**  
> Attendez 60 secondes après la mise sous tension. S'il n'est toujours pas visible, maintenez le bouton enfoncé pendant 10-15 secondes jusqu'à ce que la LED devienne orange (réinitialisation Wi-Fi), puis relâchez. L'appareil redémarrera et ouvrira à nouveau le portail. Voir **[Annexe D](appendices.md#appendix-d-user-button-reference)** pour la référence complète du bouton.

### 4.2 Configurer votre Wi-Fi domestique {#42-configure-your-home-wi-fi}

Dans la page du portail captif :

1. Allez à **Configure Wi-Fi**.
2. Sélectionnez votre réseau Wi-Fi domestique dans la liste.
3. Saisissez votre mot de passe Wi-Fi.
4. Appuyez sur **Save**.
5. L'appareil redémarre et se connecte à votre réseau domestique, ce qui prend environ 30 secondes. La LED arrêtera de clignoter en bleu et passera au **vert fixe** une fois connectée.

> **✅ ASTUCE : 2,4 GHz uniquement**  
> *EnergyMe Home* utilise le **Wi-Fi 2,4 GHz**. Si votre routeur affiche des réseaux 2,4 GHz et 5 GHz séparés, choisissez le 2,4 GHz. Si votre routeur utilise un nom unique combiné (« band steering »), cela fonctionnera généralement bien, mais si vous rencontrez des problèmes de connexion, demandez via la page d'administration de votre routeur d'exposer la bande 2,4 GHz comme un SSID distinct.

### 4.3 Accéder à l'interface web {#43-access-the-web-interface}

1. Sur un téléphone ou un ordinateur portable connecté au **même Wi-Fi domestique**, ouvrez un navigateur.
2. Accédez à **`http://energyme.local`**.
3. Connectez-vous avec les identifiants par défaut :
   - **Nom d'utilisateur :** `admin`
   - **Mot de passe :** `energyme`

> **✅ ASTUCE : Démo en ligne**  
> Un appareil entièrement configuré est disponible en ligne à **[demo-energyme-home.energyme.net](https://demo-energyme-home.energyme.net/)**. Utilisez-le pour explorer l'interface, la comparer avec votre propre tableau de bord, ou vous familiariser avec la disposition avant d'installer.

> **ⓘ NOTE : Notez l'adresse IP**  
> Une fois connecté, allez dans **Info** dans le menu supérieur. Sous **Network Status**, vous verrez l'adresse IP locale de l'appareil (ex. `192.168.1.42`). **Notez-la ou ajoutez-la aux favoris.** Si `energyme.local` cesse de fonctionner à l'avenir (certains routeurs, PC et anciennes versions d'Android ne gèrent pas les noms `.local` de manière fiable), vous pouvez toujours atteindre l'interface directement en tapant cette adresse IP dans votre navigateur.
>
> Si vous n'avez pas encore noté l'IP et que `energyme.local` ne fonctionne plus, ouvrez la page d'administration de votre routeur et cherchez un appareil avec le nom d'hôte `energyme-home-<DEVICE_ID>`.

### 4.4 Changer le mot de passe par défaut {#44-change-the-default-password}

> **⚠ Faites ceci avant toute autre chose.** L'appareil est livré avec un mot de passe par défaut connu. Tant que vous ne le changez pas, n'importe qui sur votre réseau domestique peut accéder à l'interface.

1. Dans le menu supérieur, allez dans **Configuration**.
2. Faites défiler jusqu'à **Change Password**.
3. Saisissez le mot de passe actuel (`energyme`), puis votre nouveau mot de passe deux fois. Le nouveau mot de passe doit comporter au moins 8 caractères.
4. Cliquez sur **Save**.

L'appareil vous le rappellera avec une fenêtre contextuelle sur le tableau de bord chaque fois que vous l'ouvrirez, jusqu'à ce que le mot de passe ait été changé.

> **ⓘ NOTE : Mot de passe oublié ?**  
> Maintenez le bouton enfoncé pendant 5-10 secondes jusqu'à ce que la LED devienne jaune (réinitialisation du mot de passe), puis relâchez. Le mot de passe est réinitialisé à `energyme`. Reconnectez-vous et définissez-en un nouveau immédiatement. Voir **[Annexe D](appendices.md#appendix-d-user-button-reference)** pour la référence complète du bouton.

### 4.5 Configurer chaque canal {#45-configure-each-channel}

Vous associez maintenant chaque TC physique à un nom et un rôle parlant dans l'interface. Allez dans **Channels** dans le menu supérieur et configurez chaque canal sur lequel un TC est connecté.

Chaque canal a les champs suivants :

| Champ | Signification |
| --- | --- |
| **Label** | Un nom libre (par exemple, « Grid », « Cuisine », « Borne VE »). C'est le nom qui apparaîtra sur le tableau de bord |
| **Phase** | La phase sur laquelle le TC est installé. Utilisez `1` pour les installations monophasées. Pour le triphasé, voir [Annexe B](appendices.md#appendix-b-three-phase-configuration) |
| **Reverse** | Inverse le signe d'un canal. Normalement, vous n'en aurez pas besoin : EnergyMe détecte automatiquement les TC inversés à la première activation et les corrige automatiquement. Une bascule manuelle est disponible si nécessaire (par exemple, si le rôle d'un canal change). Inverse le signe instantanément, pas besoin de rouvrir le tableau |
| **Active** | Active le canal. Décochez pour les canaux inutilisés |
| **Grouping** | Les canaux partageant la même valeur de groupe sont combinés en une seule carte sur le tableau de bord principal. Utilisez ceci pour les charges multiphases : définissez les trois TC de phase de votre four à « Oven » et le tableau de bord affichera la puissance totale pour l'ensemble de l'appareil |
| **Role** | Ce que le canal représente (voir le tableau ci-dessous) |

#### Valeurs de Role {#role-values}

| Role | Quand l'utiliser |
| --- | --- |
| `Load` | Un circuit dérivé consommant de l'énergie (cuisine, lumières, appareils électroménagers, borne VE, pompe à chaleur...) |
| `Grid (+ import, - export)` | La ligne d'alimentation principale. Positif lors de l'importation depuis le réseau, négatif lors de l'exportation (par exemple, revente PV) |
| `PV / Solar (+ generation)` | Une chaîne de panneaux solaires mesurée directement (côté AC). Positif lors de la production |
| `Battery (+ discharge, - charge)` | Un système de batterie. Positif lors de la décharge vers la maison, négatif lors de la charge |
| `Inverter (PV + Battery DC-coupled)` | Un onduleur hybride où le PV et la batterie partagent une seule sortie AC |

#### Configuration pour une maison monophasée typique {#configuration-for-a-typical-single-phase-home}

**Channel 0 (ligne principale) :**

| Champ | Valeur |
| --- | --- |
| Label | `Grid` (ou `Main`, ou selon vos préférences) |
| Phase | `1` |
| Reverse | (laisser décoché, corriger plus tard si nécessaire) |
| Active | ☑ |
| Grouping | `Group 0` (par défaut) |
| Role | `Grid (+ import, - export)` |

**Channels 1 à 15 (charges dérivées) :**

| Champ | Valeur |
| --- | --- |
| Label | L'étiquette du disjoncteur depuis l'[Annexe A](appendices.md#appendix-a-channel-map) (par exemple, « Cuisine ») |
| Phase | `1` |
| Reverse | (laisser décoché, corriger plus tard si nécessaire) |
| Active | ☑ pour les canaux avec un TC, ☐ pour les canaux inutilisés |
| Grouping | Un groupe par canal par défaut (par exemple, `Group 1`, `Group 2`...). À modifier uniquement si vous souhaitez combiner des canaux sur le tableau de bord (voir [Annexe B](appendices.md#appendix-b-three-phase-configuration) pour les charges triphasées) |
| Role | `Load` (ou `PV / Solar`, `Battery`, `Inverter` selon le cas) |

Cliquez sur **Save** pour chaque canal.

> **⚠ Configuration triphasée**  
> Si vous avez des charges triphasées (alimentation principale ou dérivations spécifiques), les champs **Phase** et **Grouping** doivent être configurés avec soin. Voir **[Annexe B](appendices.md#appendix-b-three-phase-configuration)**.

### 4.6 Étalonnage des TC : uniquement avec des TC non standard {#46-ct-calibration-only-if-using-non-standard-cts}

> **ⓘ Pour les TC standard 30 A (ceux inclus dans le kit)**  
> Les appareils EnergyMe sont étalonnés en usine. Si vous utilisez les TC 30 A fournis, **ignorez entièrement cette section ;** aucune action d'étalonnage n'est nécessaire. De légères différences par rapport à un compteur de référence (bien inférieures à 1 %) sont attendues et normales.

Vous devez visiter la page **Calibration** si :

- Vous avez commandé des TC de calibre supérieur (75 A, 150 A) pour des circuits à fort courant ; **ou**
- Vous utilisez une construction communautaire avec des TC auto-fournis.

#### Comment étalonner {#how-to-calibrate}

1. Dans le menu supérieur, allez dans **Calibration**.
2. Sélectionnez le canal que vous voulez étalonner dans la liste déroulante.
3. Définissez **CT Current Rating (A)** à la valeur imprimée sur le corps du TC (par exemple `75` pour un TC 75 A). Ceci est obligatoire dès lors que le TC installé diffère du 30 A par défaut.
4. Définissez **CT Voltage Output (V RMS)** à la tension que le TC produit à son courant nominal : `0.333` V pour les TC standard du kit, ou consultez la fiche technique de votre TC.

   > **⚠ Ne dépassez pas une sortie de 0,5 V à votre courant maximum attendu ;** c'est la limite d'entrée sûre du circuit de mesure ADE7953. Par exemple, un TC 75 A / 1 V convient si le circuit ne dépasse jamais ~37 A (la moitié du courant nominal), mais pas pour un circuit 75 A pleinement chargé.

5. Laissez **Scaling Factor (%)** à `0` sauf si vous corrigez une erreur résiduelle par rapport à un compteur de référence connu. La plage d'ajustement est par pas de 0,1 % ; n'y touchez que si vous disposez d'une référence étalonnée à laquelle comparer.
6. Cliquez sur **Save**.

Répétez pour chaque canal utilisant un TC non standard.

### 4.7 Vérification {#47-verification}

| Vérification | Quand | Attendu | Si incorrect |
| --- | --- | --- | --- |
| Tension | En quelques secondes | Votre tension nominale du réseau (~120 V ou ~230 V selon votre région) | Vérifier la connexion L/N |
| Puissance du Channel 0 (W) lors de l'importation depuis le réseau | En quelques secondes | **Positive** (importation d'énergie) | Si systématiquement négative alors que vous savez que vous importez, cochez **Reverse** pour le Channel 0 |
| Puissance du Channel 0 (W) lors de l'exportation (PV) | En quelques secondes | **Négative** | La convention de signe est correcte : `+ import, - export` |
| Puissance des canaux dérivés (W) sur les canaux `Load` | En quelques secondes | Positive lors de la consommation | Si systématiquement négative, cochez **Reverse** pour ce canal |
| Somme des dérivations `Load` inférieure ou égale à l'importation du Channel 0 | En quelques secondes | Oui | Si les dérivations dépassent le principal, un TC est probablement sur le côté entrée ou sur le peigne de distribution ([§3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-)) |
| Énergie horaire / journalière / mensuelle (kWh) | Après quelques heures | Chiffres qui se remplissent | L'appareil a besoin de temps pour accumuler les données d'énergie ; les totaux se remplissent progressivement sur plusieurs heures |

> **ⓘ NOTE : Ne vous inquiétez pas si le tableau de bord semble vide au début**  
> La puissance instantanée (Watts) apparaît en quelques secondes. **Les valeurs d'énergie agrégées (kWh) prennent quelques heures à se remplir**, car l'appareil doit accumuler des mesures avant d'afficher des tendances. Revenez plus tard dans la journée.

> **ⓘ NOTE : La case Reverse est votre amie**  
> La plupart des TC inversés sont corrigés automatiquement par EnergyMe à la première activation. Mais si un canal lit avec le mauvais signe (par exemple, affiche -8 W alors que vous attendez +8 W), vous n'avez pas besoin de rouvrir le tableau. Cochez simplement **Reverse** pour ce canal dans [§4.5](#45-configure-each-channel) ; le signe s'inverse instantanément.

> **✅ ASTUCE : Le « test de la bouilloire » (vérification instantanée)**  
> Allumez une seule charge à forte puissance sur un circuit connu (par exemple, une bouilloire dans la cuisine). Vous devriez voir la puissance instantanée augmenter **sur ce seul canal dérivé** et sur le Channel 0 d'environ la même quantité.
>
> - Si elle apparaît sur **plusieurs dérivations**, l'un des TC est sur un fil d'entrée partagé. Retournez à [§3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-).
> - Si elle apparaît sur la **mauvaise dérivation**, l'association canal-disjoncteur dans l'interface est incorrecte. Modifiez les étiquettes en [§4.5](#45-configure-each-channel).
> - Si elle apparaît sur **aucune dérivation surveillée** mais sur le Channel 0, ce circuit n'est pas encore surveillé, ce qui est attendu si vous n'y avez pas placé de TC.

### 4.8 Mises à jour du micrologiciel {#48-firmware-updates}

L'appareil vérifie automatiquement les mises à jour disponibles. Lorsqu'une nouvelle version est disponible, une **icône de cloche (🔔)** apparaît à côté du bouton Firmware Update dans la barre de navigation supérieure.

Pour mettre à jour :

1. Allez dans **Update** dans le menu supérieur.
2. Suivez les instructions à l'écran.

> **ⓘ NOTE : Appareils communautaires**  
> Les notifications de mise à jour automatique nécessitent l'activation des services cloud. Pour les appareils communautaires (auto-construits), les mises à jour sont toujours manuelles : téléchargez le dernier binaire du micrologiciel depuis [GitHub Releases](https://github.com/jibrilsharafi/EnergyMe-Home/releases) et chargez-le via la page Update.

### 4.9 Intégrations {#49-integrations}

*EnergyMe Home* prend en charge plusieurs options d'intégration : Custom MQTT, InfluxDB, REST API, Modbus TCP et découverte de service mDNS. Toute la configuration et la documentation pour celles-ci sont disponibles directement dans l'interface web sous **Integrations** dans le menu supérieur ; la page est autonome et inclut tout ce dont vous avez besoin pour vous connecter à des plateformes tierces.

---

**Précédent :** [← Installation](01-installation.md)  
**Suivant :** [Dépannage →](03-troubleshooting.md)
