<!-- translation
source: 02-setup.md
source-commit: 3207d0e
translated: 2026-05-21
-->

# Setup

Ab jetzt kann die Arbeit vom Endnutzer ausgeführt werden. Der Schaltschrank kann geschlossen bleiben.

---

## 4. Softwarekonfiguration {#4-software-configuration}

### 4.1 Mit dem WLAN des Geräts verbinden (Captive Portal) {#41-connect-to-the-devices-wi-fi-captive-portal}

Beim ersten Einschalten oder nach einem WLAN-Reset sendet das Gerät sein eigenes WLAN-Netzwerk aus, damit Sie es konfigurieren können.

1. Öffnen Sie auf Ihrem Telefon oder Laptop die WLAN-Einstellungen.
2. Suchen Sie nach einem Netzwerk namens **`EnergyMe-<DEVICE_ID>`**, wobei `<DEVICE_ID>` der 12-stellige Code ist, der auf dem Etikett im Inneren des Gerätegehäuses aufgedruckt ist.
3. Verbinden Sie sich damit. Das Passwort lautet:
   - **EnergyMe-Geräte** (von uns gekauft): aufgedruckt auf den Aufklebern am Gehäuse, sowohl als scannbarer QR-Code als auch als Klartext.
   - **Community-Geräte** (selbstgebaut): die `DEVICE_ID` selbst (derselbe 12-stellige Code wie der Netzwerkname).
4. Eine Konfigurationsseite öffnet sich automatisch. Falls nicht, öffnen Sie einen Browser und navigieren Sie zu **`http://192.168.4.1`**.

