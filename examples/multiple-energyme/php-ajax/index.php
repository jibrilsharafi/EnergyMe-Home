<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="UTF-8">
<title>Dashboard Multiple EnergyMe</title>
<style>
body { font-family: Arial; text-align:center; padding:40px; }
h1 { font-size:48px; }
h3 { margin-top:10px; }
p { font-size:20px; }
</style>
</head>
<body>

<h1 id="totale">⚡ -- W</h1>
<h3>Total Power</h3>
<p id="em1">EM1: -- W</p>
<p id="em2">EM2: -- W</p>

<script>
// Function for update values
function aggiornaValori() {
    fetch('meter_ajax.php')
    .then(response => response.json())
    .then(data => {
        const colore = (data.totale > 3000) ? '#dc3545' : '#28a745';
        document.getElementById('totale').textContent = `⚡ ${data.totale.toFixed(2)} W`;
        document.getElementById('totale').style.color = colore;
        document.getElementById('em1').textContent = `EM1: ${data.em1.toFixed(2)} W`;
        document.getElementById('em2').textContent = `EM2: ${data.em2.toFixed(2)} W`;
    })
    .catch(err => console.error('Errore AJAX:', err));
}

// Update every 2 seconds
aggiornaValori();
setInterval(aggiornaValori, 2000);
</script>

</body>
</html>
