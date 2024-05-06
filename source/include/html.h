#ifndef HTML_H
#define HTML_H

#include <Arduino.h>

const char configuration_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html>

    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">

        <link rel="stylesheet" type="text/css" href="/css/button.css">
        <link rel="stylesheet" type="text/css" href="/css/main.css">
        <link rel="stylesheet" type="text/css" href="/css/section.css">
        <link rel="stylesheet" type="text/css" href="/css/typography.css">

        <link rel="icon" type="image/png" href="/images/favicon.png">

        <title>Configuration</title>
        <style>
        </style>
    </head>

    <body>
        <div class="buttonNavigation-container">
            <a class="buttonNavigation" type="home" href="/">Home</a>
            <a class="buttonNavigation" type="inner" href="/calibration">Calibration</a>
            <a class="buttonNavigation" type="inner" href="/channel">Channel</a>
        </div>

        <div class="section-box">
            <h2>General configuration</h2>
            <form id="generalConfigurationForm">
                <div>
                    <h3>Cloud services</h3>
                    <p><i>Cloud services are used to send data to the cloud. This is useful for monitoring the device remotely.</i></p>
                    <input type="checkbox" id="cloudServices" class="config-input">
                    <label for="cloudServices">Enable cloud services</label>
                </div>
                <br>
                <button class="buttonForm" onclick="setGeneralConfiguration()" id="setGeneralConfigurationButton">Set configuration</button>
            </form>
        </div>

        <div class="section-box">
            <h2>Logging</h2>
            <p><i>Logging is used to record events and errors. It is useful for debugging and troubleshooting.</i></p>
            <div>
                <h3>Print</h3>
                <p><i>Print logging is used to display events and errors when the serial monitor is available. The serial
                        monitor is available when the device is connected to a computer via USB.</i></p>
                <select id="printLogLevel" class="config-input"></select>
            </div>
            <br>
            <div>
                <h3>Save</h3>
                <p><i>Save logging is used to record events and errors to the internal memory of the device. The internal
                        memory is limited, so it is recommended to only use this for troubleshooting.</i></p>
                <select id="saveLogLevel" class="config-input"></select>
            </div>
            <br>
            <button class="buttonForm" onclick="setLogLevel()" id="setLogLevel">Set logging level</button>
            <a class="buttonForm" href="/log" id="log" target="_blank" style="float: right;">View logs</a>
        </div>

        <div class="section-box">
            <h2>Restart Device</h2>
            <p><i>This will take a few seconds.</i></p>
            <button class="buttonForm" onclick="restart()" id="restart">Restart</button>
        </div>

        <div class="section-box">
            <h2>Disconnect from WiFi</h2>
            <p><i>This will erase the WiFi credentials.</i></p>
            <p><i>The device will create a WiFi network called "EnergyMe".
                    Connect to this network and go <a href="http://192.168.4.1" target="_blank">here</a> (192.168.4.1).
                    You can then configure the device to connect to your WiFi network.</i></p>
            <button class="buttonForm" onclick="disconnectWifi()" id="disconnect">Disconnect</button>
        </div>

        <script>
            var logLevels = ["Debug", "Info", "Warning", "Error", "Fatal"];

            function populateDropdown() {
                var printLogLevel = document.getElementById("printLogLevel");
                var saveLogLevel = document.getElementById("saveLogLevel");

                for (var i = 0; i < logLevels.length; i++) {
                    var opt = document.createElement("option");
                    opt.value = logLevels[i];
                    opt.innerHTML = logLevels[i];

                    printLogLevel.appendChild(opt.cloneNode(true));
                    saveLogLevel.appendChild(opt.cloneNode(true));
                }
            }

            function restart() {
                var restartButton = document.getElementById("restart");
                restartButton.innerHTML = "Restarting...";
                restartButton.disabled = true;

                var xhttp = new XMLHttpRequest();
                xhttp.open("POST", "/rest/restart", true);
                xhttp.onreadystatechange = function () {
                    if (this.readyState == 4) {
                        if (this.status == 200) {
                            setTimeout(function () {
                                window.location.href = "/";
                            }, 3000);
                        } else {
                            alert("Error restarting device. Response: " + this.responseText);
                            restartButton.innerHTML = "Restart";
                            restartButton.disabled = false;
                        }
                    }
                };
                xhttp.send();
            }

            function disconnectWifi() {
                var confirm = window.confirm("Are you sure you want to disconnect from WiFi? You will lose access to the device until you reconnect it to WiFi.");
                if (!confirm) {
                    return;
                }
                document.getElementById("disconnect").innerHTML = "Disconnecting...";
                document.getElementById("disconnect").disabled = true;

                var xhttp = new XMLHttpRequest();
                xhttp.open("POST", "/rest/disconnect", true);
                xhttp.onreadystatechange = function () {
                    if (this.readyState == 4) {
                        if (this.status == 200) {
                            alert("Successfully disconnected from WiFi. Please connect to the WiFi network called 'EnergyMe' and navigate to 192.168.4.1");
                        } else {
                            alert("Error disconnecting from WiFi. Response: " + this.responseText);
                        }
                    }
                };
                xhttp.send();
            }

            function setLogLevel(level) {
                document.getElementById("setLogLevel").innerHTML = "Setting...";
                var logLevels = {
                    "Debug": 1,
                    "Info": 2,
                    "Warning": 3,
                    "Error": 4,
                    "Fatal": 5
                };

                var printLogLevel = document.getElementById("printLogLevel").value;
                var saveLogLevel = document.getElementById("saveLogLevel").value;

                var printLevelNumber = logLevels[printLogLevel];
                var saveLevelNumber = logLevels[saveLogLevel];

                var xhttp = new XMLHttpRequest();
                xhttp.open("GET", "/rest/set-log?level=" + printLevelNumber + "&type=print", true);
                xhttp.onreadystatechange = function () {
                    if (this.readyState == 4) {
                        if (this.status != 200) {
                            alert("Error setting print log level. Response: " + this.responseText);
                        }
                    }
                };
                xhttp.send();

                var xhttp2 = new XMLHttpRequest();
                xhttp2.open("GET", "/rest/set-log?level=" + saveLevelNumber + "&type=save", true);
                xhttp2.onreadystatechange = function () {
                    if (this.readyState == 4 && this.status != 200) {
                        alert("Error setting save log level. Response: " + this.responseText);
                    }
                };
                xhttp2.send();

                document.getElementById("setLogLevel").innerHTML = "Set success";
                document.getElementById("setLogLevel").disabled = true;
                setTimeout(function () {
                    document.getElementById("setLogLevel").innerHTML = "Set logging Level";
                    document.getElementById("setLogLevel").disabled = false;
                }, 3000);
            }

            function getLogLevel() {
                var xhttp = new XMLHttpRequest();
                xhttp.open("GET", "/rest/get-log", true);
                xhttp.onreadystatechange = function () {
                    if (this.readyState == 4) {
                        if (this.status == 200) {
                            var logLevels = JSON.parse(this.responseText);
                            var printLogLevel = logLevels.print.toLowerCase().replace(/\b[a-z]/g, function (letter) {
                                return letter.toUpperCase();
                            });
                            var saveLogLevel = logLevels.save.toLowerCase().replace(/\b[a-z]/g, function (letter) {
                                return letter.toUpperCase();
                            });

                            populateDropdown();
                            document.getElementById("printLogLevel").value = printLogLevel;
                            document.getElementById("saveLogLevel").value = saveLogLevel;
                        } else {
                            alert("Error getting print log level. Response: " + this.responseText);
                        }
                    }
                };
                xhttp.send();
            }

            function getGeneralConfiguration() {
                var xhttp = new XMLHttpRequest();
                xhttp.open("GET", "/rest/get-general-configuration", true);
                xhttp.onreadystatechange = function () {
                    if (this.readyState == 4) {
                        if (this.status == 200) {
                            var generalConfig = JSON.parse(this.responseText);

                            document.getElementById("cloudServices").checked = generalConfig.isCloudServicesEnabled;
                        } else {
                            alert("Error getting general configuration. Response: " + this.responseText);
                        }
                    }
                };
                xhttp.send();
            }

            function setGeneralConfiguration() {
                document.getElementById("setGeneralConfigurationButton").innerHTML = "Setting...";
                document.getElementById("setGeneralConfigurationButton").disabled = true;

                var xhttp = new XMLHttpRequest();
                xhttp.open("POST", "/rest/configuration/general", true);
                xhttp.onreadystatechange = function () {
                    if (this.readyState == 4) {
                        if (this.status == 200) {
                            document.getElementById("setGeneralConfigurationButton").innerHTML = "Set success!";
                        } else {
                            alert("Error setting general configuration. Response: " + this.responseText);
                            document.getElementById("setGeneralConfigurationButton").innerHTML = "Set failed!";
                        }
                    }
                };
                xhttp.send(JSON.stringify({
                    isCloudServicesEnabled: document.getElementById("cloudServices").checked
                }));
                setTimeout(function () {
                    document.getElementById("setGeneralConfigurationButton").innerHTML = "Set configuration";
                    document.getElementById("setGeneralConfigurationButton").disabled = false;
                }, 3000);
            }

            getGeneralConfiguration();
            getLogLevel();
        </script>

    </body>

    </html>
)rawliteral";

