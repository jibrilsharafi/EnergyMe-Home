<!-- translation
source: 02-setup.md
source-commit: dbf89c0
translated: 2026-05-21
-->

# Configurazione

Da ora in poi, il lavoro può essere svolto dall'utente finale. Il quadro elettrico può restare chiuso.

---

## 4. Configurazione software {#4-software-configuration}

### 4.1 Connessione al Wi-Fi del dispositivo (portale captive) {#41-connect-to-the-devices-wi-fi-captive-portal}

Alla prima accensione, o dopo un reset Wi-Fi, il dispositivo trasmette la propria rete Wi-Fi per consentirne la configurazione.

1. Sul telefono o laptop, aprire le impostazioni Wi-Fi.
2. Cercare una rete denominata **`EnergyMe-<DEVICE_ID>`**, dove `<DEVICE_ID>` è il codice di 12 caratteri stampato sull'etichetta all'interno della custodia del dispositivo.
3. Connettersi. La password è:
   - **Dispositivi EnergyMe** (acquistati da noi): stampata sugli adesivi della custodia, sia come codice QR scansionabile sia come testo in chiaro.
   - **Dispositivi community** (auto-costruiti): il `DEVICE_ID` stesso (lo stesso codice di 12 caratteri del nome della rete).
4. Si apre automaticamente una pagina di configurazione. Se non si apre, aprire un browser e navigare su **`http://192.168.4.1`**.

> **ⓘ NOTA: Se la rete non appare**  
> Attendere 60 secondi dopo l'accensione. Se non è ancora visibile, tenere premuto il pulsante per 10-15 secondi finché il LED non diventa arancione (reset Wi-Fi), poi rilasciare. Il dispositivo si riavvierà e riaprirà il portale. Vedere **[Appendice D](appendices.md#appendix-d-user-button-reference)** per il riferimento completo del pulsante.

### 4.2 Configurazione del Wi-Fi domestico {#42-configure-your-home-wi-fi}

Nella pagina del portale captive:

1. Andare su **Configure Wi-Fi**.
2. Selezionare la propria rete Wi-Fi domestica dall'elenco.
3. Inserire la password Wi-Fi.
4. Toccare **Save**.
5. Il dispositivo si riavvia e si connette alla rete domestica, operazione che richiede circa 30 secondi. Il LED smetterà di lampeggiare blu e si stabilizzerà su **verde fisso** una volta connesso.

> **✅ SUGGERIMENTO: Solo 2,4 GHz**  
> *EnergyMe Home* utilizza il **Wi-Fi a 2,4 GHz**. Se il router mostra reti 2,4 GHz e 5 GHz separate, scegliere quella a 2,4 GHz. Se il router usa un unico nome combinato ("band steering"), funzionerà solitamente senza problemi, ma in caso di problemi di connessione, chiedere alla pagina di amministrazione del router di esporre la banda 2,4 GHz come SSID separato.

### 4.3 Accesso all'interfaccia web {#43-access-the-web-interface}

1. Su un telefono o laptop connesso **alla stessa rete Wi-Fi domestica**, aprire un browser.
2. Navigare su **`http://energyme.local`**.
3. Effettuare il login con le credenziali predefinite:
   - **Username:** `admin`
   - **Password:** `energyme`

