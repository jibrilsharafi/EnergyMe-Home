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
            <button class="buttonForm" onclick="clearLogs()" id="clearLogs">Clear logs</button>
            <a class="buttonForm" href="/log" id="log" target="_blank" style="float: right;">Explore logs</a>
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

        <div class="section-box">
            <h2>Factory Reset</h2>
            <p><i>This will reset the device to its factory settings. All configurations will be lost. The WiFi 
            connection will be kept.</i></p>
            <button class="buttonForm" onclick="factoryReset()" id="factoryReset">Reset</button>
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

            function factoryReset() {
                var confirm = window.confirm("Are you sure you want to reset the device to factory settings? All configurations and data will be lost.");
                if (!confirm) {
                    return;
                }
                document.getElementById("factoryReset").innerHTML = "Waiting for response...";
                document.getElementById("factoryReset").disabled = true;

                var xhttp = new XMLHttpRequest();
                xhttp.open("POST", "/rest/factory-reset", true);
                xhttp.onreadystatechange = function () {
                    if (this.readyState == 4) {
                        if (this.status == 200) {
                            document.getElementById("factoryReset").innerHTML = "Reset successfull! You will be redirected to the home page in a few seconds...";
                            setTimeout(function () {
                                window.location.href = "/";
                            }, 3000);
                        } else {
                            alert("Error resetting to factory settings. Response: " + this.responseText);
                        }
                    }
                };
                xhttp.send();
            }

            function setLogLevel(level) {
                document.getElementById("setLogLevel").innerHTML = "Setting...";
                var logLevels = {
                    "Verbose": 0,
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
                xhttp.open("GET", "/rest/set-log-level?level=" + printLevelNumber + "&type=print", true);
                xhttp.onreadystatechange = function () {
                    if (this.readyState == 4) {
                        if (this.status != 200) {
                            alert("Error setting print log level. Response: " + this.responseText);
                        }
                    }
                };
                xhttp.send();

                var xhttp2 = new XMLHttpRequest();
                xhttp2.open("GET", "/rest/set-log-level?level=" + saveLevelNumber + "&type=save", true);
                xhttp2.onreadystatechange = function () {
                    if (this.readyState == 4 && this.status != 200) {
                        alert("Error setting save log level. Response: " + this.responseText);
                    }
                };
                xhttp2.send();

                document.getElementById("setLogLevel").innerHTML = "Set success";
                document.getElementById("setLogLevel").disabled = true;
                setTimeout(function () {
                    document.getElementById("setLogLevel").innerHTML = "Set logging level";
                    document.getElementById("setLogLevel").disabled = false;
                }, 3000);
            }

            function getLogLevel() {
                var xhttp = new XMLHttpRequest();
                xhttp.open("GET", "/rest/get-log-level", true);
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

            function clearLogs() {
                var confirm = window.confirm("Are you sure you want to clear the logs?");
                if (!confirm) {
                    return;
                }
                document.getElementById("clearLogs").innerHTML = "Clearing...";
                document.getElementById("clearLogs").disabled = true;

                var xhttp = new XMLHttpRequest();
                xhttp.open("POST", "/rest/clear-log", true);
                xhttp.onreadystatechange = function () {
                    if (this.readyState == 4) {
                        if (this.status == 200) {
                            document.getElementById("clearLogs").innerHTML = "Clear success";
                        } else {
                            alert("Error clearing logs. Response: " + this.responseText);
                        }
                        setTimeout(function () {
                            document.getElementById("clearLogs").innerHTML = "Clear logs";
                            document.getElementById("clearLogs").disabled = false;
                        }, 3000);
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
                xhttp.open("POST", "/rest/set-general-configuration", true);
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
            <select id="calibrationDropdown" onchange="fillCalibrationBox()"></select>
            <p id="dropdownError" style="color: red; display: none;"></p>
            <button id="addButton" class="buttonForm" onclick="addNewChannel()">Add</button>
            <form id="calibrationForm">
                <h3>Values</h3>
                <p><span class="list-key">Voltage:</span> <input id="vLsb" name="vLsb" type="number" required> <span class="list-unit">V/LSB</span></p>
                <p><span class="list-key">Current:</span> <input id="aLsb" name="aLsb" type="number" required> <span class="list-unit">A/LSB</span></p>
                <p><span class="list-key">Power:</span> <input id="wLsb" name="wLsb" type="number" required> <span class="list-unit">W/LSB</span></p>
                <p><span class="list-key">Reactive Power:</span> <input id="varLsb" name="varLsb" type="number" required> <span class="list-unit">VAr/LSB</span></p>
                <p><span class="list-key">Apparent Power:</span> <input id="vaLsb" name="vaLsb" type="number" required> <span class="list-unit">VA/LSB</span></p>
                <p><span class="list-key">Energy:</span> <input id="whLsb" name="whLsb" type="number" required> <span class="list-unit">Wh/LSB</span></p>
                <p><span class="list-key">Reactive Energy:</span> <input id="varhLsb" name="varhLsb" type="number" required> <span class="list-unit">VArh/LSB</span></p>
                <p><span class="list-key">Apparent Energy:</span> <input id="vahLsb" name="vahLsb" type="number" required> <span class="list-unit">VAh/LSB</span></p>
            </form>
            <div style="display: flex; justify-content: space-between;">
                <button class="buttonForm" type="submit" onclick="submitCalibrationData()" id="submitButton">Submit</button>
                <button class="buttonForm" onclick="repopulateFields()">Reload</button>
                <button class="buttonForm" type="default" onclick="resetDefaultCalibration()">Reset default
                    calibration</button>
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

            function repopulateFields() {
                Promise.all([fetchData('meter')])
                    .then(data => {
                        meterData = data[0];

                        showMeterDataInTable();
                        fillCalibrationBox();
                    })
                    .catch(error => console.error('Error:', error));
            }

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
                var selectedCalibration = calibrationData.find(calibration => calibration.label === document.getElementById('calibrationDropdown').value);
                if (selectedCalibration) {
                    document.getElementById('vLsb').value = selectedCalibration.calibrationValues.vLsb;
                    document.getElementById('aLsb').value = selectedCalibration.calibrationValues.aLsb;
                    document.getElementById('wLsb').value = selectedCalibration.calibrationValues.wLsb;
                    document.getElementById('varLsb').value = selectedCalibration.calibrationValues.varLsb;
                    document.getElementById('vaLsb').value = selectedCalibration.calibrationValues.vaLsb;
                    document.getElementById('whLsb').value = selectedCalibration.calibrationValues.whLsb;
                    document.getElementById('varhLsb').value = selectedCalibration.calibrationValues.varhLsb;
                    document.getElementById('vahLsb').value = selectedCalibration.calibrationValues.vahLsb;
                } else {
                    document.getElementById('dropdownError').innerText = "Error: Could not find calibration data";
                    document.getElementById('dropdownError').style.display = "block";
                }
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
                    xhttp.open("POST", "/rest/calibration-reset", true);
                    xhttp.send();
                }
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

            function submitCalibrationData() {            
                var calibrationData = [
                    {
                        "label": document.getElementById('calibrationDropdown').value,
                        "calibrationValues": {
                            "vLsb": parseFloat(document.getElementById('vLsb').value),
                            "aLsb": parseFloat(document.getElementById('aLsb').value),
                            "wLsb": parseFloat(document.getElementById('wLsb').value),
                            "varLsb": parseFloat(document.getElementById('varLsb').value),
                            "vaLsb": parseFloat(document.getElementById('vaLsb').value),
                            "whLsb": parseFloat(document.getElementById('whLsb').value),
                            "varhLsb": parseFloat(document.getElementById('varhLsb').value),
                            "vahLsb": parseFloat(document.getElementById('vahLsb').value)
                        }
                    }
                ];
            
                var xhttp = new XMLHttpRequest();
                xhttp.onreadystatechange = function() {
                    if (this.readyState == 4 && this.status == 200) {
                        console.log(JSON.parse(this.responseText));
                    } else if (this.readyState == 4) {
                        console.log("Error submitting calibration data");
                    }
                };
                xhttp.open("POST", '/rest/set-calibration', true);
                xhttp.setRequestHeader("Content-Type", "application/json");
                xhttp.send(JSON.stringify(calibrationData));

                document.getElementById("submitButton").innerHTML = "Set success";
                document.getElementById("submitButton").disabled = true;
                setTimeout(function () {
                    document.getElementById("submitButton").innerHTML = "Submit";
                    document.getElementById("submitButton").disabled = false;
                }, 3000);
            }

            function addNewChannel() {
                var newChannelName = prompt("Please enter the name of the new channel:");
                if (newChannelName) {
                    var selectedCalibration = calibrationData.find(calibration => calibration.label === document.getElementById('calibrationDropdown').value);
                    if (selectedCalibration) {
                        var newCalibrationData = [{
                            "label": newChannelName,
                            "calibrationValues": selectedCalibration.calibrationValues
                        }];

                        var xhttp = new XMLHttpRequest();
                        xhttp.onreadystatechange = function() {
                            if (this.readyState == 4 && this.status == 200) {
                                console.log(JSON.parse(this.responseText));
                                location.reload(); // reload the page
                            } else if (this.readyState == 4) {
                                console.log("Error submitting new channel data");
                            }
                        };
                        xhttp.open("POST", '/rest/set-calibration', true);
                        xhttp.setRequestHeader("Content-Type", "application/json");
                        xhttp.send(JSON.stringify(newCalibrationData));
                        
                    } else {
                        console.error("Error: Could not find calibration data for selected channel");
                    }
                }
            }

            document.getElementById("calibrationDropdown").addEventListener("change", function () {
                fillCalibrationBox();
            });

            document.getElementById('calibrationForm').addEventListener('submit', function (event) {
                event.preventDefault();
                submitCalibrationData();
            });

            Promise.all([fetchData('meter'), fetchData('get-calibration'), fetchData('get-channel')])
                .then(data => {
                    meterData = data[0];
                    calibrationData = data[1];
                    channelData = data[2];

                    populateDropdown();
                    showMeterDataInTable();
                })
                .catch(error => console.error('Error:', error));

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

        <title>Channel</title>
        <style>
            .channel-columns {
                display: grid;
                grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
                gap: 10px;
                justify-items: center;
            }

            .channel-columns input[type="text"] {
                width: 90%;
                /* Adjust this value as needed */
                box-sizing: border-box;
            }
        </style>
    </head>

    <body>
        <div class="buttonNavigation-container">
            <a class="buttonNavigation" type="outer" href="/configuration">Configuration</a>
        </div>

        <div id="channelContainer" class="section-box" style="text-align: center;">
            <h1>Channels</h1>
            <div style="height: 20px;"></div>
            <div class="channel-columns"></div>
        </div>
    </body>
    <script>
        var meterData;
        var channelData;
        var calibrationData;
        var intervals = []; // Store interval IDs

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

        Promise.all([fetchData('get-calibration'), fetchData('get-channel')])
            .then(data => {
                calibrationData = data[0];
                channelData = data[1];
                createChannelBoxes()
            })
            .catch(error => console.error('Error:', error));

        function createChannelBoxes() {
            var container = document.querySelector('.channel-columns');

            channelData.forEach((channel, index) => {
                var box = document.createElement('div');
                box.className = 'section-box';
                box.style.textAlign = 'center';
                var calibrationOptions = calibrationData.map(calibration =>
                    `<option value="${calibration.label}" ${calibration.label === channel.calibration.label ? 'selected' : ''}>${calibration.label}</option>`
                ).join('');
                box.innerHTML = `
                        <label>
                            Label:
                            <input type="text" value="${channel.label}" id="label${index}" onchange="updateChannel(${index}, this.value, document.getElementById('calibration${index}').value, document.getElementById('active${index}').checked, document.getElementById('reverse${index}').checked)">
                        </label>
                        <div style="height: 10px;"></div>
                        <label>
                            Calibration:
                            <select id="calibration${index}" onchange="updateChannel(${index}, document.getElementById('label${index}').value, this.value, document.getElementById('active${index}').checked, document.getElementById('reverse${index}').checked)">
                                ${calibrationOptions}
                            </select>
                        </label>

                        <div style="height: 10px;"></div>
                        <label>
                            <input type="checkbox" ${channel.reverse ? 'checked' : ''} id="reverse${index}" onchange="updateChannel(${index}, document.getElementById('label${index}').value, document.getElementById('calibration${index}').value, document.getElementById('active${index}').checked, this.checked)">
                            Reverse
                        </label>
                        <div style="height: 10px;"></div>
                        <label>
                            <input type="checkbox" ${channel.active ? 'checked' : ''} id="active${index}" onchange="updateChannel(${index}, document.getElementById('label${index}').value, document.getElementById('calibration${index}').value, this.checked, document.getElementById('reverse${index}').checked)">
                            Active
                        </label>
                        <div style="height: 10px;"></div>
                        <span id="power${index}"></span>
                    `;
                container.appendChild(box);

                if (channel.active) {
                    intervals[index] = setInterval(() => {
                        fetchData(`active-power?index=${index}`).then(dataActivePower => {
                            document.getElementById(`power${index}`).innerHTML = `${dataActivePower.toFixed(1)} W`;
                        });
                    }, 1000);
                }
            });
        }

        window.updateChannel = function (index, label, calibration, active, reverse) {
            if (!active && intervals[index]) {
                clearInterval(intervals[index]);
                document.getElementById(`power${index}`).innerHTML = '';
                intervals[index] = null; // Set the interval ID to null
            } else if (active && !intervals[index]) {
                intervals[index] = setInterval(() => {
                    fetchData(`active-power?index=${index}`).then(dataActivePower => {
                        document.getElementById(`power${index}`).innerHTML = `${dataActivePower.toFixed(1)} W`;
                    });
                }, 1000);
            }

            fetchData(`set-channel?index=${index}&label=${encodeURIComponent(label)}&calibration=${encodeURIComponent(calibration)}&active=${active}&reverse=${reverse}`)
                .then(data => {
                    console.log(data);
                })
                .catch(error => console.error('Error:', error));
        }
    </script>

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

    var dailyEnergy = {};
    var channelData = {};
    var meterData = {};

    function getDailyEnergy() {
        var xhttp = new XMLHttpRequest();

        xhttp.onreadystatechange = function () {
            if (this.readyState == 4 && this.status == 200) {
                dailyEnergy = JSON.parse(this.responseText);
            }
        };
        xhttp.open("GET", "/daily-energy", true);
        xhttp.send();
    }

    function getChannelData() {
        var xhttp = new XMLHttpRequest();

        xhttp.onreadystatechange = function () {
            if (this.readyState == 4 && this.status == 200) {
                channelData = JSON.parse(this.responseText);
            }
        };
        xhttp.open("GET", "/rest/get-channel", false);
        xhttp.send();
    }

    function getMeterData() {
        var xhttp = new XMLHttpRequest();

        xhttp.onreadystatechange = function () {
            if (this.readyState == 4 && this.status == 200) {
                meterData = JSON.parse(this.responseText);
            }
        };
        xhttp.open("GET", "/rest/meter", false);
        xhttp.send();
    }

    function updateMeterData() {
        document.getElementById("voltage").innerHTML = meterData[0].data.voltage.toFixed(1) + ' V';

        var powerDataHtml = '';
        var channelCount = 0;
        var totalPower = 0;
        for (var channel in meterData) {
            var label = meterData[channel].label;
            var activePower = meterData[channel].data.activePower.toFixed(0);

            powerDataHtml += '<div class="channel-box">';
            powerDataHtml += '<div class="channel-label">' + label + '</div>';
            powerDataHtml += '<div class="channel-value">' + activePower + ' W</div>';
            powerDataHtml += '</div>';

            if (channelCount > 0) {
                totalPower += parseFloat(activePower);
            }
            channelCount++;
        }

        document.getElementById("channels").innerHTML = powerDataHtml;
    }

    function parseDailyEnergy() {
        var dailyActiveEnergy = {};

        for (var date in dailyEnergy) {
            for (var channel in dailyEnergy[date]) {
                if (!dailyActiveEnergy[date]) {
                    dailyActiveEnergy[date] = {};
                }

                dailyActiveEnergy[date][channel] = dailyEnergy[date][channel].activeEnergy / 1000;
            }
        }

        var firstDay = Object.keys(dailyActiveEnergy)[0];
        var lastDay = Object.keys(dailyActiveEnergy)[Object.keys(dailyActiveEnergy).length - 1];

        var listMissingDays = [];
        var currentDate = new Date(firstDay);
        var lastDate = new Date(lastDay);

        while (currentDate < lastDate) {
            currentDate.setDate(currentDate.getDate() + 1);
            var date = currentDate.toISOString().split('T')[0];

            if (!dailyActiveEnergy[date]) {
                dailyActiveEnergy[date] = {};
            }
        }

        var dailyActiveEnergyDifference = {};

        var dates = Object.keys(dailyActiveEnergy).sort();
        for (var i = 0; i < dates.length; i++) {
            var date = dates[i];
            var previousDate = dates[i - 1];
        
            dailyActiveEnergyDifference[date] = {};
        
            for (var channel in dailyActiveEnergy[date]) {
                var currentDayEnergy = dailyActiveEnergy[date][channel];
                var previousDayEnergy = i > 0 ? dailyActiveEnergy[previousDate][channel] || 0 : 0;
        
                dailyActiveEnergyDifference[date][channel] = currentDayEnergy - previousDayEnergy;
            }
        }

        dailyActiveEnergy = addOtherDailyEnergy(dailyActiveEnergyDifference);
        dailyActiveEnergy = renameDailyActiveEnergyDifference(dailyActiveEnergy, channelData);

        return dailyActiveEnergy;
    }

    function addOtherDailyEnergy(dailyActiveEnergyDifference) {

        for (var date in dailyActiveEnergyDifference) {

            var generalEnergy = dailyActiveEnergyDifference[date]["0"];
            var otherEnergy = 0;

            for (var channel in dailyActiveEnergyDifference[date]) {
                if (channel !== "0") {
                    otherEnergy += dailyActiveEnergyDifference[date][channel];
                }
            }

            dailyActiveEnergyDifference[date]['Other'] = generalEnergy - otherEnergy;
            delete dailyActiveEnergyDifference[date]["0"];
        }

        return dailyActiveEnergyDifference;
    }
    
    function renameDailyActiveEnergyDifference(dailyActiveEnergyDifference, channelData) {

        for (var date in dailyActiveEnergyDifference) {

            for (var channel in dailyActiveEnergyDifference[date]) {
                if (channel !== "Other") {
                    var channelIndex = parseInt(channel);
                    var channelLabel = channelData[channelIndex].label;

                    dailyActiveEnergyDifference[date][channelLabel] = dailyActiveEnergyDifference[date][channel];
                    delete dailyActiveEnergyDifference[date][channel];
                }
            }

            otherEnergy = dailyActiveEnergyDifference[date]['Other'];
            delete dailyActiveEnergyDifference[date]['Other'];
            dailyActiveEnergyDifference[date]['Other'] = otherEnergy;
        }

        return dailyActiveEnergyDifference;
    }

    function getTotalActiveEnergy() {
        var totalActiveEnergy = {};

        for (var channel in meterData) {
            totalActiveEnergy[channel] = meterData[channel].data.activeEnergy / 1000;
        }

        totalActiveEnergy = addTotalOtherEnergy(totalActiveEnergy);
        totalActiveEnergy = renameTotalActiveEnergy(totalActiveEnergy, channelData);

        return totalActiveEnergy;
    }

    function addTotalOtherEnergy(totalActiveEnergy) {
        var totalGeneralEnergy = totalActiveEnergy["0"];
        var totalOtherEnergy = 0;

        for (var channel in totalActiveEnergy) {
            if (channel !== "0") {
                totalOtherEnergy += totalActiveEnergy[channel];
            }
        }

        totalActiveEnergy['Other'] = totalGeneralEnergy - totalOtherEnergy;
        delete totalActiveEnergy["0"];

        return totalActiveEnergy;
    }

    function renameTotalActiveEnergy(totalActiveEnergy, channelData) {

        for (var channel in totalActiveEnergy) {
            if (channel !== "Other") {
                var channelIndex = parseInt(channel);
                var channelLabel = channelData[channelIndex].label;

                totalActiveEnergy[channelLabel] = totalActiveEnergy[channel];
                delete totalActiveEnergy[channel];
            }

            otherEnergy = totalActiveEnergy['Other'];
            delete totalActiveEnergy['Other'];
            totalActiveEnergy['Other'] = otherEnergy;
        }

        return totalActiveEnergy;
    }

    function plotConsumptionChart(consumptionData) {
        var labels = Object.keys(consumptionData);
    
        var datasets = [];
        var channels = Object.keys(consumptionData[labels[0]]);
    
        channels.forEach(function (channel, i) {
            var data = labels.map(function (label) {
                var value = consumptionData[label][channel];
                if (isNaN(value)) {
                    value = 0;
                }
                if (value >= 1000) {
                    return value.toFixed(0);
                } else if (value >= 100) {
                    return value.toFixed(1);
                } else if (value >= 10) {
                    return value.toFixed(2);
                } else {
                    return value.toFixed(3);
                }
            });
    
            datasets.push({
                label: channel,
                data: data,
                backgroundColor: colors[i % colors.length],
                borderColor: colors[i % colors.length],
                borderWidth: 1
            });
        });
    
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
        var labels = Object.keys(consumptionData);
        var data = Object.values(consumptionData).map(value => isNaN(value) ? 0 : value); // Replace NaN with 0

        var totalConsumption = data.reduce(function (acc, value) {
            return acc + value;
        }, 0);
    
        labels = labels.map(function (label, i) {
            var percentage = ((data[i] / totalConsumption) * 100).toFixed(1);
            return label + ' (' + percentage + '%)';
        });
    
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
            xhttp.open("GET", `/${endpoint}`, true);
            xhttp.send();
        });
    }

    function plotCharts() {
        Promise.all([fetchData('daily-energy'), fetchData('rest/get-channel'), fetchData('rest/meter')])
            .then(data => {
                dailyEnergy = data[0];
                channelData = data[1];
                meterData = data[2];

                updateMeterData();

                if (Object.keys(dailyEnergy).length !== 0) {
                    dailyActiveEnergy = parseDailyEnergy();
                    plotConsumptionChart(dailyActiveEnergy);
                    
                    totalActiveEnergy = getTotalActiveEnergy();
                    plotPieChart(totalActiveEnergy);
                } else {
                    document.getElementById("consumption-chart").innerHTML = "No data available yet. Come back in a few hours!";
                    document.getElementById("consumption-chart").style.textAlign = "center";
                    document.getElementById("consumption-chart").style.fontSize = "20px";
                    document.getElementById("consumption-chart").style.color = "#808080"; // Gray color

                    document.getElementById("pie-chart").innerHTML = "No data available yet. Come back in a few hours!";
                    document.getElementById("pie-chart").style.textAlign = "center";
                    document.getElementById("pie-chart").style.fontSize = "20px";
                    document.getElementById("pie-chart").style.color = "#808080"; // Gray color
                }

            })
            .catch(error => console.error('Error:', error));
    }

    plotCharts();
    
    setInterval(function () {
        getMeterData();
        updateMeterData();
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
            <p><span class='list-key'>Free:</span><span id='heapFree' class='list-value'></span></p>
            <p><span class='list-key'>Total:</span><span id='heapTotal' class='list-value'></span></p>
            <h4>Spiffs</h4>
            <p><span class='list-key'>Free:</span><span id='spiffsFree' class='list-value'></span></p>
            <p><span class='list-key'>Total:</span><span id='spiffsTotal' class='list-value'></span></p>
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