<?php
/******************************************************************************
 * File Name:        index.php
 * Project:          multiple-EMs-autoChannel
 * Author:           Andrea Marotta
 * Created On:       [2025-11-06]
 * Last Modified:    [2025-11-06]
 *
 * Description:
 *   Simple PHP dashboard to aggregate and display power consumption from multiple 
 *   EnergyMe devices with auto-refresh every 2 seconds
 *
 * Edit meter_ajax.php:
 *   Replace <IP of 1st EnergyMe> and <IP of 2nd EnergyMe> with your device IPs 
 *   Update username/password if changed from defaults
 *   Open index.php in your browser
 *
 * Operating System:
 *   Cross-platform server web
 *
 * Requirements:
 *   PHP with cURL extension
 *   Access to EnergyMe devices on local network
 *
 * Notes:
 *   Using API from EnergyMe - Jibril Sharafi
 *   Tips: Simply activate as you need any numbers of EMs channel 
 *     to show values in EM1 and EM2 configuration panel
 *
 * Revision History:
 *   [2025-11-06] - 0.99 1st coding
 *   [2025-11-27] - 1.00 removed mandatory same numbers of channels in both EMs
 *
 ******************************************************************************/
?>

<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="UTF-8">
<title>Dashboard Dual EnergyMe</title>
<style>
body {
    font-family: Arial, sans-serif;
    text-align: center;
    padding: 30px;
}
h1 {
    font-size: 44px;
    margin-bottom: 20px;
}
h2 {
    font-size: 24px;
    margin-bottom: 0px;
    margin-top: 0px;
}
table {
    margin: 0 auto;
    border-collapse: collapse;
    width: 420px;             /* <-- ridotta */
    max-width: 90%;
    font-size: 16px;
}
th, td {
    border: 1px solid #bbb;
    padding: 6px 8px;         /* <-- meno spazio */
    text-align: center;
    white-space: nowrap;
}
th {
    background: #f2f2f2;
}
tfoot td {
    font-weight: normal;
    background: #f9f9f9;
}
</style>
</head>
<body>
<h2>Total Power</h2>
<h1 id="totale">⚡ -- W</h1>

<table id="tabella">
  <thead>
    <tr>
      <th>🏠 Home</th>
      <th>🏢 Office</th>
    </tr>
  </thead>
  <tbody id="corpo-tabella"></tbody>
  <tfoot id="piede-tabella"></tfoot>
</table>
<br>
<tr>
  <th><img width="30" src="icon.svg"/></th>
  <th>Powered by EnergyMe</th>
</tr>

<script>
// Function for update values
function aggiornaValori() {
    fetch('meter_ajax.php')
    .then(response => response.json())
    .then(data => {
        // aggiorna il totale generale
        const colore = (data.totale_generale > 5000) ? '#dc3545' : '#28a745';
        document.getElementById('totale').textContent = `⚡ ${data.totale_generale.toFixed(0)} W`;
        document.getElementById('totale').style.color = colore;

        const corpo = document.getElementById('corpo-tabella');
        const piede = document.getElementById('piede-tabella');
        corpo.innerHTML = '';
        piede.innerHTML = '';

        // prendi gli indici numerici e ordina
        const indices = Object.keys(data)
            .filter(k => !isNaN(k))
            .map(k => Number(k))
            .sort((a,b) => a - b);

        // crea una riga per ogni misura
        indices.forEach(idx => {
            const item = data[idx];
            const tr = document.createElement('tr');

            const td1 = document.createElement('td');
            td1.id = item.eml1;
            if (item.label1 != "") {
            	td1.textContent = `${item.label1}: ${item.em1.toFixed(0)} W`;
	            tr.appendChild(td1);
            } else {
            	td1.textContent = ``;
			}

            const td2 = document.createElement('td');
            td2.id = item.eml2;
            if (item.label2 != "") {
            	td2.textContent = `${item.label2}: ${item.em2.toFixed(0)} W`;
	            tr.appendChild(td2);
            } else {
            	td2.textContent = ``;
			}
			
            corpo.appendChild(tr);
        });

        // calcola "Altro" = indice 0 - somma(indici successivi)
        if (indices.length > 0) {
            const firstIdx = indices[0];
            const firstItem = data[firstIdx];

            const sommaCasaAltri = indices.slice(1).reduce((acc, idx) => acc + (data[idx]?.em1 || 0), 0);
            const sommaUfficioAltri = indices.slice(1).reduce((acc, idx) => acc + (data[idx]?.em2 || 0), 0);

            const altroCasa = Math.max(0, (firstItem.em1 - sommaCasaAltri)).toFixed(0);
            const altroUfficio = Math.max(0, (firstItem.em2 - sommaUfficioAltri)).toFixed(0);

            const trAltro = document.createElement('tr');
            const tdAltro1 = document.createElement('td');
            const tdAltro2 = document.createElement('td');

            tdAltro1.textContent = `Other: ${altroCasa} W`;
            tdAltro2.textContent = `Other: ${altroUfficio} W`;

            trAltro.appendChild(tdAltro1);
            trAltro.appendChild(tdAltro2);
            piede.appendChild(trAltro);
        }
    })
    .catch(err => console.error('Errore AJAX:', err));
}

