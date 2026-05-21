# EnergyMe Home: Informazioni di sicurezza e conformità

Questo documento contiene le informazioni normative, di sicurezza, di garanzia e di smaltimento di *EnergyMe Home*. **Lo legga una volta prima di installare il dispositivo.** Conservi questo documento per tutta la vita utile del prodotto; se il prodotto viene trasferito a terzi, questo documento deve essere trasferito con esso.

Per l'installazione e la configurazione pratica passo-passo, vedere il [Manuale di installazione](README.md).

---

## 1. Fabbricante {#1-manufacturer}

| | |
| --- | --- |
| **Società** | EnergyMe S.r.l. |
| **Sede legale** | Via Plezzo 56, 20132 Milano (MI), Italy |
| **P. IVA / Codice Fiscale** | IT14543830963 |
| **Sito web** | <https://www.energyme.net> |
| **Assistenza** | <support@energyme.net> |

**Assistenza autorizzata.** Il servizio di assistenza, le riparazioni e la manutenzione straordinaria sono eseguiti esclusivamente dal fabbricante o da personale autorizzato per iscritto dal fabbricante. Qualsiasi intervento da parte di personale non autorizzato annulla la garanzia e può compromettere la sicurezza.

---

## 2. Identificazione del prodotto {#2-product-identification}

| | |
| --- | --- |
| **Nome del prodotto** | EnergyMe Home |
| **Codice prodotto** | EM-HOME-1.0-KIT-STANDARD |
| **Revisione hardware** | v6.1 |
| **Certificazione open-source** | OSHWA `IT000025` |

Il numero di serie, la revisione hardware e i dati di produzione sono stampati sull'etichetta all'interno della custodia del dispositivo. Li registri prima di installare il prodotto; sono necessari per le richieste di assistenza e per i reclami in garanzia.

---

## 3. Dichiarazione di Conformità {#3-declaration-of-conformity}

*EnergyMe Home* è fornito con una **Dichiarazione di Conformità (DoC)** separata, redatta dal fabbricante ai sensi delle direttive applicabili dell'Unione Europea. Prima di utilizzare il dispositivo, verificare che la DoC sia presente nella confezione o disponibile presso il fabbricante.

Il dispositivo è conforme ai requisiti essenziali della seguente legislazione UE:

- **2014/35/EU** - Direttiva Bassa Tensione (LVD)
- **2014/30/EU** - Compatibilità Elettromagnetica (EMC)
- **2014/53/EU** - Direttiva Apparecchiature Radio (RED), inclusi i requisiti di cybersicurezza dell'Articolo 3(3)(d)/(e)/(f) affrontati tramite **EN 18031-1** ed **EN 18031-2**
- **2011/65/EU** - Restrizione delle Sostanze Pericolose (RoHS), come modificata dalla Direttiva Delegata (UE) **2015/863**
- **2012/19/EU** - Rifiuti di Apparecchiature Elettriche ed Elettroniche (WEEE)
- **Regolamento (UE) 2023/988** - Regolamento Generale sulla Sicurezza dei Prodotti (GPSR)

L'elenco completo delle norme armonizzate applicate (incluse quelle relative alla sicurezza radio, EMC, esposizione RF e requisiti di cybersicurezza per le apparecchiature radio connesse a internet) è riportato nella Dichiarazione di Conformità.

> Se la Dichiarazione di Conformità è mancante, **non usare il dispositivo**. Contattare il fabbricante per ottenerne una copia.

---

## 4. Simboli e convenzioni {#4-symbols-and-conventions}

### 4.1 Simboli sul prodotto {#41-symbols-on-the-product}

I seguenti simboli possono apparire sull'etichetta del dispositivo, sulla confezione, o in questa documentazione. La loro presenza è obbligatoria e non devono essere rimossi, coperti o oscurati.

