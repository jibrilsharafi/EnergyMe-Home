# Appendici

## Appendice A: Mappa dei canali {#appendix-a-channel-map}

Da compilare durante l'installazione (dove noto), scattare una foto prima di chiudere il quadro.

| Canale # | Etichetta interruttore | Descrizione (locale/carico) | Portata TA | Role | Phase | Grouping |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Interruttore generale | Linea entrante di tutta la casa | 30 A | Grid | 1 | Group 0 |
| 1 | | | 30 A | Load | 1 | Group 1 |
| 2 | | | 30 A | Load | 1 | Group 2 |
| 3 | | | 30 A | Load | 1 | Group 3 |
| 4 | | | | | | |
| ... | | | | | | |
| 15 | | | | | | |

---

## Appendice B: Configurazioni multifase {#appendix-b-three-phase-configuration}

*EnergyMe Home* è fondamentalmente un dispositivo monofase, ma può monitorare un'**alimentazione principale trifase**, **carichi trifase specifici** e **circuiti bifase nordamericani 120/240 V** impostando il campo `Phase` su ciascun canale e combinando le letture sulla dashboard tramite il campo `Grouping`.

### B.1 Alimentazione principale trifase {#b1-three-phase-main-supply}

#### Hardware

1. Prendere **3 TA** dal kit (o dal kit di espansione).
2. Applicare ciascun TA attorno a **uno dei tre conduttori di fase** (L1, L2, L3), mai attorno al neutro.
3. Collegarli al dispositivo:
   - **Fase L1** → Canale `0` (il canale di riferimento principale)
   - **Fase L2** → qualsiasi canale derivato libero (es. Canale `1`)
   - **Fase L3** → qualsiasi canale derivato libero (es. Canale `2`)
4. **Annotare quale fase fisica è su quale canale;** servirà per il passaggio software.

#### Software