const char calibration_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">

    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        
        <link rel="stylesheet" type="text/css" href="/css/button.css">
        <link rel="stylesheet" type="text/css" href="/css/main.css">
        <link rel="stylesheet" type="text/css" href="/css/section.css">
        <link rel="stylesheet" type="text/css" href="/css/typography.css">

        <link rel="icon" type="image/png" href="/images/favicon.png">

        <title>Calibration</title>
        <style>
            #meterTable {
                width: 100%;
                border-collapse: collapse;
                text-align: center;
            }

            #meterTable th,
            #meterTable td {
                padding: 8px;
                border: 1px solid #ddd;
            }

            #meterTable th {
                font-weight: bold;
            }

            #meterTable td:first-child {
                border-right: 1px solid #ddd;
            }
        </style>
    </head>

    <body>
        <div class="buttonNavigation-container">
            <a class="buttonNavigation" type="outer" href="/configuration">Configuration</a>
        </div>
        <div id="calibrationBox" class="section-box">
            <h1>Calibration</h1>
            <form id="calibrationForm">
                <h3>Calibration</h3>
                <p><span class="list-key">Active Power - Gain - Channel A:</span> <input id="aWGain" name="ActivePowerGainChannelA" type="number" required></p>
                <p><span class="list-key">Active Power - Offset - Channel A:</span> <input id="aWattOs" name="ActivePowerOffsetChannelA" type="number" required></p>
                <p><span class="list-key">Reactive Power - Gain - Channel A:</span> <input id="aVarGain" name="ReactivePowerGainChannelA" type="number" required></p>
                <p><span class="list-key">Reactive Power - Offset - Channel A:</span> <input id="aVarOs" name="ReactivePowerOffsetChannelA" type="number" required></p>
                <p><span class="list-key">Apparent Power - Gain - Channel A:</span> <input id="aVaGain" name="ApparentPowerGainChannelA" type="number" required></p>
                <p><span class="list-key">Apparent Power - Offset - Channel A:</span> <input id="aVaOs" name="ApparentPowerOffsetChannelA" type="number" required></p>
                <p><span class="list-key">Current - Gain - Channel B:</span> <input id="bIGain" name="CurrentGainChannelB" type="number" required></p>
                <p><span class="list-key">Phase Calibration - Channel A:</span> <input id="phCalA" name="PhaseCalibrationChannelA" type="number" required></p>
                <p><span class="list-key">Phase Calibration - Channel B:</span> <input id="phCalB" name="PhaseCalibrationChannelB" type="number" required></p>
            </form>
            <div style="display: flex; justify-content: space-between;">
                <button class="buttonForm" type="submit" onclick="submitCalibrationData()" id="submitButton">Submit</button>
                <button class="buttonForm" onclick="repopulateFields()">Reload</button>
                <button class="buttonForm" type="default" onclick="resetDefaultCalibration()">Reset default calibration</button>
            </div>
        </div>

        <div id="meterBox" class="section-box">
            <h1>Live meter data</h1>
            <table id="meterTable">
                <tr>
                    <th>Label</th>
                    <th>Voltage</th>
                    <th>Current</th>
                    <th>Active power</th>
                    <th>Apparent power</th>
                    <th>Reactive power</th>
                    <th>Power factor</th>
                </tr>
            </table>
        </div>

        <script>
            var meterData;
            var channelData;
            var calibrationData;

            function fetchData(endpoint) {
                return new Promise((resolve, reject) => {
                    var xhttp = new XMLHttpRequest();
                    xhttp.onreadystatechange = function () {
                        if (this.readyState == 4) {
                            if (this.status == 200) {
                                resolve(JSON.parse(this.responseText));
                            } else {
                                console.error(`Error fetching data from ${endpoint}. Status: ${this.status}`);
                                reject();
                            }
                        }
                    };
                    xhttp.open("GET", `/rest/${endpoint}`, true);
                    xhttp.send();
                });
            }

            Promise.all([fetchData('meter')])
                .then(data => {
                    meterData = data[0];
                    showMeterDataInTable();
                })
                .catch(error => console.error('Error:', error));

            Promise.all([fetchData('meter'), fetchData('calibration'), fetchData('get-channel')])
                .then(data => {
                    meterData = data[0];
                    calibrationData = data[1];
                    channelData = data[2];
                    populateDropdown();
                })
                .catch(error => console.error('Error:', error));

            function populateDropdown() {
                var dropdown = document.getElementById('calibrationDropdown');
                calibrationData.forEach((data, index) => {
                    var option = document.createElement('option');
                    option.text = data.label;
                    option.value = data.label;
                    dropdown.add(option, index);
                });
                dropdown.selectedIndex = 0;
                fillCalibrationBox();
            }

            function fillCalibrationBox() {
                var selectedLabel = document.getElementById('calibrationDropdown').value;
                var matchingChannels = channelData.filter(channel => channel.calibration.aWGain === selectedLabel && channel.active);
                var errorElement = document.getElementById('dropdownError');

                if (matchingChannels.length === 0) {
                    document.getElementById('currentVoltage').innerText = '';
                    document.getElementById('currentCurrent').innerText = '';
                    document.getElementById('currentActivePower').innerText = '';
                    errorElement.innerText = 'No matching channels found for selected calibration.';
                    errorElement.style.display = 'block';
                    submitButton.style.display = 'none';
                    return;
                }
                errorElement.style.display = 'none';
                var currentData = meterData.find(data => data.label === matchingChannels[0].label);
                if (currentData) {
                    document.getElementById('currentVoltage').innerText = currentData.data.voltage.toFixed(1);
                    document.getElementById('currentCurrent').innerText = currentData.data.current.toFixed(3);
                    document.getElementById('currentActivePower').innerText = currentData.data.activePower.toFixed(1);
                    document.getElementById('expectedVoltage').value = currentData.data.voltage.toFixed(1);
                    document.getElementById('expectedCurrent').value = currentData.data.current.toFixed(3);
                    document.getElementById('expectedActivePower').value = currentData.data.activePower.toFixed(1);
                    submitButton.style.display = 'block';
                } else {
                    console.error('No matching meter data found for label:', matchingChannels[0].label);
                    submitButton.style.display = 'none';
                }
            }

            function submitCalibrationData() {
                var selectedLabel = document.getElementById('calibrationDropdown').value;
                var matchingChannel = channelData.find(channel => channel.calibration.aWGain === selectedLabel);
                var currentMeterData = meterData.find(data => data.label === matchingChannel.label);
                var currentCalibrationData = calibrationData.find(data => data.label === selectedLabel);
                var expectedVoltage = document.getElementById('expectedVoltage').value;
                var expectedCurrent = document.getElementById('expectedCurrent').value;
                var expectedActivePower = document.getElementById('expectedActivePower').value;
                var noLoadChecked = document.getElementById('noLoadCheckbox').checked;

                var newVoltageSlope = currentCalibrationData.values.voltage.slope;
                var newCurrentSlope = currentCalibrationData.values.current.slope;
                var newActivePowerSlope = currentCalibrationData.values.activePower.slope;

                var newCurrentIntercept = currentCalibrationData.values.current.intercept;
                var newActivePowerIntercept = currentCalibrationData.values.activePower.intercept;

                newVoltageSlope = currentMeterData.data.voltage != 0
                        ? currentCalibrationData.values.voltage.slope * expectedVoltage / currentMeterData.data.voltage
                        : currentCalibrationData.values.voltage.slope;

                if (!noLoadChecked) {
                    newCurrentSlope = currentMeterData.data.current != 0
                        ? currentCalibrationData.values.current.slope * expectedCurrent / currentMeterData.data.current
                        : currentCalibrationData.values.current.slope;
                    newActivePowerSlope = currentMeterData.data.activePower != 0
                        ? currentCalibrationData.values.activePower.slope * expectedActivePower / currentMeterData.data.activePower
                        : currentCalibrationData.values.activePower.slope;
                } else {
                    newCurrentIntercept = newCurrentIntercept - currentMeterData.data.current;
                    newActivePowerIntercept = newActivePowerIntercept - currentMeterData.data.activePower;
                }

                var newCalibrationData = {
                    label: currentCalibrationData.label,
                    values: {
                        voltage: {
                            slope: newVoltageSlope
                        },
                        current: {
                            slope: newCurrentSlope,
                            intercept: newCurrentIntercept
                        },
                        activePower: {
                            slope: newActivePowerSlope,
                            intercept: newActivePowerIntercept
                        }
                    }
                };

                var existingIndex = calibrationData.findIndex(data => data.label === selectedLabel);
                if (existingIndex !== -1) {
                    calibrationData[existingIndex] = newCalibrationData;
                } else {
                    calibrationData.push(newCalibrationData);
                }

                var xhttp = new XMLHttpRequest();
                xhttp.onreadystatechange = function () {
                    if (this.readyState == 4 && this.status == 200) {
                        alert("Calibration data submitted successfully");
                        location.reload();
                    }
                };
                xhttp.open("POST", "/rest/calibration", true);
                xhttp.send(JSON.stringify(calibrationData));
            }

            function addAnotherCalibration(event) {
                event.preventDefault();
                var dropdown = document.getElementById('calibrationDropdown');
                var newLabel = prompt("Enter a label for the new calibration:");
                if (newLabel === null || newLabel === "") {
                    return;
                }
                var newData = {
                    label: newLabel,
                    values: {
                        voltage: {
                            slope: calibrationData[0].values.voltage.slope
                        },
                        current: {
                            slope: calibrationData[0].values.current.slope,
                            intercept: calibrationData[0].values.current.intercept
                        },
                        activePower: {
                            slope: calibrationData[0].values.activePower.slope,
                            intercept: calibrationData[0].values.activePower.intercept
                        }
                    }
                };
                calibrationData.push(newData);
                var newOption = document.createElement('option');
                newOption.text = newLabel;
                newOption.value = newLabel;
                dropdown.add(newOption);
                dropdown.selectedIndex = dropdown.options.length - 1;
                
                var xhttp = new XMLHttpRequest();
                xhttp.onreadystatechange = function () {
                    if (this.readyState == 4) {
                        if (this.status == 200) {
                            alert("Calibration data submitted successfully. You will be redirected to the channel page, where you can assign the new calibration to a channel. Then, you can come back to this page to calibrate the new configuration.");
                            window.location.href = "/channel";
                        } else {
                            console.error("Error submitting calibration data");
                        }
                    }
                };
                xhttp.open("POST", "/rest/calibration", true);
                xhttp.send(JSON.stringify(calibrationData));
            }

            function repopulateFields() {
                Promise.all([fetchData('meter')])
                    .then(data => {
                        meterData = data[0];
                        showMeterDataInTable();
                        fillCalibrationBox();
                    })
                    .catch(error => console.error('Error:', error));
            }

            function showMeterDataInTable() {
                var table = document.getElementById("meterTable");
                table.innerHTML = "<tr><th>Label</th><th>Voltage</th><th>Current</th><th>Active power</th><th>Apparent power</th><th>Reactive power</th><th>Power factor</th></tr>";

                meterData.forEach(function (meter) {
                    var row = table.insertRow();
                    var labelCell = row.insertCell(0);
                    var voltageCell = row.insertCell(1);
                    var currentCell = row.insertCell(2);
                    var activePowerCell = row.insertCell(3);
                    var apparentPowerCell = row.insertCell(4);
                    var reactivePowerCell = row.insertCell(5);
                    var powerFactorCell = row.insertCell(6);

                    labelCell.innerHTML = meter.label;
                    voltageCell.innerHTML = meter.data.voltage.toFixed(1) + " V";
                    currentCell.innerHTML = meter.data.current.toFixed(3) + " A";
                    activePowerCell.innerHTML = meter.data.activePower.toFixed(1) + " W";
                    apparentPowerCell.innerHTML = meter.data.apparentPower.toFixed(1) + " VA";
                    reactivePowerCell.innerHTML = meter.data.reactivePower.toFixed(1) + " VAr";
                    powerFactorCell.innerHTML = (meter.data.powerFactor * 100.0).toFixed(1) + " %";
                });
            }

            function resetDefaultCalibration() {
                var r = confirm("Are you sure you want to reset the calibration to the default values?");
                if (r == true) {
                    var xhttp = new XMLHttpRequest();
                    xhttp.onreadystatechange = function () {
                        if (this.readyState == 4) {
                            if (this.status == 200) {
                                alert("Default calibration reset successfully");
                                location.reload();
                            } else {
                                console.error("Error submitting default calibration data");
                            }
                        }
                    };
                    xhttp.open("POST", "/rest/calibration/reset", true);
                    xhttp.send();
                }
            }

            document.getElementById("calibrationDropdown").addEventListener("change", function () {
                fillCalibrationBox();
            });

            document.getElementById('calibrationForm').addEventListener('submit', function (event) {
                event.preventDefault();
                submitCalibrationData();
            });

        </script>
    </body>

    </html>
)rawliteral";

