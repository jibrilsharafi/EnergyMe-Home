#ifndef CSS_H
#define CSS_H

#include <Arduino.h>

const char button_css[] PROGMEM = R"rawliteral(
    .buttonNavigation-container {
        display: flex;
        flex-direction: row;
        justify-content: space-evenly;
        width: 50%;
        margin-top: 20px;
        margin-bottom: 50px;
    }

    .buttonNavigation {
        background: transparent;
        color: #2196F3;
        font-weight: bold;
        font-size: 1.2em;
        cursor: pointer;
        padding: 10px 20px;
        border-radius: 50px;
        text-decoration: none;
        box-shadow: 0 10px 20px rgba(0, 0, 0, 0.4);
        transition: all 0.1s ease;
    }

    .buttonNavigation:hover {
        box-shadow: 0 5px 15px rgba(0, 0, 0, 0.6);
    }

    .buttonNavigation:active {
        transform: scale(0.98);
        box-shadow: 0 2px 5px rgba(0, 0, 0, 0.8);
    }

    .buttonNavigation[type="home"] {
        color: #4CAF50;
    }

    .buttonNavigation[type="inner"] {
        color: #ff9800;
    }

    .buttonNavigation[type="outer"] {
        color: #0b7dda;
    }

    .buttonForm-container {
        display: flex;
        flex-direction: row;
        justify-content: space-evenly;
        width: 50%;
        margin-bottom: 100px;
    }

    .buttonForm {
        background: transparent;
        background-color: #f4f4f4;
        color: #4CAF50;
        font-weight: bold;
        padding: 10px 20px;
        border-radius: 50px;
        font-size: 1.0em;
        cursor: pointer;
        text-decoration: none;
        box-shadow: 0 10px 20px rgba(0, 0, 0, 0.2);
        transition: all 0.1s ease;
        border: 1px solid;
        margin-top: 10px;
    }

    .buttonForm:hover {
        box-shadow: 0 5px 15px rgba(0, 0, 0, 0.4);
    }

    .buttonForm:active {
        transform: scale(0.98);
        box-shadow: 0 2px 5px rgba(0, 0, 0, 0.6);
    }

    .buttonForm:disabled {
        color: #2196F3;
        box-shadow: 0 10px 20px rgba(0, 0, 0, 0.2);
        background-color: #bf7c0054;
        pointer-events: none;
    }
)rawliteral";

const char main_css[] PROGMEM = R"rawliteral(
    body {
        font-family: 'Trebuchet MS', sans-serif;
        background-color: #f4f4f4;
        margin: 0;
        padding: 0;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        min-height: 100vh;
    }
)rawliteral";

const char section_css[] PROGMEM = R"rawliteral(
    .section-box {
        width: 50%;
        margin: 20px;
        padding: 20px;
        border: 1px solid #ddd;
        border-radius: 10px;
        box-sizing: border-box;
        background-color: white;
        box-shadow: 0 0 10px rgba(0, 0, 0, 0.5);
    }
)rawliteral";

const char typography_css[] PROGMEM = R"rawliteral(
    .list-key {
        font-weight: bold;
        margin-right: 10px;
    }

    .list-value {
        font-style: italic;
    }

    h2 {
        color: #1e83d6;
    }

    h3 {
        color: #54a2e2;
    }

    h4 {
        color: #a4aff2;
    }
)rawliteral";

#endif