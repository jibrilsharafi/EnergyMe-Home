<?php
/*******************************************************************************
* Function Name  : meter_ajax.php
* Description    : Dashboard Dual EnergyMe
* Input          : None
* Output         : None
* Return         : None
* Author         : Andrea Marotta (marotta.andrea@gmail.com)
*******************************************************************************/

header('Content-Type: application/json');

// Configuration on the two EMs
$em1 = 'http://<IP of 1st EnergyMe>/api/v1/ade7953/meter-values';
$em2 = 'http://<IP of 2nd EnergyMe>/api/v1/ade7953/meter-values';
$username = 'admin';
$password = 'energyme';

// Index to check
$indici = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];

// Function to read data from EMs
function getMeterValues($url, $username, $password) {
    $ch = curl_init($url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    curl_setopt($ch, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_setopt($ch, CURLOPT_USERPWD, "$username:$password");

    $response = curl_exec($ch);
    $err = curl_error($ch);
    curl_close($ch);

    if ($err) return [];

    $data = json_decode($response, true);
    if (json_last_error() !== JSON_ERROR_NONE) return [];

    return $data;
}

// Function to check index 
function trovaIndex($array, $indice) {
    foreach ($array as $item) {
        if (isset($item['index']) && $item['index'] == $indice) {
            return $item;
        }
    }
    return null;
}

// Read two EMs
$dati1 = getMeterValues($em1, $username, $password);
$dati2 = getMeterValues($em2, $username, $password);

$risultato = [];
$totaleGenerale = 0;

// Cycle on each index
foreach ($indici as $i) {
    $blocco1 = trovaIndex($dati1, $i);
    $blocco2 = trovaIndex($dati2, $i);
    
    // Se uno dei due blocchi non esiste o ha label nulla/vuota → salta
    if (empty(trim($blocco1['label'])) || empty(trim($blocco2['label']))) {
        continue;
    }

   	$label1 = $blocco1['label'];
    $val1 = round($blocco1['data']['activePower'], 0);
    $eml1 = "em1_" . $i;

    $label2 = $blocco2['label'];
   	$val2 = round($blocco2['data']['activePower'], 0);
    $eml2 = "em2_" . $i;
    
    if ($i == 0) {
    	$totaleGenerale = $val1 + $val2;
	}
    $risultato[] = [
        'label1'  => $label1,
        'label2'  => $label2,
        'em1'    => $val1,
        'em2'    => $val2,
        'eml1'    => $eml1,
        'eml2'    => $eml2,
    ];
}

// Output JSON

$risultato['totale_generale'] = $totaleGenerale;

// Convert the mail array
echo json_encode($risultato, JSON_PRETTY_PRINT);
?>
