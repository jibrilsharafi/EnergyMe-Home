<?php
header('Content-Type: application/json');

// Configurazione dispositivi e credenziali
$em1 = 'http://<IP of 1st EnergyMe>/api/v1/ade7953/meter-values?index=0';
$em2 = 'http://<IP of 2nd EnergyMe>/api/v1/ade7953/meter-values?index=0';
$username = 'admin';
$password = 'energyme';

function getMeterValues($url, $username, $password) {
    $ch = curl_init($url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
    curl_setopt($ch, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
    curl_setopt($ch, CURLOPT_USERPWD, "$username:$password");

    $response = curl_exec($ch);
    $err = curl_error($ch);
    curl_close($ch);

    if ($err) return ['activePower' => 0];
    $data = json_decode($response, true);
    if (json_last_error() !== JSON_ERROR_NONE) return ['activePower' => 0];
    return $data;
}

$data1 = getMeterValues($em1, $username, $password);
$data2 = getMeterValues($em2, $username, $password);

$potenza1 = floatval($data1['activePower']);
$potenza2 = floatval($data2['activePower']);
$totale = $potenza1 + $potenza2;

// Risposta JSON
echo json_encode([
    'em1' => $potenza1,
    'em2' => $potenza2,
    'total' => $totale
]);