In **Channels** ([§4.5](02-setup.md#45-configure-each-channel)), configurare i tre canali in questo modo:

| Canale | Label | Phase | Active | Grouping | Role |
| --- | --- | --- | --- | --- | --- |
| 0 | `Grid L1` | `1` | ☑ | `Grid` | `Grid (+ import, - export)` |
| 1 (o derivato scelto) | `Grid L2` | `2` | ☑ | `Grid` | `Grid (+ import, - export)` |
| 2 (o derivato scelto) | `Grid L3` | `3` | ☑ | `Grid` | `Grid (+ import, - export)` |

I tre canali condividono lo stesso valore di **Grouping** (`Grid`), quindi la dashboard mostrerà un'unica card "Grid" con la **potenza trifase totale** dell'alimentazione principale.

> **ⓘ Sovrascrivere il gruppo predefinito.** Il campo Grouping è precompilato con `Group 0`, `Group 1`, `Group 2`, ecc. per impostazione predefinita. Per il raggruppamento trifase deve **sostituire** il valore predefinito con il nome del gruppo condiviso (es. `Grid`) su ciascuno dei tre canali.

### B.2 Carico derivato trifase (es. EV charger, pompa di calore, forno) {#b2-three-phase-branch-load-eg-ev-charger-heat-pump-oven}

#### Hardware

1. Prendere **3 TA**.
2. Applicare ciascun TA su una fase del carico.
3. Collegarli a 3 canali derivati liberi (es. Canali `3`, `4`, `5`).
4. Annotare quale fase è su quale canale.

#### Software

Esempio per una stazione di ricarica EV trifase:

| Canale | Label | Phase | Active | Grouping | Role |
| --- | --- | --- | --- | --- | --- |
| 3 | `EV charger L1` | `1` | ☑ | `EV charger` | `Load` |
| 4 | `EV charger L2` | `2` | ☑ | `EV charger` | `Load` |
| 5 | `EV charger L3` | `3` | ☑ | `EV charger` | `Load` |

La dashboard mostrerà un'unica card "EV charger" con la potenza trifase totale dell'apparecchio.

> **✅ SUGGERIMENTO: Nominare il gruppo**  
> Usare un nome di gruppo pulito (es. `Oven`, `Heat pump`, `EV charger`) senza suffissi di fase; è quello che apparirà sulla dashboard. Metta il suffisso di fase solo nella `Label` di ciascun canale, in modo da poter comunque ispezionare le singole fasi se necessario.

### B.3 Bifase nordamericano 120/240 V {#b3-north-american-120240-v-split-phase}

Il servizio residenziale standard in Nord America è un sistema **bifase**: un trasformatore con presa centrale fornisce due "semifasi" da 120 V (L1 e L2) più un neutro. La maggior parte dei circuiti funziona fase-neutro a 120 V (illuminazione, prese); i grandi elettrodomestici (piano cottura elettrico, scaldabagno, asciugatrice, climatizzazione centralizzata, EV charger Livello 2) funzionano fase-fase a 240 V tra entrambe le semifasi.

*EnergyMe Home* misura la tensione **fase-neutro** (≈120 V), quindi un TA su un circuito a 240 V sottostimerebbe per impostazione predefinita la potenza di un fattore due. Il firmware gestisce questo aspetto al Suo posto: selezioni **`240V (Split Phase)`** nel campo `Phase` del canale e verrà applicato automaticamente un **moltiplicatore di tensione ×2**.

> **⚠ ATTENZIONE: L'alimentazione L e N del dispositivo stesso deve essere sempre cablata fase-neutro (≈120 V).**  
>
> L'impostazione `240V (Split Phase)` nel campo `Phase` **modifica unicamente il modo in cui viene interpretata la misurazione di corrente del TA di quel singolo canale**. **Non** modifica il modo in cui il dispositivo stesso viene alimentato né il modo in cui la tensione viene misurata internamente.
>
> - I fili **marrone (L)** e **blu (N)** del dispositivo devono sempre essere collegati tra **una semifase a 120 V e il Neutro**, esattamente come per un circuito a 120 V.
> - Non cablare mai L e N del dispositivo tra le due semifasi a 240 V. L'alimentatore interno è certificato 100-240 V AC e non verrebbe danneggiato, ma il **riferimento di tensione utilizzato per calcolare la potenza su ogni canale** non corrisponderebbe più all'assunzione del firmware e **tutte le misurazioni sarebbero errate**.
> - Questo vale indipendentemente da quanti canali siano impostati su `240V (Split Phase)`: il cablaggio di alimentazione del dispositivo è indipendente dall'impostazione `Phase` di ciascun canale.

#### Quando usare ciascuna impostazione di Phase (Nord America) {#when-to-use-each-phase-setting-north-america}

| Tipo di circuito | Su cosa è applicato il TA | Impostazione Phase |
| --- | --- | --- |
| Derivato a 120 V (una semifase, fase-neutro) | Una delle due semifasi del circuito a 120 V | `1` |
| Derivato a 240 V (fase-fase, entrambe le semifasi) | Una delle due semifasi del circuito a 240 V | `240V (Split Phase)` |
| Alimentazione principale, semifase singola | Una delle due semifasi principali | `1` |
| Alimentazione principale, entrambe le semifasi separatamente | Un TA su ciascuna semifase, su canali separati | `1` su ciascuno |

#### Hardware

Per un derivato a 240 V (es. un'asciugatrice o un EV charger Livello 2):

1. Prendere **una** pinza amperometrica.
2. Applicarla attorno a **uno qualsiasi** dei due conduttori di linea del circuito a 240 V (L1 o L2). Non applicarla attorno al neutro, e mai attorno a entrambe le semifasi insieme.
3. Chiudere la pinza finché non scatta.
4. Inserire il jack da 3,5 mm in un canale libero del dispositivo.

#### Software

In **Channels** ([§4.5](02-setup.md#45-configure-each-channel)), impostare:

| Campo | Valore |
| --- | --- |
| Label | es. `Dryer` o `EV charger` |
| Phase | **`240V (Split Phase)`** |
| Active | ☑ |
| Grouping | Un gruppo per ogni elettrodomestico (il `Group N` predefinito va bene) |
| Role | `Load` (oppure `Battery` / `Inverter` se applicabile) |

> **ⓘ NOTA: Perché ×2 e non misurare direttamente la tensione?**  
> Il trasformatore di tensione all'interno di *EnergyMe Home* è cablato tra Linea e Neutro, che su un servizio bifase nordamericano corrisponde a ≈120 V. Un circuito fase-fase a 240 V ha esattamente il doppio di quella tensione RMS, quindi moltiplicare la corrente per 2× i 120 V misurati fornisce la potenza corretta. Questa soluzione è preferibile a un riferimento di tensione aggiuntivo e funziona per qualsiasi servizio bifase standard.

> **⚠ Non utilizzare `240V (Split Phase)` al di fuori dei sistemi bifase nordamericani.** È una scorciatoia numerica per la specifica topologia bifase 120/240 V e produrrebbe letture errate su un monofase europeo a 230 V o su qualsiasi sistema trifase.

---

## Appendice C: Riferimento di stato del LED {#appendix-c-led-status-reference}

Il LED sul fronte del dispositivo comunica lo stato del sistema a colpo d'occhio.

> **ⓘ Colori di avvio**  
> Durante i primi ~10 secondi dopo l'accensione, il LED attraversa brevemente giallo, arancione, viola e altri colori. **Questo è normale.** Ogni colore segna una fase interna di avvio. Attendere che il LED si stabilizzi prima di trarre conclusioni.

### Funzionamento normale {#normal-operation}

| LED | Stato | Significato |
| --- | --- | --- |
| 🟢 Verde, fisso | Connesso | Wi-Fi connesso, monitoraggio normale |
| 🔵 Blu, pulsazione lenta | In connessione | Credenziali Wi-Fi configurate ma attualmente non connesso (primo avvio, router ancora in avvio, o perdita temporanea di connessione). Si riconnette automaticamente; di solito si risolve entro 60 s |
| 🔵 Blu, lampeggio veloce | Portale attivo | Wi-Fi non configurato; portale captive aperto ([§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal)) |

### Avvisi {#alerts}

| LED | Stato | Significato | Cosa fare |
| --- | --- | --- | --- |
| 🟣 Viola, fisso | Modalità protetta | Protezione anti-crash attivata; il dispositivo sta ancora monitorando ed è pienamente raggiungibile tramite l'interfaccia web | Andare su **Logs** nel menu in alto per investigare; il dispositivo si riprenderà automaticamente |
| 🔴 Rosso, lampeggio veloce | Errore critico | Guasto persistente dopo i tentativi di recupero | Riavviare tramite pulsante ([Appendice D](#appendix-d-user-button-reference)); se si ripresenta, contattare l'assistenza |

### Feedback del pulsante (mentre il pulsante è premuto; vedere [Appendice D](#appendix-d-user-button-reference)) {#button-feedback-while-the-button-is-held-see-appendix-d-user-button-reference}

| Colore del LED mentre si tiene premuto | Azione che verrà attivata al rilascio |
| --- | --- |
| ⚪ Bianco | Pressione registrata ma sotto la soglia di azione. Rilasciare per nessuna azione |
| 🔵 Ciano | Riavvio |
| 🟡 Giallo | Reset della password ai valori predefiniti |
| 🟠 Arancione | Reset Wi-Fi (riapre il portale captive) |
| 🔴 Rosso | Reset di fabbrica: tutti i dati e le impostazioni verranno cancellati |
| ⚪ Bianco (di nuovo) | Tenuto premuto troppo a lungo. Rilasciare e riprovare |

---

## Appendice D: Riferimento del pulsante utente {#appendix-d-user-button-reference}

Il pulsante sul fronte del dispositivo Le consente di recuperare da situazioni comuni senza bisogno di un telefono o di un laptop.

**Come funziona:** premere e tenere premuto. Il LED cambia colore mentre si mantiene la pressione, mostrando quale azione verrà attivata al rilascio. **Rilasciare il pulsante non appena vede il colore corrispondente all'azione desiderata.**

| Tempo di pressione | Colore del LED | Azione al rilascio |
| --- | --- | --- |
| < 2 s | Bianco | Nessuna azione |
| 2-5 s | **Ciano** | **Riavvio:** equivalente a un ciclo di alimentazione |
| 5-10 s | **Giallo** | **Reset della password:** reimposta la password web su `energyme` |
| 10-15 s | **Arancione** | **Reset Wi-Fi:** cancella le credenziali Wi-Fi e riapre il portale captive ([§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal)). I dati energetici e la configurazione dei canali vengono preservati |
| 15-20 s | **Rosso** | **Reset di fabbrica** ⚠: cancella tutti i dati, la configurazione e le credenziali. Da usare solo come ultima risorsa |
| > 20 s | Bianco | Nessuna azione. Rilasciare e riprovare |

> **⚠ ATTENZIONE: Il reset di fabbrica è irreversibile**  
> Un reset di fabbrica cancella tutti i dati energetici accumulati, i nomi dei canali, la calibrazione, le credenziali Wi-Fi e la Sua password personalizzata. Non c'è modo di annullare. Mantenga la pressione fino al rosso solo se ne è certo.

> **✅ SUGGERIMENTO: Il colore come conferma**  
> Non è necessario contare i secondi. Osservi il LED: rilasci nel momento in cui mostra il colore desiderato. Se supera nuovamente fino al bianco, continui a tenere premuto; rilasci semplicemente senza attivare alcuna azione. Poi riprovi da capo.

---

**Precedente:** [← Risoluzione dei problemi](03-troubleshooting.md)