const char channel_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">

    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        
        <link rel="stylesheet" type="text/css" href="/css/button.css">
        <link rel="stylesheet" type="text/css" href="/css/main.css">
        <link rel="stylesheet" type="text/css" href="/css/section.css">
        <link rel="stylesheet" type="text/css" href="/css/typography.css">

        <link rel="icon" type="image/png" href="/images/favicon.png">
        
        <title>Update</title>
        <style>
        </style>
    </head>

    <body>
        <div class="buttonNavigation-container">
            <a class="buttonNavigation" type="home" href="/">Home</a>
        </div>
        <div id="update" class="section-box">
            <h2>Update</h2>
            <p>Upload a new firmware or filesystem to the device (only <i>.bin</i> files are allowed)</p>
            <form method='POST' action='/do-update' enctype='multipart/form-data' onsubmit="return formSubmitted()">
                <input type='file' id='file' name='update' accept='.bin' required>
                <input type='submit' id='submit' value='Update'>
            </form>
        </div>

        <div id="current-device" class="section-box">
            <h2>Current device</h2>
            <h3>Firmware</h3>
            <p>Version: <span id="current-firmware-version"></span></p>
            <p>Date: <span id="current-firmware-date"></span></p>
            <h3>Filesystem</h3>
            <p>Version: <span id="current-filesystem-version"></span></p>
            <p>Date: <span id="current-filesystem-date"></span></p>
        </div>

        <script>
            function formSubmitted() {
                var res = confirm("Are you sure you want to update the device?");
                if (!res) {
                    return false;
                }
                document.getElementById("submit").value = "Updating...";
                document.getElementById("submit").disabled = true;
                return true;
            }

            function getCurrentDeviceInfo() {
                var request = new XMLHttpRequest();
                request.open('GET', '/rest/device-info', true);
                request.onload = function () {
                    if (request.status >= 200 && request.status < 400) {
                        var data = JSON.parse(request.responseText);
                        document.getElementById("current-firmware-version").innerHTML = data.firmware.version;
                        document.getElementById("current-firmware-date").innerHTML = data.firmware.date;

                        document.getElementById("current-filesystem-version").innerHTML = data.filesystem.version;
                        document.getElementById("current-filesystem-date").innerHTML = data.filesystem.date;

                        var elements = document.querySelectorAll("#current-firmware-version, #current-firmware-date, #current-filesystem-version, #current-filesystem-date");
                        for (var i = 0; i < elements.length; i++) {
                            elements[i].style.fontStyle = "italic";
                        }
                    } else {
                        console.log("Error getting current firmware");
                    }
                };
                request.send();
            }

            getCurrentDeviceInfo();
        </script>
    </body>

    </html>
)rawliteral";

