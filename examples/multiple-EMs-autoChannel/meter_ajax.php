<?php
/******************************************************************************
 * File Name:        meter_ajax.php
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

header('Content-Type: application/json');

// ConfigEMs and credentials
$em1 = 'http://<IP of 1st EnergyMe>/api/v1/ade7953/meter-values';
$em2 = 'http://<IP of 2nd EnergyMe>/api/v1/ade7953/meter-values';
$username = 'admin';
$password = 'energyme';

// Index to be used
$indici = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];

// Function to read data from EM
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

// Funtion to find index data
function trovaIndex($array, $indice) {
    foreach ($array as $item) {
        if (isset($item['index']) && $item['index'] == $indice) {
            return $item;
        }
    }
    return null;
}

// Get 2 EMs data
$dati1 = getMeterValues($em1, $username, $password);
$dati2 = getMeterValues($em2, $username, $password);

$risultato = [];
$totaleGenerale = 0;

// Cycle all requested indexes 
foreach ($indici as $i) {
    $blocco1 = trovaIndex($dati1, $i);
    $blocco2 = trovaIndex($dati2, $i);
    
    if (empty(trim($blocco1['label']))) {
		$label1 = "";
		$val1 = 0;
		$eml1 = "em1_";
    } else {
		$label1 = $blocco1['label'];
		$val1 = round($blocco1['data']['activePower'], 0);
		$eml1 = "em1_" . $i;
    }
    
    if (empty(trim($blocco2['label']))) {
		$label2 = "";
		$val2 = 0;
		$eml2 = "em2_";
    } else {
		$label2 = $blocco2['label'];
		$val2 = round($blocco2['data']['activePower'], 0);
		$eml2 = "em2_" . $i;
    }

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

// Create json
echo json_encode($risultato, JSON_PRETTY_PRINT);
?>
