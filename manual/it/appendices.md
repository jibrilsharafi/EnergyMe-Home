<!-- translation
source: appendices.md
source-commit: eda785a
translated: 2026-05-21
-->

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

## Appendice B: Configurazione trifase {#appendix-b-three-phase-configuration}

*EnergyMe Home* è fondamentalmente un dispositivo monofase, ma può monitorare un'**alimentazione principale trifase** (o carichi trifase specifici) utilizzando **un TA per fase** e combinando le tre letture sulla dashboard tramite il campo `Grouping`.

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