const char index_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html>

    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">

        <link rel="stylesheet" type="text/css" href="/css/button.css">
        <link rel="stylesheet" type="text/css" href="/css/main.css">
        <link rel="stylesheet" type="text/css" href="/css/section.css">
        <link rel="stylesheet" type="text/css" href="/css/typography.css">

        <link rel="icon" type="image/png" href="/images/favicon.png">
        <title>EnergyMe</title>
        <style>
            .header {
                font-size: 96px;
                margin-bottom: 10px;
                text-align: center;
            }

            .subheader {
                font-size: 32px;
                margin-bottom: 20px;
                text-align: center;
                color: #808080;
            }

            .voltage-box {
                text-align: center;
                font-size: 20px;
                margin-bottom: 20px;
                margin-top: 5px;
            }

            .channels-box {
                display: flex;
                flex-wrap: wrap;
                justify-content: space-around;
            }

            .channel-box {
                width: 18%;
                border: 1px solid #ddd;
                border-radius: 10px;
                box-sizing: border-box;
                background-color: white;
                box-shadow: 0 0 10px rgba(0, 0, 0, 0.5);
                margin-bottom: 20px;
                padding: 10px;
                text-align: center;
            }

            .channel-label {
                font-size: 24px;
                margin-bottom: 5px;
                font-weight: bold;
            }

            .channel-value {
                font-style: italic;
                color: #808080;
            }
        </style>
    </head>

    <body>

        <div class="header">EnergyMe</div>
        <div class="subheader">Home</div>

        <div class="buttonNavigation-container">
            <a class="buttonNavigation" type="inner" href="/info">Info</a>
            <a class="buttonNavigation" type="inner" href="/configuration">Configuration</a>
            <a class="buttonNavigation" type="inner" href="/update">Update</a>
        </div>

        <div class="section-box">
            <div id="voltage" class="voltage-box"></div>
            <div id="channels" class="channels-box"></div>
        </div>

        <div class="section-box">
            <div id="consumption-chart">
                <canvas id="consumption-chart-canvas"></canvas>
            </div>
        </div>

        <div class="section-box">
            <div id="pie-chart">
                <canvas id="pie-chart-canvas"></canvas>
            </div>
        </div>

        <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    </body>

    <script>
        var colors = [
            'rgba(0,123,255,0.5)', 'rgba(255,0,0,0.5)', 'rgba(0,255,0,0.5)', 'rgba(255,255,0,0.5)', 'rgba(0,255,255,0.5)',
            'rgba(123,0,255,0.5)', 'rgba(255,0,123,0.5)', 'rgba(0,255,123,0.5)', 'rgba(255,255,123,0.5)', 'rgba(123,255,0,0.5)',
            'rgba(0,123,0,0.5)', 'rgba(123,0,0,0.5)', 'rgba(0,0,123,0.5)', 'rgba(123,123,123,0.5)', 'rgba(255,255,255,0.5)',
            'rgba(0,0,255,0.5)', 'rgba(255,0,255,0.5)', 'rgba(255,123,0,0.5)'
        ];

        function getConsumptionData() {
            channelData = getChannelData();

            var xhttp = new XMLHttpRequest();
            xhttp.onreadystatechange = function () {
                if (this.readyState == 4 && this.status == 200) {
                    consumptionData = JSON.parse(this.responseText);

                    var otherData = [];
                    for (var i = 0; i < consumptionData[0].activeEnergy.daily.length; i++) {
                        var otherValue = consumptionData[0].activeEnergy.daily[i].value;
                        for (var channel in consumptionData) {
                            if (channel !== "0") {
                                if (consumptionData[channel].activeEnergy.daily.length > i) {
                                    otherValue -= consumptionData[channel].activeEnergy.daily[i].value;
                                }
                            }
                        }
                        otherData.push({
                            date: consumptionData[0].activeEnergy.daily[i].date,
                            value: otherValue
                        });
                    }
                    consumptionData["Other"] = {
                        activeEnergy: {
                            total: 0,
                            daily: otherData
                        },
                        reactiveEnergy: {
                            total: 0,
                            daily: []
                        },
                        apparentEnergy: {
                            total: 0,
                            daily: []
                        }
                    };

                    for (var channel in consumptionData) {
                        var dailyData = [];
                        for (var i = 0; i < consumptionData[channel].activeEnergy.daily.length; i++) {
                            var currentValue = consumptionData[channel].activeEnergy.daily[i].value;
                            var previousValue = i > 0 ? consumptionData[channel].activeEnergy.daily[i - 1].value : 0;
                            var dailyValue = currentValue - previousValue;
                            dailyData.push({
                                date: consumptionData[channel].activeEnergy.daily[i].date,
                                value: dailyValue // Display data with 2 decimals
                            });
                        }
                        consumptionData[channel] = dailyData;
                    }

                    delete consumptionData[0];
                    for (var channel in consumptionData) {
                        if (consumptionData[channel].length === 0) {
                            delete consumptionData[channel];
                        }
                    }

                    for (var channel in consumptionData) {
                        for (channelSingleData in channelData) {
                            if (channelData[channelSingleData].index === parseInt(channel)) {
                                consumptionData[channel].label = channelData[channelSingleData].label;
                            }
                        }
                    }
                    consumptionData["Other"].label = "Other";

                    plotConsumptionChart(consumptionData);
                    plotPieChart(consumptionData);
                }
            };
            xhttp.open("GET", "/rest/file/energy.json", true);
            xhttp.send();
        }

        function plotConsumptionChart(consumptionData) {
            var labels = consumptionData.Other.map(function (item) {
                return item.date;
            });

            var datasets = [];

            var i = 0;
            for (var channel in consumptionData) {
                var data = consumptionData[channel].map(function (item) {
                    return item.value.toFixed(2); // Display data with 2 decimals
                });

                datasets.push({
                    label: consumptionData[channel].label,
                    data: data,
                    backgroundColor: colors[i % colors.length],
                    borderColor: colors[i % colors.length],
                    borderWidth: 1
                });

                i++;
            }

            var ctx = document.getElementById('consumption-chart-canvas').getContext('2d');
            new Chart(ctx, {
                type: 'bar',
                data: {
                    labels: labels,
                    datasets: datasets
                },
                options: {
                    scales: {
                        x: {
                            stacked: true
                        },
                        y: {
                            stacked: true,
                            title: {
                                display: true,
                                text: 'kWh'
                            }
                        }
                    }
                }
            });
        }

        function plotPieChart(consumptionData) {
            var labels = [];
            var data = [];

            var totalConsumption = 0;
            for (var channel in consumptionData) {
                var total = consumptionData[channel].reduce(function (acc, item) {
                    return acc + item.value;
                }, 0);
                totalConsumption += total;
            }

            var i = 0;
            for (var channel in consumptionData) {
                var total = consumptionData[channel].reduce(function (acc, item) {
                    return acc + item.value;
                }, 0);

                var percentage = ((total / totalConsumption) * 100).toFixed(1);

                labels.push(consumptionData[channel].label + ' (' + percentage + '%)');
                data.push(total);
                i++;
            }

            var ctx = document.getElementById('pie-chart-canvas').getContext('2d');
            new Chart(ctx, {
                type: 'doughnut', // Use 'doughnut' type for a pie chart with a hole inside
                data: {
                    labels: labels,
                    datasets: [{
                        data: data,
                        backgroundColor: colors.slice(0, labels.length),
                        borderWidth: 1
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false
                }
            });
        }

        function updatePowerData() {
            var xhttp = new XMLHttpRequest();
            xhttp.onreadystatechange = function () {
                if (this.readyState == 4 && this.status == 200) {
                    powerData = JSON.parse(this.responseText);

                    document.getElementById("voltage").innerHTML = powerData[0].data.voltage.toFixed(1) + ' V';

                    var powerDataHtml = '';
                    var channelCount = 0;
                    var totalPower = 0;
                    for (var channel in powerData) {
                        var label = powerData[channel].label;
                        var activePower = powerData[channel].data.activePower.toFixed(0);

                        powerDataHtml += '<div class="channel-box">';
                        powerDataHtml += '<div class="channel-label">' + label + '</div>';
                        powerDataHtml += '<div class="channel-value">' + activePower + ' W</div>';
                        powerDataHtml += '</div>';

                        if (channelCount > 0) {
                            totalPower += parseFloat(activePower);
                        }
                        channelCount++;
                    }

                    if (channelCount > 1) {
                        var otherPower = (parseFloat(powerData[0].data.activePower) - totalPower).toFixed(0);
                        powerDataHtml += '<div class="channel-box">';
                        powerDataHtml += '<div class="channel-label">Other</div>';
                        powerDataHtml += '<div class="channel-value">' + otherPower + ' W</div>';
                        powerDataHtml += '</div>';
                    }

                    document.getElementById("channels").innerHTML = powerDataHtml;
                }
            };
            xhttp.open("GET", "/rest/meter", true);
            xhttp.send();
        }

        function getChannelData() {
            var xhttp = new XMLHttpRequest();
            xhttp.open("GET", "/rest/get-channel", false);
            xhttp.send();
            return JSON.parse(xhttp.responseText);
        }

        updatePowerData();

        getConsumptionData();

        setInterval(function () {
            updatePowerData();
        }, 1000);

    </script>

    </html>
)rawliteral";

const char info_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">

    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        
        <link rel="stylesheet" type="text/css" href="/css/button.css">
        <link rel="stylesheet" type="text/css" href="/css/main.css">
        <link rel="stylesheet" type="text/css" href="/css/section.css">
        <link rel="stylesheet" type="text/css" href="/css/typography.css">

        <link rel="icon" type="image/png" href="/images/favicon.png">
        
        <title>Device and WiFi Info</title>
        <style>
        </style>
    </head>

    <body>
        <div class="buttonNavigation-container">
            <a class="buttonNavigation" type="home" href="/">Home</a>
        </div>
        <div id="deviceInfo" class="section-box">
            <h2>Device Info</h2>
            <h3>System</h3>
            <p><span class='list-key'>Uptime:</span><span id='uptime' class='list-value'></span></p>
            <h3>Firmware</h3>
            <p><span class='list-key'>Version:</span><span id='firmwareVersion' class='list-value'></span></p>
            <p><span class='list-key'>Date:</span><span id='firmwareDate' class='list-value'></span></p>
            <h3>Filesystem</h3>
            <p><span class='list-key'>Version:</span><span id='filesystemVersion' class='list-value'></span></p>
            <p><span class='list-key'>Date:</span><span id='filesystemDate' class='list-value'></span></p>
            <h3>Memory</h3>
            <h4>Heap</h4>
            <p><span class='list-key'>Free:</span><span id='heapFree' class='list-value'></span> kB</p>
            <p><span class='list-key'>Total:</span><span id='heapTotal' class='list-value'></span> kB</p>
            <h4>Spiffs</h4>
            <p><span class='list-key'>Free:</span><span id='spiffsFree' class='list-value'></span> kB</p>
            <p><span class='list-key'>Total:</span><span id='spiffsTotal' class='list-value'></span> kB</p>
            <h3>Chip</h3>
            <p><span class='list-key'>Model:</span><span id='chipModel' class='list-value'></span></p>
            <p><span class='list-key'>Revision:</span><span id='chipRevision' class='list-value'></span></p>
            <p><span class='list-key'>CPU Frequency:</span><span id='chipCpuFrequency' class='list-value'></span> MHz</p>
            <p><span class='list-key'>SDK Version:</span><span id='chipSdkVersion' class='list-value'></span></p>
            <p><span class='list-key'>Flash Chip ID:</span><span id='chipFlashChipId' class='list-value'></span></p>
        </div>
        <div id="wifiInfo" class="section-box">
            <h2>WiFi Info</h2>
            <p><span class='list-key'>MAC Address:</span><span id='macAddress' class='list-value'></span></p>
            <p><span class='list-key'>Local IP:</span><span id='localIp' class='list-value'></span></p>
            <p><span class='list-key'>Subnet Mask:</span><span id='subnetMask' class='list-value'></span></p>
            <p><span class='list-key'>Gateway IP:</span><span id='gatewayIp' class='list-value'></span></p>
            <p><span class='list-key'>DNS IP:</span><span id='dnsIp' class='list-value'></span></p>
            <p><span class='list-key'>Status:</span><span id='status' class='list-value'></span></p>
            <p><span class='list-key'>SSID:</span><span id='ssid' class='list-value'></span></p>
            <p><span class='list-key'>BSSID:</span><span id='bssid' class='list-value'></span></p>
            <p><span class='list-key'>RSSI:</span><span id='rssi' class='list-value'></span> dB</p>
        </div>

        <script>
            function fetchData() {
                var deviceInfo, wifiInfo;

                function handleRequest() {
                    if (this.readyState == 4 && this.status == 200) {
                        var data = JSON.parse(this.responseText);
                        displayData(data, this.responseURL);
                    }
                }

                var deviceXhttp = new XMLHttpRequest();
                deviceXhttp.onreadystatechange = handleRequest;
                deviceXhttp.open("GET", "/rest/device-info", true);
                deviceXhttp.send();

                var wifiXhttp = new XMLHttpRequest();
                wifiXhttp.onreadystatechange = handleRequest;
                wifiXhttp.open("GET", "/rest/wifi-info", true);
                wifiXhttp.send();
            }

            function displayData(data, responseUrl) {
                if (responseUrl.includes("device-info")) {
                    displayDeviceInfo(data);
                } else if (responseUrl.includes("wifi-info")) {
                    displayWifiInfo(data);
                } else {
                    console.log("Unknown response URL: " + responseUrl);
                    return;
                }
            }

            function displayDeviceInfo(data) {
                var uptime = data.system.uptime;
                var uptimeStr = uptime + " ms";
                if (uptime > 1000) {
                    uptime /= 1000;
                    uptimeStr = uptime.toFixed(1) + " seconds";
                    if (uptime > 60) {
                        uptime /= 60;
                        uptimeStr = uptime.toFixed(1) + " minutes";
                        if (uptime > 60) {
                            uptime /= 60;
                            uptimeStr = uptime.toFixed(1) + " hours";
                            if (uptime > 24) {
                                uptime /= 24;
                                uptimeStr = uptime.toFixed(1) + " days";
                            }
                        }
                    }
                }
                document.getElementById('uptime').textContent = uptimeStr;

                document.getElementById('firmwareVersion').textContent = data.firmware.version;
                document.getElementById('firmwareDate').textContent = data.firmware.date;
                document.getElementById('filesystemVersion').textContent = data.filesystem.version;
                document.getElementById('filesystemDate').textContent = data.filesystem.date;
                
                var heapFree = data.memory.heap.free;
                var heapTotal = data.memory.heap.total;
                var heapFreeStr = (heapFree/1000.0).toFixed(1) + " kB (" + ((heapFree/heapTotal)*100).toFixed(1) + "%)";
                document.getElementById('heapFree').textContent = heapFreeStr;
                document.getElementById('heapTotal').textContent = (heapTotal/1000.0).toFixed(1) + " kB";

                var spiffsFree = data.memory.spiffs.free;
                var spiffsTotal = data.memory.spiffs.total;
                var spiffsFreeStr = (spiffsFree/1000.0).toFixed(1) + " kB (" + ((spiffsFree/spiffsTotal)*100).toFixed(1) + "%)";
                document.getElementById('spiffsFree').textContent = spiffsFreeStr;
                document.getElementById('spiffsTotal').textContent = (spiffsTotal/1000.0).toFixed(1) + " kB";

                document.getElementById('chipModel').textContent = data.chip.model;
                document.getElementById('chipRevision').textContent = data.chip.revision;
                document.getElementById('chipCpuFrequency').textContent = data.chip.cpuFrequency;
                document.getElementById('chipSdkVersion').textContent = data.chip.sdkVersion;
                document.getElementById('chipFlashChipId').textContent = data.chip.id;
            }

            function displayWifiInfo(data) {
                document.getElementById('macAddress').textContent = data.macAddress;
                document.getElementById('localIp').textContent = data.localIp;
                document.getElementById('subnetMask').textContent = data.subnetMask;
                document.getElementById('gatewayIp').textContent = data.gatewayIp;
                document.getElementById('dnsIp').textContent = data.dnsIp;
                document.getElementById('status').textContent = data.status;
                document.getElementById('ssid').textContent = data.ssid;
                document.getElementById('bssid').textContent = data.bssid;
                document.getElementById('rssi').textContent = data.rssi;
            }

            fetchData();
        </script>
    </body>

    </html>
)rawliteral";