> **✅ SUGGERIMENTO: Demo dal vivo**  
> Un dispositivo completamente configurato è disponibile online su **[demo-energyme-home.energyme.net](https://demo-energyme-home.energyme.net/)**. Lo utilizzi per esplorare l'interfaccia, confrontarla con la Sua dashboard, o familiarizzare con il layout prima dell'installazione.

> **ⓘ NOTA: Annotare l'indirizzo IP**  
> Una volta effettuato il login, andare su **Info** nel menu in alto. Sotto **Network Status**, troverà l'indirizzo IP locale del dispositivo (es. `192.168.1.42`). **Lo annoti o lo salvi tra i preferiti.** Se in futuro `energyme.local` dovesse smettere di risolversi (alcuni router, PC e versioni Android più vecchie non gestiscono in modo affidabile i nomi `.local`), potrà sempre raggiungere l'interfaccia digitando direttamente quell'indirizzo IP nel browser.
>
> Se non ha ancora annotato l'IP e `energyme.local` non funziona più, apra la pagina di amministrazione del router e cerchi un dispositivo con hostname `energyme-home-<DEVICE_ID>`.

### 4.4 Modifica della password predefinita {#44-change-the-default-password}

> **⚠ Lo faccia prima di qualsiasi altra cosa.** Il dispositivo è fornito con una password predefinita nota. Finché non la cambia, chiunque sulla Sua rete domestica può accedere all'interfaccia.

1. Nel menu in alto, andare su **Configuration**.
2. Scorrere fino a **Change Password**.
3. Inserire la password attuale (`energyme`), quindi la nuova password due volte. La nuova password deve avere almeno 8 caratteri.
4. Cliccare **Save**.

Il dispositivo Le ricorderà con un pop-up sulla dashboard ogni volta che la apre, finché la password non sarà stata cambiata.

> **ⓘ NOTA: Password dimenticata?**  
> Tenere premuto il pulsante per 5-10 secondi finché il LED non diventa giallo (reset della password), poi rilasciare. La password viene reimpostata su `energyme`. Effettuare nuovamente il login e impostare subito una nuova password. Vedere **[Appendice D](appendices.md#appendix-d-user-button-reference)** per il riferimento completo del pulsante.

### 4.5 Configurazione di ogni canale {#45-configure-each-channel}

Ora si mappa ogni TA fisico a un nome e a un ruolo significativi nell'interfaccia. Andare su **Channels** nel menu in alto e configurare ogni canale a cui è collegato un TA.

Ogni canale ha i seguenti campi:

| Campo | Cosa significa |
| --- | --- |
| **Label** | Un nome a testo libero (es. "Grid", "Cucina", "EV charger"). È il nome che apparirà sulla dashboard |
| **Phase** | La fase su cui è applicato il TA. Usare `1` per impianti monofase. Per il trifase, vedere [Appendice B](appendices.md#appendix-b-three-phase-configuration) |
| **Reverse** | Inverte il segno di un canale. Normalmente non serve: EnergyMe rileva automaticamente i TA invertiti alla prima attivazione e li corregge in automatico. L'interruttore manuale è disponibile se necessario (es. quando cambia il ruolo di un canale). Inverte il segno istantaneamente, senza necessità di riaprire il quadro |
| **Active** | Abilita il canale. Deselezionare per i canali non utilizzati |
| **Grouping** | I canali che condividono la stessa etichetta di gruppo vengono combinati in un'unica card sulla dashboard. Lo usi per carichi multifase: imposti tutti e tre i TA di fase del Suo forno su "Forno" e la dashboard mostrerà la potenza totale dell'intero elettrodomestico |
| **Role** | Cosa rappresenta il canale (vedere tabella sotto) |

#### Valori di Role {#role-values}

| Role | Quando usarlo |
| --- | --- |
| `Load` | Un circuito derivato che consuma energia (cucina, luci, elettrodomestici, EV charger, pompa di calore...) |
| `Grid (+ import, - export)` | La linea di alimentazione principale. Positivo durante l'importazione dalla rete, negativo durante l'esportazione (es. vendita PV) |
| `PV / Solar (+ generation)` | Una stringa di pannelli solari misurata direttamente (lato AC). Positivo durante la generazione |
| `Battery (+ discharge, - charge)` | Un sistema di accumulo. Positivo durante la scarica verso la casa, negativo durante la carica |
| `Inverter (PV + Battery DC-coupled)` | Un inverter ibrido in cui PV e batteria condividono una singola uscita AC |

#### Configurazione per una tipica casa monofase {#configuration-for-a-typical-single-phase-home}

**Canale 0 (linea principale):**

| Campo | Valore |
| --- | --- |
| Label | `Grid` (o `Main`, o a Sua preferenza) |
| Phase | `1` |
| Reverse | (lasciare deselezionato, correggere in seguito se necessario) |
| Active | ☑ |
| Grouping | `Group 0` (predefinito) |
| Role | `Grid (+ import, - export)` |

**Canali da 1 a 15 (carichi derivati):**

| Campo | Valore |
| --- | --- |
| Label | L'etichetta dell'interruttore dall'[Appendice A](appendices.md#appendix-a-channel-map) (es. "Cucina") |
| Phase | `1` |
| Reverse | (lasciare deselezionato, correggere in seguito se necessario) |
| Active | ☑ per i canali con un TA, ☐ per i canali non utilizzati |
| Grouping | Un gruppo per canale per impostazione predefinita (es. `Group 1`, `Group 2`...). Modificare solo se si vogliono combinare canali sulla dashboard (vedere [Appendice B](appendices.md#appendix-b-three-phase-configuration) per i carichi trifase) |
| Role | `Load` (o `PV / Solar`, `Battery`, `Inverter` se applicabile) |

Cliccare **Save** per ogni canale.

> **⚠ Configurazione trifase**  
> Se ha carichi trifase (alimentazione principale o specifici circuiti derivati), i campi **Phase** e **Grouping** devono essere configurati con attenzione. Vedere **[Appendice B](appendices.md#appendix-b-three-phase-configuration)**.

### 4.6 Calibrazione del TA: solo se si usano TA non standard {#46-ct-calibration-only-if-using-non-standard-cts}

> **ⓘ Per i TA standard da 30 A (quelli inclusi nel kit)**  
> I dispositivi EnergyMe sono calibrati in fabbrica. Se sta usando i TA da 30 A forniti, **salti completamente questa sezione;** non è necessaria alcuna azione di calibrazione. Differenze minime rispetto a un contatore di riferimento (ben al di sotto dell'1%) sono attese e normali.

Deve visitare la pagina **Calibration** se:

- Ha ordinato TA di portata superiore (75 A, 150 A) per circuiti ad alta corrente; **oppure**
- Sta usando un dispositivo community con TA reperiti autonomamente.

#### Come calibrare {#how-to-calibrate}

1. Nel menu in alto, andare su **Calibration**.
2. Selezionare il canale da calibrare dal menu a discesa.
3. Impostare **CT Current Rating (A)** sul valore stampato sul corpo del TA (es. `75` per un TA da 75 A). Questo è obbligatorio ogni volta che il TA installato differisce dal valore predefinito di 30 A.
4. Impostare **CT Voltage Output (V RMS)** sulla tensione che il TA fornisce in uscita alla sua corrente nominale: `0.333` V per i TA standard del kit, oppure consultare il datasheet del proprio TA.

   > **⚠ Non superare i 0,5 V di uscita alla corrente massima prevista;** questo è il limite di ingresso sicuro dell'IC di misura ADE7953. Ad esempio, un TA 75 A / 1 V va bene se il circuito non supererà mai i ~37 A (metà della corrente nominale), ma non per un circuito a pieno carico da 75 A.

5. Lasciare **Scaling Factor (%)** a `0` a meno che non si stia rifinendo un errore residuo rispetto a un contatore di riferimento noto. L'intervallo di regolazione è in passi dello 0,1%; modifichi questo valore solo se ha un riferimento calibrato con cui confrontarsi.
6. Cliccare **Save**.

Ripetere per ogni canale che utilizza un TA non standard.

### 4.7 Verifica {#47-verification}

| Verifica | Quando | Atteso | Se errato |
| --- | --- | --- | --- |
| Tensione | Entro pochi secondi | La tensione nominale della Sua rete (~120 V o ~230 V a seconda della regione) | Ricontrollare la connessione L/N |
| Potenza Canale 0 (W) durante importazione dalla rete | Entro pochi secondi | **Positiva** (importazione di energia) | Se costantemente negativa quando si sa di stare importando, spuntare **Reverse** per il Canale 0 |
| Potenza Canale 0 (W) durante esportazione (PV) | Entro pochi secondi | **Negativa** | La convenzione dei segni è corretta: `+ import, - export` |
| Potenza canale derivato (W) su canali `Load` | Entro pochi secondi | Positiva durante il consumo | Se costantemente negativa, spuntare **Reverse** per quel canale |
| Somma dei rami `Load` minore o uguale all'importazione del Canale 0 | Entro pochi secondi | Sì | Se i rami superano la principale, è probabile che un TA sia sul lato di ingresso o sul pettine di distribuzione ([§3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-)) |
| Energia oraria / giornaliera / mensile (kWh) | Dopo qualche ora | Numeri che si popolano | Il dispositivo ha bisogno di tempo per accumulare i dati di energia; i totali si riempiono progressivamente nel corso delle ore |

> **ⓘ NOTA: Non si preoccupi se la dashboard appare vuota all'inizio**  
> La potenza istantanea (Watt) appare entro pochi secondi. **I valori aggregati di energia (kWh) richiedono alcune ore per popolarsi**, perché il dispositivo deve accumulare misure prima di mostrare le tendenze. Ricontrolli più tardi nella giornata.

> **ⓘ NOTA: La casella Reverse è Sua amica**  
> La maggior parte dei TA invertiti viene corretta automaticamente da EnergyMe alla prima attivazione. Ma se un canale dovesse leggere con il segno sbagliato (es. mostra -8 W quando ne aspetta +8 W), non c'è bisogno di riaprire il quadro. Basta spuntare **Reverse** per quel canale in [§4.5](#45-configure-each-channel); il segno si inverte istantaneamente.

> **✅ SUGGERIMENTO: Il "test del bollitore" (verifica istantanea)**  
> Accenda un singolo carico ad alta potenza su un circuito noto (es. un bollitore in cucina). Dovrebbe vedere la potenza istantanea aumentare **su quel singolo canale derivato** e sul Canale 0 di circa la stessa quantità.
>
> - Se appare su **più rami**, uno dei TA è su un cavo di ingresso condiviso. Torni al [§3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-).
> - Se appare sul **ramo sbagliato**, la mappatura canale-interruttore nell'interfaccia è errata. Modifichi le etichette in [§4.5](#45-configure-each-channel).
> - Se non appare su **nessun ramo monitorato** ma sul Canale 0, quel circuito non è ancora monitorato, il che è normale se non vi ha applicato un TA.

### 4.8 Aggiornamenti del firmware {#48-firmware-updates}

Il dispositivo controlla automaticamente la disponibilità di aggiornamenti del firmware. Quando è disponibile una nuova versione, appare un'**icona a forma di campanella (🔔)** accanto al pulsante Firmware Update nella barra di navigazione superiore.

Per aggiornare:

1. Andare su **Update** nel menu in alto.
2. Seguire le istruzioni a schermo.

> **ⓘ NOTA: Dispositivi community**  
> Le notifiche automatiche di aggiornamento richiedono che i servizi cloud siano abilitati. Per i dispositivi community (auto-costruiti), gli aggiornamenti sono sempre manuali: scaricare il binario del firmware più recente da [GitHub Releases](https://github.com/jibrilsharafi/EnergyMe-Home/releases) e caricarlo tramite la pagina Update.

### 4.9 Integrazioni {#49-integrations}

*EnergyMe Home* supporta diverse opzioni di integrazione: Custom MQTT, InfluxDB, REST API, Modbus TCP, e mDNS service discovery. Tutta la configurazione e la documentazione relative sono disponibili direttamente nell'interfaccia web sotto **Integrations** nel menu in alto; la pagina è autosufficiente e include tutto il necessario per la connessione a piattaforme di terze parti.

---

**Precedente:** [← Installazione](01-installation.md)  
**Successivo:** [Risoluzione dei problemi →](03-troubleshooting.md)