| Simbolo | Significato |
| --- | --- |
| **CE** | Conformità con le direttive UE applicabili (vedere §3) |
| **Bidone con ruote barrato** (WEEE) | Raccolta separata delle apparecchiature elettriche ed elettroniche richiesta a fine vita (vedere §11) |
| **Fulmine in un triangolo** | Rischio di scossa elettrica; consultare il manuale prima di qualsiasi intervento |
| **Libro aperto / "i" in cerchio** | Leggere il manuale d'uso prima di utilizzare il dispositivo |
| **Classe II (doppio quadrato)** | Apparecchiatura con isolamento rinforzato o doppio; non è richiesto il collegamento del conduttore di protezione |

### 4.2 Convenzioni utilizzate nella documentazione {#42-conventions-used-in-the-documentation}

I manuali destinati all'utente (installazione, configurazione, risoluzione dei problemi) utilizzano i seguenti callout inline:

| Callout | Scopo |
| --- | --- |
| `⚠ ATTENZIONE` | Procedure la cui non osservanza può causare danni al dispositivo, alle cose, o esporre le persone a pericolo |
| `⚠ NOTA` | Condizioni o vincoli importanti evidenziati al di fuori del testo corrente |
| `ⓘ NOTA` | Informazioni utili che completano una procedura |
| `✅ SUGGERIMENTO` | Raccomandazioni e buone pratiche |

Un marcatore rosso `🔴` o "PERICOLO", dove utilizzato, indica procedure la cui non osservanza può causare lesioni o gravi danni al dispositivo.

---

## 5. Uso previsto {#5-intended-use}

*EnergyMe Home* è un dispositivo IoT da montaggio su guida DIN per la misurazione e il monitoraggio del consumo e della produzione di energia elettrica in ambienti **residenziali**, **prosumer** e **piccoli commerciali**. Supporta fino a 16 canali di misura (uno diretto e quindici multiplexati) tramite trasformatori di corrente (TA) a nucleo apribile non invasivi.

Il dispositivo è destinato a essere installato da un **elettricista qualificato** all'interno di un quadro elettrico chiuso, in ambiente interno, su una guida DIN standard (EN 60715, DIN 43880), in conformità con le norme e i regolamenti elettrici locali del paese di installazione.