const char update_failed_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">

    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        
        <link rel="stylesheet" type="text/css" href="/css/button.css">
        <link rel="stylesheet" type="text/css" href="/css/main.css">
        <link rel="stylesheet" type="text/css" href="/css/section.css">
        <link rel="stylesheet" type="text/css" href="/css/typography.css">

        <link rel="icon" type="image/png" href="/images/favicon.png">

        <title>Update failed</title>
        <style>
        </style>
    </head>

    <body>
        <div class="section-box">
            <h2 style="color: #F44336;">Upload failed!</h2>
            <p>Please try again.</p>
            <p style="font-style: italic; color: #aaa;">You will be redirected automatically to the homepage.</p>
            
            <script>
                function checkOnline() {
                    var xhr = new XMLHttpRequest();
                    xhr.open("GET", "/rest/is-alive", true);
                    xhr.onreadystatechange = function() {
                        if (xhr.readyState == 4) {
                            if (xhr.status == 200) {
                                window.location.href = "/";
                            }
                        }
                    }
                    xhr.send();
                }
                setInterval(checkOnline, 1000);
                
            </script>
    </body>

    </html>
)rawliteral";

const char update_successful_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">

    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        
        <link rel="stylesheet" type="text/css" href="/css/button.css">
        <link rel="stylesheet" type="text/css" href="/css/main.css">
        <link rel="stylesheet" type="text/css" href="/css/section.css">
        <link rel="stylesheet" type="text/css" href="/css/typography.css">

        <link rel="icon" type="image/png" href="/images/favicon.png">

        <title>Update successful</title>
        <style>
        </style>
    </head>

    <body>
        <div class="section-box">
            <h2>Upload successful!</h2>
            <p>Please wait while the device reboots...</p>
            <p style="font-style: italic; color: #aaa;">You will be redirected automatically to the homepage when the device is back online.</p>
            
            <script>
                function checkOnline() {
                    var xhr = new XMLHttpRequest();
                    xhr.open("GET", "/rest/is-alive", true);
                    xhr.onreadystatechange = function() {
                        if (xhr.readyState == 4) {
                            if (xhr.status == 200) {
                                window.location.href = "/";
                            }
                        }
                    }
                    xhr.send();
                }
                
                setTimeout(function() {
                    setInterval(checkOnline, 1000);
                }, 3000);
            </script>
    </body>

    </html>
)rawliteral";