> **ⓘ HINWEIS: Wenn das Netzwerk nicht erscheint**  
> Warten Sie 60 Sekunden nach dem Einschalten. Wenn es immer noch nicht sichtbar ist, halten Sie die Taste 10-15 Sekunden gedrückt, bis die LED orange leuchtet (WLAN-Reset), und lassen Sie dann los. Das Gerät startet neu und öffnet das Portal erneut. Siehe **[Anhang D](appendices.md#appendix-d-user-button-reference)** für die vollständige Tastenreferenz.

### 4.2 Heim-WLAN konfigurieren {#42-configure-your-home-wi-fi}

Auf der Captive-Portal-Seite:

1. Gehen Sie zu **Configure Wi-Fi**.
2. Wählen Sie Ihr Heim-WLAN-Netzwerk aus der Liste.
3. Geben Sie Ihr WLAN-Passwort ein.
4. Tippen Sie auf **Save**.
5. Das Gerät startet neu und verbindet sich mit Ihrem Heimnetzwerk, was etwa 30 Sekunden dauert. Die LED hört auf, blau zu blinken, und stabilisiert sich auf **dauerhaft Grün**, sobald die Verbindung hergestellt ist.

> **✅ TIPP: Nur 2,4 GHz**  
> *EnergyMe Home* verwendet **2,4-GHz-WLAN**. Wenn Ihr Router separate 2,4-GHz- und 5-GHz-Netzwerke anzeigt, wählen Sie das 2,4-GHz-Netzwerk. Wenn Ihr Router einen einzigen kombinierten Namen verwendet ("Band Steering"), funktioniert es normalerweise problemlos, aber bei Verbindungsproblemen lassen Sie das 2,4-GHz-Band über die Admin-Seite Ihres Routers als separate SSID bereitstellen.

### 4.3 Auf die Web-Oberfläche zugreifen {#43-access-the-web-interface}

1. Öffnen Sie auf einem Telefon oder Laptop, das mit **demselben Heim-WLAN** verbunden ist, einen Browser.
2. Navigieren Sie zu **`http://energyme.local`**.
3. Melden Sie sich mit den Standardanmeldedaten an:
   - **Username:** `admin`
   - **Password:** `energyme`

> **✅ TIPP: Live-Demo**  
> Ein vollständig konfiguriertes Gerät ist online verfügbar unter **[demo-energyme-home.energyme.net](https://demo-energyme-home.energyme.net/)**. Verwenden Sie es, um die Oberfläche zu erkunden, mit Ihrem eigenen Dashboard zu vergleichen oder sich vor der Installation mit dem Layout vertraut zu machen.

> **ⓘ HINWEIS: IP-Adresse notieren**  
> Nach der Anmeldung gehen Sie im oberen Menü zu **Info**. Unter **Network Status** sehen Sie die lokale IP-Adresse des Geräts (z. B. `192.168.1.42`). **Notieren Sie sie oder setzen Sie ein Lesezeichen.** Wenn `energyme.local` in Zukunft nicht mehr aufgelöst werden kann (manche Router, PCs und ältere Android-Versionen behandeln `.local`-Namen nicht zuverlässig), können Sie die Oberfläche jederzeit direkt erreichen, indem Sie diese IP-Adresse in Ihren Browser eingeben.
>
> Wenn Sie die IP noch nicht notiert haben und `energyme.local` nicht mehr funktioniert, öffnen Sie die Admin-Seite Ihres Routers und suchen Sie nach einem Gerät mit dem Hostnamen `energyme-home-<DEVICE_ID>`.

### 4.4 Standardpasswort ändern {#44-change-the-default-password}

> **⚠ Tun Sie dies vor allem anderen.** Das Gerät wird mit einem bekannten Standardpasswort ausgeliefert. Bis Sie es ändern, kann jeder in Ihrem Heimnetzwerk auf die Oberfläche zugreifen.

1. Gehen Sie im oberen Menü zu **Configuration**.
2. Scrollen Sie zu **Change Password**.
3. Geben Sie das aktuelle Passwort (`energyme`) ein, dann zweimal Ihr neues Passwort. Das neue Passwort muss mindestens 8 Zeichen lang sein.
4. Klicken Sie auf **Save**.

Das Gerät erinnert Sie mit einem Pop-up auf dem Dashboard bei jedem Öffnen, bis das Passwort geändert wurde.

> **ⓘ HINWEIS: Passwort vergessen?**  
> Halten Sie die Taste 5-10 Sekunden gedrückt, bis die LED gelb leuchtet (Passwort-Reset), und lassen Sie dann los. Das Passwort wird auf `energyme` zurückgesetzt. Melden Sie sich wieder an und setzen Sie sofort ein neues. Siehe **[Anhang D](appendices.md#appendix-d-user-button-reference)** für die vollständige Tastenreferenz.

### 4.5 Jeden Kanal konfigurieren {#45-configure-each-channel}

Nun ordnen Sie jeden physischen CT einem aussagekräftigen Namen und einer Rolle im UI zu. Gehen Sie im oberen Menü zu **Channels** und konfigurieren Sie jeden Kanal, an dem ein CT angeschlossen ist.

Jeder Kanal hat die folgenden Felder:

| Feld | Bedeutung |
| --- | --- |
| **Label** | Ein freier Textname (z. B. "Grid", "Kitchen", "EV charger"). Es ist der Name, der auf dem Dashboard angezeigt wird |
| **Phase** | Die AC-Phase, auf der der CT angeklemmt ist. Verwenden Sie `1` für europäische einphasige Systeme und für 120-V-Standardlasten in Nordamerika. Verwenden Sie `2` oder `3` für die beiden anderen Phasen eines Dreiphasensystems (siehe [Anhang B](appendices.md#appendix-b-three-phase-configuration)). Verwenden Sie **`240V (Split Phase)`** für einen nordamerikanischen 240-V-Stromkreis, der beide Schenkel L1-L2 umspannt (siehe [Anhang B.3](appendices.md#b3-north-american-120240-v-split-phase)); die Firmware wendet automatisch einen 2×-Spannungsmultiplikator an, da die Spannungsreferenz Phase-zu-Neutralleiter (≈120 V) ist, während der Stromkreis Phase-zu-Phase (240 V) verläuft. |
| **Reverse** | Kehrt das Vorzeichen eines Kanals um. Normalerweise nicht nötig: EnergyMe erkennt umgekehrt installierte CTs automatisch bei der ersten Aktivierung und korrigiert sie automatisch. Manuelles Umschalten ist bei Bedarf verfügbar (z. B. wenn sich eine Kanalrolle ändert). Kehrt das Vorzeichen sofort um, kein erneutes Öffnen des Schaltschranks nötig |
| **Active** | Aktiviert den Kanal. Bei nicht verwendeten Kanälen entfernen |
| **Grouping** | Kanäle mit derselben Gruppenbezeichnung werden auf dem Home-Dashboard zu einer Karte zusammengefasst. Verwenden Sie dies für mehrphasige Lasten: Setzen Sie alle drei Phasen-CTs Ihres Ofens auf "Oven", und das Dashboard zeigt die Gesamtleistung für das gesamte Gerät an |
| **Role** | Was der Kanal repräsentiert (siehe Tabelle unten) |

#### Werte für Role {#role-values}

| Role | Wann verwenden |
| --- | --- |
| `Load` | Ein Abzweigstromkreis, der Energie verbraucht (Küche, Beleuchtung, Geräte, Wallbox, Wärmepumpe...) |
| `Grid (+ import, - export)` | Die Hauptversorgungsleitung. Positiv beim Bezug aus dem Stromnetz, negativ bei Einspeisung (z. B. PV-Rückeinspeisung) |
| `PV / Solar (+ generation)` | Ein direkt gemessener Solarpanel-String (AC-Seite). Positiv bei Erzeugung |
| `Battery (+ discharge, - charge)` | Ein Batteriesystem. Positiv beim Entladen ins Haus, negativ beim Laden |
| `Inverter (PV + Battery DC-coupled)` | Ein Hybrid-Wechselrichter, bei dem PV und Batterie einen gemeinsamen AC-Ausgang teilen |

#### Konfiguration für ein typisches einphasiges Zuhause {#configuration-for-a-typical-single-phase-home}

**Channel 0 (Hauptleitung):**

| Feld | Wert |
| --- | --- |
| Label | `Grid` (oder `Main`, oder nach Wunsch) |
| Phase | `1` |
| Reverse | (deaktiviert lassen, später falls nötig korrigieren) |
| Active | ☑ |
| Grouping | `Group 0` (Standard) |
| Role | `Grid (+ import, - export)` |

**Channel 1 bis 15 (Abzweiglasten):**

| Feld | Wert |
| --- | --- |
| Label | Die LSS-Bezeichnung aus [Anhang A](appendices.md#appendix-a-channel-map) (z. B. "Kitchen") |
| Phase | `1` |
| Reverse | (deaktiviert lassen, später falls nötig korrigieren) |
| Active | ☑ für Kanäle mit CT, ☐ für nicht verwendete Kanäle |
| Grouping | Standardmäßig eine Gruppe pro Kanal (z. B. `Group 1`, `Group 2`...). Nur ändern, wenn Sie Kanäle auf dem Dashboard kombinieren möchten (siehe [Anhang B](appendices.md#appendix-b-three-phase-configuration) für dreiphasige Lasten) |
| Role | `Load` (oder `PV / Solar`, `Battery`, `Inverter` falls zutreffend) |

Klicken Sie für jeden Kanal auf **Save**.

> **⚠ Dreiphasige Konfiguration**  
> Wenn Sie dreiphasige Lasten haben (Hauptversorgung oder bestimmte Abzweige), müssen die Felder **Phase** und **Grouping** sorgfältig konfiguriert werden. Siehe **[Anhang B](appendices.md#appendix-b-three-phase-configuration)**.

### 4.6 CT-Kalibrierung: nur bei Verwendung nicht standardmäßiger CTs {#46-ct-calibration-only-if-using-non-standard-cts}

> **ⓘ Für Standard-30-A-CTs (die im Kit enthaltenen)**  
> EnergyMe-Geräte werden werkseitig kalibriert. Wenn Sie die mitgelieferten 30-A-CTs verwenden, **überspringen Sie diesen Abschnitt vollständig;** es ist keine Kalibrierung erforderlich. Geringe Abweichungen von einem Referenzzähler (deutlich unter 1 %) sind zu erwarten und normal.

Sie müssen die Seite **Calibration** nur aufrufen, wenn:

- Sie CTs mit höherer Nennstromstärke (75 A, 150 A) für Hochstromkreise bestellt haben; **oder**
- Sie einen Community-Build mit selbst beschafften CTs verwenden.

#### So kalibrieren Sie {#how-to-calibrate}

1. Gehen Sie im oberen Menü zu **Calibration**.
2. Wählen Sie den zu kalibrierenden Kanal aus dem Dropdown-Menü.
3. Setzen Sie **CT Current Rating (A)** auf den auf dem CT-Gehäuse aufgedruckten Wert (z. B. `75` für einen 75-A-CT). Dies ist obligatorisch, wenn der installierte CT vom Standard 30 A abweicht.
4. Setzen Sie **CT Voltage Output (V RMS)** auf die Spannung, die der CT bei seinem Nennstrom ausgibt: `0.333` V für die Standard-CTs im Kit, oder prüfen Sie das Datenblatt Ihres CT.

   > **⚠ Überschreiten Sie nicht 0,5 V Ausgangsspannung bei Ihrem maximal erwarteten Strom;** dies ist die sichere Eingangsgrenze des Mess-IC ADE7953. Ein 75 A / 1 V CT ist beispielsweise in Ordnung, wenn der Stromkreis niemals ~37 A (halber Nennstrom) überschreitet, aber nicht für einen vollständig belasteten 75-A-Stromkreis.

5. Lassen Sie **Scaling Factor (%)** auf `0`, es sei denn, Sie korrigieren einen Restfehler gegen einen bekannten Referenzzähler. Der Einstellbereich erfolgt in 0,1 %-Schritten; berühren Sie dies nur, wenn Sie eine kalibrierte Referenz zum Vergleich haben.
6. Klicken Sie auf **Save**.

Wiederholen Sie dies für jeden Kanal, der einen nicht standardmäßigen CT verwendet.

### 4.7 Überprüfung {#47-verification}

| Prüfung | Wann | Erwartet | Falls falsch |
| --- | --- | --- | --- |
| Spannung | Innerhalb von Sekunden | Ihre Nenn-Netzspannung (~120 V oder ~230 V je nach Region) | L/N-Anschluss erneut prüfen |
| Channel-0-Leistung (W) beim Netzbezug | Innerhalb von Sekunden | **Positiv** (Energiebezug) | Wenn dauerhaft negativ, obwohl Sie wissen, dass Sie beziehen, **Reverse** für Channel 0 aktivieren |
| Channel-0-Leistung (W) bei Einspeisung (PV) | Innerhalb von Sekunden | **Negativ** | Vorzeichenkonvention ist korrekt: `+ import, - export` |
| Leistung der Abzweigkanäle (W) bei `Load`-Kanälen | Innerhalb von Sekunden | Positiv bei Verbrauch | Wenn dauerhaft negativ, **Reverse** für diesen Kanal aktivieren |
| Summe der `Load`-Abzweige ist kleiner oder gleich dem Channel-0-Bezug | Innerhalb von Sekunden | Ja | Wenn die Abzweige den Hauptwert überschreiten, sitzt wahrscheinlich ein CT auf der Eingangsseite oder Kammschiene ([§3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-)) |
| Stündliche / tägliche / monatliche Energie (kWh) | Nach einigen Stunden | Werte füllen sich | Das Gerät braucht Zeit, um Energiedaten zu akkumulieren; die Summen füllen sich schrittweise über Stunden |

> **ⓘ HINWEIS: Keine Sorge, wenn das Dashboard zunächst leer aussieht**  
> Die momentane Leistung (Watt) erscheint innerhalb von Sekunden. **Aggregierte Energiewerte (kWh) brauchen einige Stunden, um sich zu füllen**, da das Gerät Messungen akkumulieren muss, bevor es Trends anzeigt. Schauen Sie später am Tag noch einmal vorbei.

> **ⓘ HINWEIS: Das Reverse-Kontrollkästchen ist Ihr Freund**  
> Die meisten umgekehrt installierten CTs werden von EnergyMe bei der ersten Aktivierung automatisch korrigiert. Falls jedoch ein Kanal jemals mit falschem Vorzeichen anzeigt (z. B. -8 W, wo Sie +8 W erwarten), müssen Sie den Schaltschrank nicht erneut öffnen. Aktivieren Sie einfach **Reverse** für diesen Kanal in [§4.5](#45-configure-each-channel); das Vorzeichen wird sofort umgekehrt.

> **✅ TIPP: Der "Wasserkocher-Test" (sofortige Überprüfung)**  
> Schalten Sie eine einzelne Hochleistungslast an einem bekannten Stromkreis ein (z. B. einen Wasserkocher in der Küche). Sie sollten sehen, wie die momentane Leistung **auf diesem einen Abzweigkanal** und auf Channel 0 um etwa denselben Wert steigt.
>
> - Wenn sie auf **mehreren Abzweigen** erscheint, sitzt einer der CTs auf einer gemeinsamen Eingangsleitung. Gehen Sie zurück zu [§3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-).
> - Wenn sie auf dem **falschen Abzweig** erscheint, ist die Kanal-zu-LSS-Zuordnung im UI falsch. Bearbeiten Sie die Bezeichnungen in [§4.5](#45-configure-each-channel).
> - Wenn sie auf **keinem überwachten Abzweig**, aber auf Channel 0 erscheint, wird dieser Stromkreis noch nicht überwacht, was zu erwarten ist, wenn Sie keinen CT daran angebracht haben.

### 4.8 Firmware-Updates {#48-firmware-updates}

Das Gerät prüft automatisch auf verfügbare Firmware-Updates. Wenn eine neue Version verfügbar ist, erscheint ein **Glockensymbol (🔔)** neben der Firmware-Update-Schaltfläche in der oberen Navigationsleiste.

So aktualisieren Sie:

1. Gehen Sie im oberen Menü zu **Update**.
2. Folgen Sie den Anweisungen auf dem Bildschirm.

> **ⓘ HINWEIS: Community-Geräte**  
> Automatische Update-Benachrichtigungen erfordern aktivierte Cloud-Dienste. Bei Community-Geräten (selbstgebaut) erfolgen Updates immer manuell: Laden Sie die neueste Firmware-Binärdatei von [GitHub Releases](https://github.com/jibrilsharafi/EnergyMe-Home/releases) herunter und laden Sie sie über die Update-Seite hoch.

### 4.9 Integrationen {#49-integrations}

*EnergyMe Home* unterstützt mehrere Integrationsoptionen: Custom MQTT, InfluxDB, REST API, Modbus TCP und mDNS-Diensterkennung. Die gesamte Konfiguration und Dokumentation hierfür ist direkt in der Web-Oberfläche unter **Integrations** im oberen Menü verfügbar; die Seite ist eigenständig und enthält alles, was Sie zur Anbindung an Drittanbieter-Plattformen benötigen.

---

**Vorherige:** [← Installation](01-installation.md)  
**Nächste:** [Fehlerbehebung →](03-troubleshooting.md)
