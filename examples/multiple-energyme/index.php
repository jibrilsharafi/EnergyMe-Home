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
    margin-bottom: 20px;
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

<script>
function aggiornaValori() {
    fetch('meter_ajax.php')
    .then(response => response.json())
    .then(data => {
        // update of total power
        const colore = (data.totale_generale > 3000) ? '#dc3545' : '#28a745';
        document.getElementById('totale').textContent = `⚡ ${data.totale_generale.toFixed(0)} W`;
        document.getElementById('totale').style.color = colore;

        const corpo = document.getElementById('corpo-tabella');
        const piede = document.getElementById('piede-tabella');
        corpo.innerHTML = '';
        piede.innerHTML = '';

        // get index and sort it
        const indices = Object.keys(data)
            .filter(k => !isNaN(k))
            .map(k => Number(k))
            .sort((a,b) => a - b);

        // create each table row per value
        indices.forEach(idx => {
            const item = data[idx];
            const tr = document.createElement('tr');

            const td1 = document.createElement('td');
            td1.id = item.eml1;
            td1.textContent = `${item.label1}: ${item.em1.toFixed(0)} W`;

            const td2 = document.createElement('td');
            td2.id = item.eml2;
            td2.textContent = `${item.label2}: ${item.em2.toFixed(0)} W`;

            tr.appendChild(td1);
            tr.appendChild(td2);
            corpo.appendChild(tr);
        });

        // calc Other = index 0 - sum(next index)
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
