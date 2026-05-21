# Installation

> Lesen Sie die [Sicherheitspräambel](README.md#before-you-start-read-this-page), bevor Sie beginnen.

---

## 1. Was ist in der Verpackung {#1-whats-in-the-box}

### 1.1 Starter-Kit (immer enthalten) {#11-starter-kit-always-included}

| # | Artikel | Stück | Hinweise |
| --- | --- | --- | --- |
| 1 | *EnergyMe Home*-Gerät | 1 | Vormontiert, 3-Modul-DIN-Schienenmontage, mit bereits angeschlossenen L/N-Leitungen. **Die Kanäle sind auf den vorderen Aufklebern von 0 bis 15 nummeriert** (insgesamt 16 Kanäle, 4 oben und 12 unten) |
| 2 | Klemmstromwandler (30 A, 1 m Kabel, 3,5 mm Klinkenbuchse) | 4 | Alle identisch; die Nennstromstärke "30 A" ist auf jedem Klemmgehäuse aufgedruckt. 1 wird für **Channel 0** (Hauptleitung) und 3 für Abzweigstromkreise verwendet |

Sie finden außerdem einen **"Let's get started"-Aufkleber mit QR-Code** auf der Innenseite des Verpackungsdeckels.

> *Und wenn Sie diese Anleitung lesen: gut gemacht, Sie haben den QR-Code bereits gefunden. 🎯*

### 1.2 Erweiterungs-Kit (optional, falls bestellt) {#12-expansion-kit-optional-if-ordered}

Wenn Sie zusätzliche Stromwandler (CTs) bestellt haben, um mehr als 3 Abzweigstromkreise zu überwachen, finden Sie diese in einer separaten Tüte in der Verpackung. Das Gerät unterstützt insgesamt bis zu **16 Kanäle** (Channel 0 für die Hauptleitung und Channel 1 bis 15 für Abzweige).

> **ⓘ HINWEIS: Zu den CTs**  
> Die Standard-CTs haben eine **Nennstromstärke von 30 A** (auf jeder Klemme aufgedruckt). Sie funktionieren problemlos für **einphasige Systeme bis ~7 kW bei 230 V oder ~3,5 kW bei 120 V**.
>
> Bevor Sie einen CT anbringen, prüfen Sie die auf dem zu überwachenden Leitungsschutzschalter aufgedruckte Nennstromstärke; sie darf die Nennstromstärke des CT nicht überschreiten. Wenn Sie Stromkreise mit höherem Strom haben (Netzteile, Wärmepumpe) oder eine dreiphasige Versorgung, benötigen Sie möglicherweise CTs mit höherer Nennstromstärke (75 A oder 150 A). Kontaktieren Sie uns unter `support@energyme.net`, um diese zu bestellen.

> **ⓘ HINWEIS: Netzfrequenz**  
> *EnergyMe Home* funktioniert sowohl in **50 Hz**- (Europa, Asien, Afrika, Ozeanien) als auch in **60 Hz**-Netzen (Nordamerika, Teile Südamerikas und Japan) ohne zusätzliche Konfiguration.

> **ⓘ HINWEIS: Wenn etwas fehlt oder beschädigt ist**  
> Installieren Sie das Gerät nicht. Kontaktieren Sie den Support unter `support@energyme.net` mit einem Foto des Verpackungsinhalts.

> **⚠ Dreiphasige Lasten: Lesen Sie dies, bevor Sie fortfahren**  
> Wenn Ihre Hauptversorgung **dreiphasig** ist oder wenn Sie **bestimmte dreiphasige Lasten** überwachen möchten (z. B. eine dreiphasige Wallbox), unterscheidet sich die Verdrahtung leicht. **Lesen Sie [Anhang B](appendices.md#appendix-b-three-phase-configuration), bevor Sie mit der Installation beginnen.**

---

## 2. Was Sie benötigen (nicht enthalten) {#2-what-you-need-not-included}

**Der Elektriker benötigt:**

- Isolierten Schraubendreher
- **3 freie zusammenhängende DIN-Module** in Ihrem Schaltschrank

**Sie benötigen:**

- Ein Smartphone, Tablet oder Laptop mit WLAN (2,4 GHz)
- Den Namen (SSID) und das Passwort Ihres Heim-WLAN

---

## 3. Elektrische Installation {#3-electrical-installation}

> **Bevor Sie den Schaltschrank öffnen:**
>
> 1. Öffnen Sie die Schaltschrankabdeckung und identifizieren Sie visuell **3 freie zusammenhängende DIN-Module** für das Gerät.
> 2. **Schalten Sie den Hauptschalter AUS.**
>
> Ab diesem Punkt ist die Arbeit wirklich einfach; die meisten Installateure schließen sie in **10 bis 15 Minuten** ab.

> **⚠ WARNUNG: Ab diesem Abschnitt muss die Arbeit von einem qualifizierten Elektriker ausgeführt werden.**  
> Überprüfen Sie mit einem Spannungsprüfer die Spannungsfreiheit an jedem Leiter, den Sie berühren werden.

### 3.1 Gerät auf der DIN-Schiene montieren {#31-mount-the-device-on-the-din-rail}

1. Hängen Sie in dem von Ihnen identifizierten 3-Modul-Platz zuerst die **Oberseite** des Geräts auf die Schiene ein.
2. Drücken Sie die **Unterseite**, bis die Federklemme einrastet.
3. Ziehen Sie leicht nach unten, um sicherzustellen, dass das Gerät fest sitzt.

### 3.2 Spannungsversorgung anschließen (L und N) {#32-connect-the-power-supply-l-and-n}

Das Gerät wird mit **zwei bereits angeschlossenen Leitungen** an der internen Spannungsklemme geliefert: einer **braunen Leitung (Phase)** und einer **blauen Leitung (Neutralleiter)**. Sie müssen lediglich die freien Enden dieser Leitungen mit den **nächstgelegenen Phase- und Neutralleiter-Anschlüssen** im Schaltschrank verbinden.

1. Verbinden Sie die **braune Leitung (Phase)** mit dem nächstgelegenen verfügbaren Phasenanschluss, typischerweise der Phasenseite eines Leitungsschutzschalters (LSS) im Schaltschrank.
2. Verbinden Sie die **blaue Leitung (Neutralleiter)** mit der **Neutralleiterschiene** des Schaltschranks (der gemeinsamen Neutralleiter-Klemmleiste).
3. Ziehen Sie die Schrauben fest an.

> **ⓘ HINWEIS: Eigene Verdrahtung**  
> Wenn die mitgelieferte Verdrahtung für Ihre Anwendung nicht geeignet ist, können Sie eigene Leitungen verwenden.
>
> Heben Sie dazu die Kunststoffabdeckung der Spannungsklemme am Gerät an, lösen Sie die braune und blaue Leitung und schließen Sie Ihre eigenen Leitungen an dieselben Klemmen an, dabei die gleiche Phase/Neutralleiter-Konvention beachten. Setzen Sie dann die Abdeckung wieder auf.
>
> Verwenden Sie unbedingt Leitungen mit geeignetem Querschnitt und geeigneter Isolierung für Netzspannung.

![Anschluss der L- und N-Leitungen](../assets/connection_l_n.jpg)

### 3.3 CT an Channel 0 installieren (Hauptleitung) {#33-install-the-ct-on-channel-0-main-line}

Der CT an **Channel 0** ist der genaueste Kanal und sollte zur Messung der gesamten in Ihr Zuhause eingehenden Energie verwendet werden.

1. Nehmen Sie **einen** der Klemmstromwandler aus dem Kit (egal welchen, sie sind alle identisch).
2. Identifizieren Sie den **Hauptleitungsleiter** nach dem Hauptschalter (typischerweise die braune/schwarze Leitung vom kWh-Zähler in den Schaltschrank).
3. Öffnen Sie den Klemmstromwandler.
4. Klemmen Sie ihn **entweder nur um den Phasenleiter oder nur um den Neutralleiter** - niemals um beide zusammen. Werden L und N gemeinsam umklemmt, heben sich die Magnetfelder auf und die Messung geht gegen null.
5. Schließen Sie die Klemme, bis Sie das Klicken hören/spüren.
6. Stecken Sie die 3,5 mm-Klinke in die mit **`0`** beschriftete Buchse am Gerät (die Kanalnummern sind auf dem oberen vorderen Aufkleber aufgedruckt).

> **ⓘ HINWEIS: Klemmrichtung spielt keine Rolle**  
> Die Klemmstromwandler sind nicht richtungsabhängig. Falls Sie später (in [§4.7](02-setup.md#47-verification)) feststellen, dass die Messwerte eines Kanals invertiert sind (z. B. negativ, wenn Sie positiv erwarten), müssen Sie den Schaltschrank nicht erneut öffnen. Aktivieren Sie einfach das Kontrollkästchen **Reverse** für diesen Kanal in der Web-Oberfläche und das Vorzeichen wird sofort umgekehrt.

> **⚠ Dreiphasige Hauptversorgung**  
> Wenn Ihre Hauptversorgung dreiphasig ist, kommt der CT an Channel 0 auf **eine der drei Phasen**, und Sie benötigen zusätzliche CTs für die anderen beiden Phasen (an freien Abzweigkanälen). Siehe **[Anhang B](appendices.md#appendix-b-three-phase-configuration)**.

### 3.4 CTs an den Abzweigkanälen (1 bis 15) installieren: Kritischer Schritt ⚠ {#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-}

Hier passieren die meisten Installationsfehler. **Lesen Sie diesen Abschnitt, bevor Sie etwas anklemmen.**

#### 3.4.1 Das Problem der "gemeinsamen Eingangsleitung" {#341-the-shared-input-wire-problem}

In einem Schaltschrank können Leitungsschutzschalter auf der **Eingangsseite** in **Reihenschaltung** versorgt werden: die Versorgungsleitung geht in LSS #1, dann zu LSS #2, dann zu LSS #3 usw. Dies hat erhebliche Folgen für die Messung:

> **⚠ Die Leitung auf der EINGANGSSEITE (oben) eines LSS führt den Strom DIESES LSS UND aller in der Kette nachgeschalteten LSS.**  
> Wenn Sie den CT auf die Eingangsleitung klemmen, messen Sie die **Summe mehrerer Stromkreise**, was falsch ist.

Die Leitung auf der **AUSGANGSSEITE (Lastseite, unten)** des LSS führt **nur** den Strom der an diesem spezifischen LSS angeschlossenen Lasten. **Hier muss der CT angebracht werden.**

![CT-Platzierung](../assets/ct_placement_breaker.svg)

#### 3.4.2 Die Regel {#342-the-rule}

> **Klemmen Sie die Abzweig-CTs immer auf der AUSGANGSSEITE (Lastseite) des LSS an, d. h. auf der Leitung, die den LSS verlässt und zu den Lasten im Haus geht. Niemals auf der Eingangsschiene.**

#### 3.4.3 Schritt für Schritt für jeden Abzweig-CT {#343-step-by-step-for-each-branch-ct}

Für jeden Stromkreis, den Sie überwachen möchten:

1. Entscheiden Sie, welchen LSS Sie überwachen möchten (z. B. "Küche", "Beleuchtung", "Waschmaschine").
2. Lokalisieren Sie die **Ausgangs-Leitung** dieses LSS, also diejenige, die die untere Klemme in Richtung der Lasten verlässt. **Nicht** die Eingangsleitung oben.
3. Öffnen Sie den Klemmstromwandler.
4. Klemmen Sie ihn **entweder um den einzelnen Phasenleiter oder den einzelnen Neutralleiter** dieser Ausgangsleitung - niemals um beide zusammen, niemals um den Schutzleiter (gelb/grüne Leitung).
5. Schließen Sie die Klemme, bis sie klickt.
6. Stecken Sie die 3,5 mm-Klinke in eine freie Buchse am Gerät, **nummeriert von 1 bis 15** (die Kanalnummern sind auf den vorderen Aufklebern aufgedruckt).
7. **Wenn Sie wissen, welcher Stromkreis es ist, notieren Sie sich die Kanalnummer und die LSS-Bezeichnung** (z. B. "Channel 2 → Gartenbeleuchtung"). Sie werden dies in [§4.5](02-setup.md#45-configure-each-channel) verwenden, um jedem Kanal im UI einen aussagekräftigen Namen zu geben. Verwenden Sie die Tabelle in [Anhang A](appendices.md#appendix-a-channel-map).

> **✅ TIPP: Sie wissen nicht, welcher LSS was steuert?**  
> Kein Problem. Stecken Sie die CTs in beliebige freie Kanäle und fahren Sie mit der Installation fort. Sobald das Gerät online ist, können Sie die Kanäle jederzeit über die Web-Oberfläche umbenennen.

> **✅ TIPP: Keine Sorge wegen der CT-Ausrichtung**  
> Versehentlich einen CT verkehrt herum installiert? Kein Problem. EnergyMe erkennt umgekehrt installierte CTs automatisch bei der ersten Messung nach Aktivierung eines Kanals und kehrt die Polarität für Sie um. Es ist nicht nötig, den Schaltschrank erneut zu öffnen oder manuell etwas anzupassen.

> **⚠ WARNUNG: Häufige zu vermeidende Fehler**
>
> - ❌ Anklemmen an der **Eingangs**-Leitung eines LSS (misst mehrere Stromkreise)
> - ❌ Anklemmen an der **Phasenschiene/Kammschiene** (misst die Summe aller LSS auf der Kammschiene)
> - ❌ Anklemmen um **beide** L und N (misst null)
> - ❌ Anklemmen um den **Schutzleiter** (misst unter Normalbedingungen null, kann ansonsten Fehlerströme messen)
> - ❌ Erzwingen der Klemme auf einer zu dicken Leitung; die Backen müssen vollständig schließen und klicken

> **⚠ Dreiphasige Abzweiglasten**  
> Wenn eine bestimmte Last, die Sie überwachen möchten, dreiphasig ist (z. B. eine dreiphasige Wallbox oder Wärmepumpe), siehe **[Anhang B](appendices.md#appendix-b-three-phase-configuration)** vor dem Anklemmen.

### 3.5 Letzte Prüfung vor dem Schließen des Schaltschranks {#35-final-check-before-closing-the-panel}

Bevor Sie den Hauptschalter wieder EINSCHALTEN:

| Prüfung | OK? |
| --- | --- |
| Alle CT-Klinken sind vollständig in das Gerät eingesteckt (1 an Channel 0, die anderen an Channel 1-15) | ☐ |
| Braune (L) und blaue (N) Leitungen vom Gerät sind fest angeschlossen, kein Kupfer sichtbar | ☐ |
| Der CT von Channel 0 sitzt am Phasenleiter der Hauptleitung | ☐ |
| Jeder Abzweig-CT sitzt auf der AUSGANGSSEITE seines LSS (nicht auf der Kammschiene) | ☐ |
| Keine Werkzeuge oder Schrauben im Schaltschrank vergessen | ☐ |
| Kanalübersicht ([Anhang A](appendices.md#appendix-a-channel-map)) ausgefüllt (soweit bekannt) und fotografiert | ☐ |
| Schaltschrankabdeckung bereit zum Wiederanbringen | ☐ |

### 3.6 Erstes Einschalten {#36-first-power-on}

1. Schließen Sie die Schaltschrankabdeckung.
2. Schalten Sie den Hauptschalter **EIN**.
3. Beobachten Sie die LED an der Vorderseite des Geräts.

> **ⓘ HINWEIS: Farben der Startsequenz**  
> Während der ersten ~10 Sekunden durchläuft die LED schnell verschiedene Farben (gelb, orange, violett und andere). **Das ist völlig normal;** jede Farbe markiert eine Phase der Startsequenz. Reagieren Sie nicht auf eine dieser Farben; warten Sie, bis sich die LED stabilisiert.

Nach dem Booten sind drei Ergebnisse möglich:

| LED-Verhalten | Bedeutung | Nächster Schritt |
| --- | --- | --- |
| 🔵 Blau, schnelles Blinken | Noch kein WLAN konfiguriert; Captive Portal aktiv | Gehen Sie zu **[§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal)** |
| 🔵 Blau, langsames Pulsieren | Bekanntes WLAN gefunden, aber noch nicht verbunden (z. B. Router startet noch) | Warten Sie bis zu 60 s; die LED sollte sich auf dauerhaft Grün stabilisieren |
| 🟢 Dauerhaft Grün | Mit Heim-WLAN verbunden, Überwachung läuft | Gehen Sie zu **[§4.3](02-setup.md#43-access-the-web-interface)** (überspringen Sie [§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal) und [§4.2](02-setup.md#42-configure-your-home-wi-fi)) |

> **⚠ WARNUNG**  
> Wenn Sie etwas Brennendes riechen, ein Summen hören oder Rauch sehen: **schalten Sie sofort den Hauptschalter AUS** und kontaktieren Sie den Support, bevor Sie irgendetwas anderes tun.

---

**Weiter:** [Setup →](02-setup.md)