// Update each 2 seconds
aggiornaValori();
setInterval(aggiornaValori, 2000);
</script>

</body>
</html>










<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="UTF-8">
<title>Dashboard Dual EnergyMe</title>
<style>
body {
    font-family: Arial, sans-serif;
    text-align: center;
    padding: 30px;
}
h1 {
    font-size: 44px;
    margin-bottom: 20px;
}
h2 {
    font-size: 24px;
    margin-bottom: 0px;
    margin-top: 0px;
}
table {
    margin: 0 auto;
    border-collapse: collapse;
    width: 420px;             /* <-- ridotta */
    max-width: 90%;
    font-size: 16px;
}
th, td {
    border: 1px solid #bbb;
    padding: 6px 8px;         /* <-- meno spazio */
    text-align: center;
    white-space: nowrap;
}
th {
    background: #f2f2f2;
}
tfoot td {
    font-weight: normal;
    background: #f9f9f9;
}
</style>
</head>
<body>
<h2>Potenza totale istantanea</h2>
<h1 id="totale">⚡ -- W</h1>

<table id="tabella">
  <thead>
    <tr>
      <th>🏠 Casa</th>
      <th>🏢 Ufficio</th>
    </tr>
  </thead>
  <tbody id="corpo-tabella"></tbody>
  <tfoot id="piede-tabella"></tfoot>
</table>
<br>
<tr>
  <th><img width="30" src="icon.svg"/></th>
  <th>Powered by EnergyMe</th>
</tr>

<script>
function aggiornaValori() {
    fetch('meter_ajax.php')
    .then(response => response.json())
    .then(data => {
        // aggiorna il totale generale
        const colore = (data.totale_generale > 5000) ? '#dc3545' : '#28a745';
        document.getElementById('totale').textContent = `⚡ ${data.totale_generale.toFixed(0)} W`;
        document.getElementById('totale').style.color = colore;

        const corpo = document.getElementById('corpo-tabella');
        const piede = document.getElementById('piede-tabella');
        corpo.innerHTML = '';
        piede.innerHTML = '';

        // prendi gli indici numerici e ordina
        const indices = Object.keys(data)
            .filter(k => !isNaN(k))
            .map(k => Number(k))
            .sort((a,b) => a - b);

        // crea una riga per ogni misura
        indices.forEach(idx => {
            const item = data[idx];
            const tr = document.createElement('tr');

            const td1 = document.createElement('td');
            td1.id = item.eml1;
            if (item.label1 != "") {
            	td1.textContent = `${item.label1}: ${item.em1.toFixed(0)} W`;
	            tr.appendChild(td1);
            } else {
            	td1.textContent = ``;
			}

            const td2 = document.createElement('td');
            td2.id = item.eml2;
            if (item.label2 != "") {
            	td2.textContent = `${item.label2}: ${item.em2.toFixed(0)} W`;
	            tr.appendChild(td2);
            } else {
            	td2.textContent = ``;
			}
			
            corpo.appendChild(tr);
        });

        // calcola "Altro" = indice 0 - somma(indici successivi)
        if (indices.length > 0) {
            const firstIdx = indices[0];
            const firstItem = data[firstIdx];

            const sommaCasaAltri = indices.slice(1).reduce((acc, idx) => acc + (data[idx]?.em1 || 0), 0);
            const sommaUfficioAltri = indices.slice(1).reduce((acc, idx) => acc + (data[idx]?.em2 || 0), 0);

            const altroCasa = Math.max(0, (firstItem.em1 - sommaCasaAltri)).toFixed(0);
            const altroUfficio = Math.max(0, (firstItem.em2 - sommaUfficioAltri)).toFixed(0);

            const trAltro = document.createElement('tr');
            const tdAltro1 = document.createElement('td');
            const tdAltro2 = document.createElement('td');

            tdAltro1.textContent = `Other: ${altroCasa} W`;
            tdAltro2.textContent = `Other: ${altroUfficio} W`;

            trAltro.appendChild(tdAltro1);
            trAltro.appendChild(tdAltro2);
            piede.appendChild(trAltro);
        }
    })
    .catch(err => console.error('Errore AJAX:', err));
}

// Aggiorna subito e poi ogni 2 secondi
aggiornaValori();
setInterval(aggiornaValori, 2000);
</script>

</body>
</html>