const char update_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">

    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        
        <link rel="stylesheet" type="text/css" href="/css/button.css">
        <link rel="stylesheet" type="text/css" href="/css/main.css">
        <link rel="stylesheet" type="text/css" href="/css/section.css">
        <link rel="stylesheet" type="text/css" href="/css/typography.css">

        <link rel="icon" type="image/png" href="/images/favicon.png">
        
        <title>Update</title>
        <style>
        </style>
    </head>

    <body>
        <div class="buttonNavigation-container">
            <a class="buttonNavigation" type="home" href="/">Home</a>
        </div>
        <div id="update" class="section-box">
            <h2>Update</h2>
            <p>Upload a new firmware or filesystem to the device (only <i>.bin</i> files are allowed)</p>
            <form method='POST' action='/do-update' enctype='multipart/form-data' onsubmit="return formSubmitted()">
                <input type='file' id='file' name='update' accept='.bin' required>
                <input type='submit' id='submit' value='Update'>
            </form>
        </div>

        <div id="current-device" class="section-box">
            <h2>Current device</h2>
            <h3>Firmware</h3>
            <p>Version: <span id="current-firmware-version"></span></p>
            <p>Date: <span id="current-firmware-date"></span></p>
            <h3>Filesystem</h3>
            <p>Version: <span id="current-filesystem-version"></span></p>
            <p>Date: <span id="current-filesystem-date"></span></p>
        </div>

        <script>
            function formSubmitted() {
                var res = confirm("Are you sure you want to update the device?");
                if (!res) {
                    return false;
                }
                document.getElementById("submit").value = "Updating...";
                document.getElementById("submit").disabled = true;
                return true;
            }

            function getCurrentDeviceInfo() {
                var request = new XMLHttpRequest();
                request.open('GET', '/rest/device-info', true);
                request.onload = function () {
                    if (request.status >= 200 && request.status < 400) {
                        var data = JSON.parse(request.responseText);
                        document.getElementById("current-firmware-version").innerHTML = data.firmware.version;
                        document.getElementById("current-firmware-date").innerHTML = data.firmware.date;

                        document.getElementById("current-filesystem-version").innerHTML = data.filesystem.version;
                        document.getElementById("current-filesystem-date").innerHTML = data.filesystem.date;

                        var elements = document.querySelectorAll("#current-firmware-version, #current-firmware-date, #current-filesystem-version, #current-filesystem-date");
                        for (var i = 0; i < elements.length; i++) {
                            elements[i].style.fontStyle = "italic";
                        }
                    } else {
                        console.log("Error getting current firmware");
                    }
                };
                request.send();
            }

            getCurrentDeviceInfo();
        </script>
    </body>

    </html>
)rawliteral";

#endif