Il dispositivo monitora impianti elettrici monofase e trifase (vedere [Appendice B](appendices.md#appendix-b-three-phase-configuration)). Non agisce sull'impianto elettrico: è esclusivamente un dispositivo di misura e monitoraggio.

Il dispositivo comunica tramite **Wi-Fi 2,4 GHz** (IEEE 802.11 b/g/n) e supporta l'integrazione tramite REST API, MQTT, Modbus TCP e InfluxDB.

---

## 6. Uso non previsto {#6-non-intended-use}

Qualsiasi uso diverso da quello descritto in §5 è considerato improprio. Il fabbricante declina ogni responsabilità per danni a persone, animali o cose derivanti da uso improprio. L'uso improprio annulla inoltre la garanzia.

In particolare, è **rigorosamente vietato**:

- Uso in ambienti diversi da quadri elettrici chiusi interni (nessun uso esterno, nessuna atmosfera ATEX o potenzialmente esplosiva, nessun ambiente corrosivo o ad alta polverosità, nessuna esposizione a luce solare diretta, fiamme o fonti di calore).
- Uso con tensione o frequenza di rete al di fuori dell'intervallo nominale (vedere [datasheet](../../hardware/datasheet.md)).
- Uso con trasformatori di corrente, accessori o parti di ricambio non forniti o espressamente approvati dal fabbricante.
- Uso come contatore fiscale per transazioni commerciali di energia.
- Installazione o intervento da parte di personale non qualificato.
- Uso da parte di bambini (persone di età inferiore a 14 anni) o da parte di persone con ridotte capacità fisiche, sensoriali o cognitive, se non sotto la supervisione di una persona responsabile della loro sicurezza.
- Qualsiasi modifica, alterazione o manomissione del dispositivo, della sua custodia, della circuiteria interna, del firmware o delle etichette.
- Funzionamento del dispositivo con custodia danneggiata, conduttori esposti, o segni visibili di urto, deformazione o bruciatura.
- Immersione in acqua o altri liquidi; esposizione a gocciolamento o spruzzi d'acqua.
- Divulgazione delle credenziali di accesso al dispositivo a persone non autorizzate.
- Uso continuato del dispositivo dopo che è stata rilevata un'anomalia.

Il riutilizzo di qualsiasi componente dopo la dismissione del dispositivo è di esclusiva responsabilità dell'utente e solleva il fabbricante da qualsiasi responsabilità.

---

## 7. Rischi residui {#7-residual-risks}

Nonostante la progettazione e le misure di protezione adottate dal fabbricante, permangono i seguenti rischi residui:

- **Rischio di scossa elettrica** durante l'installazione, la manutenzione e la dismissione. Il dispositivo si collega direttamente alla rete elettrica AC. Scollegare sempre l'alimentazione tramite l'interruttore generale e verificare l'assenza di tensione con un tester prima di qualsiasi intervento.
- **Rischio di misura errata** se i trasformatori di corrente sono applicati in modo non corretto (su un cavo di ingresso condiviso, su un pettine di distribuzione, attorno a L e N insieme, o attorno al conduttore di protezione). Fare riferimento a [Installazione §3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-).
- **Rischio di danni al dispositivo** se la tensione di alimentazione eccede l'intervallo nominale, se la tensione di uscita del TA eccede 0,5 V alla corrente massima, o se il dispositivo è soggetto a urti meccanici.
- **Rischio per la sicurezza di rete** se la password predefinita non viene modificata dopo il primo login o se il dispositivo è esposto a reti non fidate.

L'utente è responsabile della lettura integrale di questo documento e del Manuale di installazione prima di installare o utilizzare il dispositivo.

---

## 8. Condizioni ambientali e di stoccaggio {#8-environmental-and-storage-conditions}

Il dispositivo deve essere installato e utilizzato **esclusivamente in ambienti interni**, in condizioni ambientali compatibili con le sue specifiche.

| Parametro | Funzionamento | Stoccaggio |
| --- | --- | --- |
| Temperatura | 0 °C a +50 °C | -10 °C a +60 °C |
| Umidità relativa | ≤ 80 %, non condensante | ≤ 80 %, non condensante |
| Grado IP | IP20 (solo quadro chiuso) | - |
| Altitudine | ≤ 2000 m | - |
| Grado di inquinamento | 2 | - |

L'area di stoccaggio deve essere protetta da luce solare diretta, fiamme, fonti di calore, vapore, vapori corrosivi, e accessibile solo a personale autorizzato.

---

## 9. Movimentazione e disimballaggio {#9-handling-and-unpacking}

Il dispositivo è fornito in un imballaggio di cartone e plastica progettato per preservarne l'integrità durante lo stoccaggio e il trasporto. Nonostante ciò, maneggiare il dispositivo con cura per evitare urti e cadute.

**Al disimballaggio, prima dell'installazione:**

1. Verificare che la confezione contenga tutti gli articoli elencati in [Installazione §1.1](01-installation.md#11-starter-kit-always-included).
2. Ispezionare visivamente il dispositivo, le pinze amperometriche e i cavi alla ricerca di danni (crepe, deformazioni, conduttori esposti, segni di urto).
3. Verificare che la Dichiarazione di Conformità (§3) e questo documento siano presenti.

**Se manca qualcosa, è danneggiato o non conforme, non installare il dispositivo.** Contattare `support@energyme.net` con una fotografia del contenuto della confezione e del problema. Non tentare di riparare o modificare il dispositivo.

Maneggiare sempre il dispositivo dalla sua custodia in ABS; non tirarlo mai per i cavi. Dopo il disimballaggio, smaltire l'imballaggio secondo le normative locali e tenerlo fuori dalla portata dei bambini.

---

## 10. Manutenzione {#10-maintenance}

Il dispositivo è progettato per una lunga vita operativa con manutenzione minima. **Non vi sono parti riparabili dall'utente all'interno.** Non aprire la custodia; l'apertura annulla la garanzia ed espone l'utente a rischio di scossa elettrica.

### 10.1 Manutenzione ordinaria (a cura dell'utente) {#101-routine-maintenance-by-the-user}

**Almeno una volta all'anno**, con il dispositivo spento e scollegato dalla rete elettrica:

1. Ispezionare visivamente il dispositivo, i cavi e le pinze amperometriche alla ricerca di segni di danno, deformazione o surriscaldamento.
2. Verificare che le etichette sul dispositivo siano leggibili e integre. Se un'etichetta è danneggiata o illeggibile, contattare il fabbricante per ottenerne una sostitutiva.
3. Rimuovere la polvere dalla custodia utilizzando un pennello morbido, un aspirapolvere con bocchetta morbida, o aria compressa a bassa pressione.

> **⚠ ATTENZIONE: Detergenti**
>
> - ❌ **Non** utilizzare solventi, alcol, ammoniaca o detergenti abrasivi o corrosivi.
> - ❌ **Non** utilizzare panni o spugne abrasive.
> - ❌ **Non** spruzzare liquidi direttamente sul dispositivo.
> - ✅ Utilizzare un panno morbido asciutto o appena umido solo sull'esterno della custodia, se necessario.

### 10.2 Manutenzione straordinaria (a cura del fabbricante) {#102-non-routine-maintenance-by-the-manufacturer}

La manutenzione straordinaria (riparazioni, sostituzione di componenti interni, ripristino del firmware dopo un guasto fisico) è eseguita esclusivamente dal fabbricante o da personale autorizzato. Contattare `support@energyme.net` per qualsiasi intervento oltre la pulizia ordinaria.

### 10.3 Aggiornamenti del firmware {#103-firmware-updates}

Gli aggiornamenti del firmware sono parte della normale vita del prodotto e vengono distribuiti over-the-air (OTA) tramite l'interfaccia web (vedere [Configurazione §4.8](02-setup.md#48-firmware-updates)). Applicare gli aggiornamenti disponibili entro un tempo ragionevole per mantenere il dispositivo sicuro e allineato con le funzionalità più recenti.

### 10.4 Parti di ricambio {#104-spare-parts}

Utilizzare solo parti di ricambio originali. L'uso di parti non originali annulla la garanzia e solleva il fabbricante da qualsiasi responsabilità per danni a persone o cose. Ordinare le parti di ricambio a `support@energyme.net`.

---

## 11. Smaltimento (WEEE) {#11-disposal-weee}

<img src="../assets/weee-symbol.svg" alt="WEEE symbol" width="60">

Il simbolo del **bidone con ruote barrato** stampato sull'etichetta del dispositivo indica che al termine della sua vita operativa, il dispositivo deve essere smaltito separatamente dai rifiuti domestici, in conformità con:

- **Direttiva 2012/19/EU** sui Rifiuti di Apparecchiature Elettriche ed Elettroniche (WEEE), come modificata;
- **D.Lgs. 14 marzo 2014, n. 49** (attuazione italiana della Direttiva 2012/19/EU).

**Non smaltire il dispositivo o i suoi accessori nei rifiuti domestici generici.** Conferirli a un punto di raccolta WEEE autorizzato nella propria zona, o restituirli al fabbricante o al rivenditore all'acquisto di un dispositivo nuovo equivalente, in conformità con le normative locali.

Lo smaltimento improprio è punibile ai sensi della legge nazionale applicabile e può causare danni ambientali e alla salute umana a causa della presenza di sostanze pericolose nelle apparecchiature elettriche ed elettroniche.

Anche i materiali di imballaggio (cartone, plastica) devono essere smaltiti secondo le normative locali di riciclo.

---

## 12. Dismissione {#12-decommissioning}

Al termine della vita operativa del dispositivo, o prima della rimozione permanente:

1. Scollegare il dispositivo dalla rete elettrica tramite l'interruttore generale.
2. Verificare l'assenza di tensione con un tester.
3. Rimuovere il dispositivo dalla guida DIN e scollegare i cavi L/N e le pinze amperometriche.
4. **Cancellare tutti i dati di configurazione e le credenziali** tramite reset di fabbrica prima dello smaltimento o del trasferimento ([Appendice D](appendices.md#appendix-d-user-button-reference)). Questo protegge le Sue credenziali Wi-Fi e qualsiasi configurazione personale.
5. Smaltire il dispositivo secondo §11.

Se il dispositivo viene trasferito a terzi (vendita, donazione, comodato), **tutta la documentazione che accompagna il dispositivo deve essere trasferita con esso**, inclusi questo documento, il Manuale di installazione e la Dichiarazione di Conformità.

---

## 13. Garanzia {#13-warranty}

Il prodotto è coperto dai termini di garanzia stabiliti nel contratto di acquisto e riassunti di seguito. La garanzia si applica solo quando il dispositivo è utilizzato nelle condizioni di uso previsto del §5 e in conformità con questa documentazione.

### 13.1 Durata {#131-duration}

- **12 mesi** per clienti business, in conformità con la legislazione UE e nazionale applicabile.
- **24 mesi** per consumatori finali, in conformità con la Direttiva 1999/44/CE e il D.Lgs. 206/2005 (Codice del Consumo).

Il periodo di garanzia decorre dalla data di consegna, comprovata dalla fattura o dalla ricevuta di acquisto.

### 13.2 Copertura {#132-coverage}

Il fabbricante si impegna, a propria discrezione, a riparare o sostituire i componenti riscontrati difettosi per materiali o lavorazione, a seguito di una propria ispezione. Le parti sostituite diventano proprietà del fabbricante.

### 13.3 Esclusioni {#133-exclusions}

La garanzia **non** copre:

- Danni causati da uso improprio, negligenza, incidente o mancata osservanza di questa documentazione.
- Danni causati da eventi esterni (fulmini, sovratensioni, alluvioni, incendi, disastri naturali).
- Danni causati dall'uso con accessori, parti di ricambio o trasformatori di corrente non originali.
- Danni alla custodia, alle etichette o ai componenti interni causati da manomissione, apertura o modifica.
- Usura coerente con l'uso normale.
- Costi di trasporto, spedizione e installazione, che sono a carico dell'acquirente.
- Difficoltà derivanti da normative di paesi diversi dal paese in cui il dispositivo è stato venduto.

### 13.4 Reclami {#134-claims}

Per richiedere la garanzia, contattare `support@energyme.net` fornendo:

1. Codice prodotto e numero di serie (stampati sull'etichetta all'interno della custodia del dispositivo).
2. Prova di acquisto (fattura o ricevuta).
3. Descrizione dettagliata del problema e, ove possibile, fotografie o video.

Qualsiasi prodotto restituito senza prova di acquisto o con etichette identificative alterate sarà riparato a carico dell'acquirente.

### 13.5 Limitazione di responsabilità {#135-limitation-of-liability}

Il fabbricante non è responsabile per incidenti o danni derivanti da uso improprio, modifica non autorizzata o mancata osservanza di questa documentazione. I termini completi di responsabilità sono stabiliti nel contratto di acquisto.

---

## 14. Conservazione documentale {#14-document-retention}

Il fabbricante conserva il fascicolo tecnico di questo prodotto (incluso questo documento, la DoC, la documentazione di progetto e i registri della valutazione di conformità) per **10 anni** dalla data in cui l'ultima unità è immessa sul mercato, in conformità con le direttive UE applicabili.

Durante questo periodo, il fascicolo tecnico è disponibile esclusivamente alle autorità di vigilanza del mercato su richiesta.

---
