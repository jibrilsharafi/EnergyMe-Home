<!-- translation
source: appendices.md
source-commit: eda785a
translated: 2026-05-21
-->

# Anhänge

## Anhang A: Kanalübersicht {#appendix-a-channel-map}

Während der Installation ausfüllen (soweit bekannt), ein Foto machen, bevor der Schaltschrank geschlossen wird.

| Channel # | LSS-Bezeichnung | Beschreibung (Raum/Last) | CT-Nennstrom | Role | Phase | Grouping |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Hauptschalter | Eingangsleitung des gesamten Hauses | 30 A | Grid | 1 | Group 0 |
| 1 | | | 30 A | Load | 1 | Group 1 |
| 2 | | | 30 A | Load | 1 | Group 2 |
| 3 | | | 30 A | Load | 1 | Group 3 |
| 4 | | | | | | |
| ... | | | | | | |
| 15 | | | | | | |

---

## Anhang B: Dreiphasen-Konfiguration {#appendix-b-three-phase-configuration}

*EnergyMe Home* ist grundsätzlich ein einphasiges Gerät, kann aber eine **dreiphasige Hauptversorgung** (oder bestimmte dreiphasige Lasten) überwachen, indem **ein CT pro Phase** verwendet und die drei Messwerte auf dem Dashboard über das Feld `Grouping` kombiniert werden.

### B.1 Dreiphasige Hauptversorgung {#b1-three-phase-main-supply}

#### Hardware

1. Nehmen Sie **3 CTs** aus dem Kit (oder Erweiterungs-Kit).
2. Klemmen Sie jeden CT um **einen der drei Phasenleiter** (L1, L2, L3), niemals um den Neutralleiter.
3. Stecken Sie sie in das Gerät:
   - **Phase L1** → Channel `0` (der Haupt-Referenzkanal)
   - **Phase L2** → ein beliebiger freier Abzweigkanal (z. B. Channel `1`)
   - **Phase L3** → ein beliebiger freier Abzweigkanal (z. B. Channel `2`)
4. **Notieren Sie, welche physische Phase an welchem Kanal liegt;** Sie benötigen dies für den Software-Schritt.

#### Software

In **Channels** ([§4.5](02-setup.md#45-configure-each-channel)) richten Sie die drei Kanäle wie folgt ein:

| Channel | Label | Phase | Active | Grouping | Role |
| --- | --- | --- | --- | --- | --- |
| 0 | `Grid L1` | `1` | ☑ | `Grid` | `Grid (+ import, - export)` |
| 1 (oder gewählter Abzweig) | `Grid L2` | `2` | ☑ | `Grid` | `Grid (+ import, - export)` |
| 2 (oder gewählter Abzweig) | `Grid L3` | `3` | ☑ | `Grid` | `Grid (+ import, - export)` |

Die drei Kanäle teilen denselben **Grouping**-Wert (`Grid`), sodass das Dashboard eine einzige "Grid"-Karte mit der **dreiphasigen Gesamtleistung** der Hauptversorgung anzeigt.

> **ⓘ Standardgruppe überschreiben.** Das Grouping-Feld ist standardmäßig mit `Group 0`, `Group 1`, `Group 2` usw. vorausgefüllt. Für die Dreiphasen-Gruppierung müssen Sie den Standardwert auf jedem der drei Kanäle durch den gemeinsamen Gruppennamen (z. B. `Grid`) **ersetzen**.

### B.2 Dreiphasige Abzweiglast (z. B. Wallbox, Wärmepumpe, Ofen) {#b2-three-phase-branch-load-eg-ev-charger-heat-pump-oven}

#### Hardware

1. Nehmen Sie **3 CTs**.
2. Klemmen Sie jeden CT auf eine Phase der Last.
3. Stecken Sie sie in 3 freie Abzweigkanäle (z. B. Channel `3`, `4`, `5`).
4. Notieren Sie, welche Phase an welchem Kanal liegt.

#### Software

Beispiel für eine dreiphasige Wallbox:

| Channel | Label | Phase | Active | Grouping | Role |
| --- | --- | --- | --- | --- | --- |
| 3 | `EV charger L1` | `1` | ☑ | `EV charger` | `Load` |
| 4 | `EV charger L2` | `2` | ☑ | `EV charger` | `Load` |
| 5 | `EV charger L3` | `3` | ☑ | `EV charger` | `Load` |

Das Dashboard zeigt eine einzige "EV charger"-Karte mit der dreiphasigen Gesamtleistung des Geräts an.

> **✅ TIPP: Benennung der Gruppe**  
> Verwenden Sie einen sauberen Gruppennamen (z. B. `Oven`, `Heat pump`, `EV charger`) ohne Phasensuffixe; das ist es, was auf dem Dashboard erscheint. Setzen Sie das Phasensuffix nur in das `Label` jedes Kanals, damit Sie bei Bedarf weiterhin einzelne Phasen inspizieren können.

---

## Anhang C: LED-Statusreferenz {#appendix-c-led-status-reference}

Die LED an der Vorderseite des Geräts kommuniziert den Systemzustand auf einen Blick.

> **ⓘ Boot-Farben**  
> Während der ersten ~10 Sekunden nach dem Einschalten durchläuft die LED kurz Gelb, Orange, Violett und andere Farben. **Das ist normal.** Jede Farbe markiert eine interne Startphase. Warten Sie, bis sich die LED stabilisiert, bevor Sie irgendwelche Schlüsse ziehen.

### Normalbetrieb {#normal-operation}

| LED | Zustand | Bedeutung |
| --- | --- | --- |
| 🟢 Grün, dauerhaft | Verbunden | WLAN verbunden, Überwachung läuft normal |
| 🔵 Blau, langsames Pulsieren | Verbindung wird hergestellt | WLAN-Zugangsdaten konfiguriert, aber derzeit nicht verbunden (erster Start, Router startet noch oder temporärer Verbindungsverlust). Stellt sich automatisch wieder her; normalerweise innerhalb von 60 s gelöst |
| 🔵 Blau, schnelles Blinken | Portal aktiv | Kein WLAN konfiguriert; Captive Portal ist geöffnet ([§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal)) |

### Alarme {#alerts}

| LED | Zustand | Bedeutung | Was zu tun ist |
| --- | --- | --- | --- |
| 🟣 Violett, dauerhaft | Abgesicherter Modus | Crash-Schutz wurde ausgelöst; das Gerät überwacht weiterhin und ist über die Web-Oberfläche vollständig erreichbar | Gehen Sie im oberen Menü zu **Logs**, um zu untersuchen; das Gerät erholt sich automatisch |
| 🔴 Rot, schnelles Blinken | Kritischer Fehler | Anhaltender Ausfall nach Wiederherstellungsversuchen | Neustart über die Taste ([Anhang D](#appendix-d-user-button-reference)); wenn dies erneut auftritt, Support kontaktieren |

### Tastenfeedback (während die Taste gehalten wird; siehe [Anhang D](#appendix-d-user-button-reference)) {#button-feedback-while-the-button-is-held-see-appendix-d}

| LED-Farbe beim Halten | Aktion, die beim Loslassen ausgelöst wird |
| --- | --- |
| ⚪ Weiß | Druck registriert, aber unter Aktionsschwelle. Loslassen ohne Aktion |
| 🔵 Cyan | Neustart |
| 🟡 Gelb | Passwort-Reset auf Standardwert |
| 🟠 Orange | WLAN-Reset (öffnet das Captive Portal erneut) |
| 🔴 Rot | Werksreset: alle Daten und Einstellungen werden gelöscht |
| ⚪ Weiß (wieder) | Zu lange gehalten. Loslassen und erneut versuchen |

---

## Anhang D: Tastenreferenz für Benutzer {#appendix-d-user-button-reference}

Die Taste an der Vorderseite des Geräts ermöglicht es Ihnen, sich aus häufigen Situationen ohne Telefon oder Laptop zu erholen.

**So funktioniert es:** drücken und halten. Die LED wechselt die Farbe, je länger Sie halten, und zeigt an, welche Aktion beim Loslassen ausgelöst wird. **Lassen Sie die Taste los, sobald Sie die Farbe für die gewünschte Aktion sehen.**

| Haltezeit | LED-Farbe | Aktion beim Loslassen |
| --- | --- | --- |
| < 2 s | Weiß | Keine Aktion |
| 2-5 s | **Cyan** | **Neustart:** entspricht einem Strom-Neustart |
| 5-10 s | **Gelb** | **Passwort-Reset:** setzt das Web-Passwort auf `energyme` zurück |
| 10-15 s | **Orange** | **WLAN-Reset:** löscht die WLAN-Zugangsdaten und öffnet das Captive Portal erneut ([§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal)). Energiedaten und Kanalkonfiguration bleiben erhalten |
| 15-20 s | **Rot** | **Werksreset** ⚠: löscht alle Daten, Konfiguration und Anmeldedaten. Nur als letzter Ausweg verwenden |
| > 20 s | Weiß | Keine Aktion. Loslassen und erneut versuchen |

> **⚠ WARNUNG: Werksreset ist unumkehrbar**  
> Ein Werksreset löscht alle angesammelten Energiedaten, Kanalnamen, Kalibrierung, WLAN-Zugangsdaten und Ihr benutzerdefiniertes Passwort. Es gibt kein Rückgängigmachen. Halten Sie nur bis Rot, wenn Sie sich sicher sind.

> **✅ TIPP: Farbe als Bestätigung**  
> Sie müssen keine Sekunden zählen. Beobachten Sie die LED: lassen Sie in dem Moment los, in dem sie die gewünschte Farbe zeigt. Wenn Sie versehentlich wieder zu Weiß durchhalten, halten Sie weiter; lassen Sie einfach los, ohne eine Aktion auszulösen. Versuchen Sie es dann von vorne.

---

**Vorherige:** [← Fehlerbehebung](03-troubleshooting.md)
