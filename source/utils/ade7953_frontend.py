#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Simple ADE7953 Register Tester UI
- Hosts a small Flask web UI with a searchable dropdown of registers
- Uses Digest auth (admin/energyme) against http://192.168.1.60/api/v1/ade7953/register
- Supports read (GET) and write (PUT)
- Allows overriding bits and signedness
- Accepts decimal or hex input for value (e.g., 12345 or 0x3039)

Run:
    pip install flask requests
    python ade7953_frontend.py
Browse:
    http://127.0.0.1:5009
"""

from flask import Flask, request, render_template_string, redirect, url_for, flash
import requests
from requests.auth import HTTPDigestAuth

# -------------------- Config --------------------
# You can modify these defaults here:
DEFAULT_DEVICE_IP = "192.168.1.60"
DEFAULT_USERNAME = "admin"
DEFAULT_PASSWORD = "energyme"
TIMEOUT = 5  # seconds
SECRET_KEY = "ade7953-ui"  # for flash messages

# Runtime config (will be set by user in UI)
current_device_ip = DEFAULT_DEVICE_IP
current_username = DEFAULT_USERNAME
current_password = DEFAULT_PASSWORD

def get_base_url():
    return f"http://{current_device_ip}/api/v1/ade7953/register"

def get_auth():
    return HTTPDigestAuth(current_username, current_password)

# Registers that require two sequential writes (per your note)
DOUBLE_WRITE_ADDRESSES = {0x103, 0x104}  # CF1DEN, CF2DEN

# -------------------- Register list --------------------
# name, address(int), bits(int), signed(bool), description(str)
REGISTERS = [
    # 8-bit
    {"name": "SAGCYC_8", "address": 0x000, "bits": 8, "signed": False, "desc": "SAGCYC (R/W) Sag lines Cycle"},
    {"name": "DISNOLOAD_8", "address": 0x001, "bits": 8, "signed": False, "desc": "DISNOLOAD (R/W) No-load detection disable"},
    {"name": "LCYCMODE_8", "address": 0x004, "bits": 8, "signed": False, "desc": "LCYCMODE (R/W) Line cycle accumulation mode"},
    {"name": "PGA_V_8", "address": 0x007, "bits": 8, "signed": False, "desc": "PGA_V (R/W) Voltage channel gain"},
    {"name": "PGA_IA_8", "address": 0x008, "bits": 8, "signed": False, "desc": "PGA_IA (R/W) Current A gain"},
    {"name": "PGA_IB_8", "address": 0x009, "bits": 8, "signed": False, "desc": "PGA_IB (R/W) Current B gain"},
    {"name": "WRITE_PROTECT_8", "address": 0x040, "bits": 8, "signed": False, "desc": "WRITE_PROTECT (R/W)"},
    {"name": "LAST_OP_8", "address": 0x0FD, "bits": 8, "signed": False, "desc": "LAST_OP (R/W) 0x35 read, 0xCA write"},
    {"name": "LAST_RWDATA_8", "address": 0x0FF, "bits": 8, "signed": False, "desc": "LAST_RWDATA (R/W) 8-bit"},
    {"name": "Version_8", "address": 0x702, "bits": 8, "signed": False, "desc": "Version (R/W) Silicon version"},
    {"name": "EX_REF_8", "address": 0x800, "bits": 8, "signed": False, "desc": "EX_REF (R/W) Reference input config"},
    # 16-bit
    {"name": "ZXTOUT_16", "address": 0x100, "bits": 16, "signed": False, "desc": "ZXTOUT (R/W) Zero-crossing timeout"},
    {"name": "LINECYC_16", "address": 0x101, "bits": 16, "signed": False, "desc": "LINECYC (R/W) Half line cycles"},
    {"name": "CONFIG_16", "address": 0x102, "bits": 16, "signed": False, "desc": "CONFIG (R/W) Configuration"},
    {"name": "CF1DEN_16", "address": 0x103, "bits": 16, "signed": False, "desc": "CF1DEN (R/W) CF1 divider denom (double write)"},
    {"name": "CF2DEN_16", "address": 0x104, "bits": 16, "signed": False, "desc": "CF2DEN (R/W) CF2 divider denom (double write)"},
    {"name": "CFMODE_16", "address": 0x107, "bits": 16, "signed": False, "desc": "CFMODE (R/W) CF output selection"},
    {"name": "PHCALA_16", "address": 0x108, "bits": 16, "signed": True,  "desc": "PHCALA (R/W) Phase calibration A (sign-magnitude)"},
    {"name": "PHCALB_16", "address": 0x109, "bits": 16, "signed": True,  "desc": "PHCALB (R/W) Phase calibration B (sign-magnitude)"},
    {"name": "PFA_16", "address": 0x10A, "bits": 16, "signed": True,  "desc": "PFA (R) Power factor A"},
    {"name": "PFB_16", "address": 0x10B, "bits": 16, "signed": True,  "desc": "PFB (R) Power factor B"},
    {"name": "ANGLE_A_16", "address": 0x10C, "bits": 16, "signed": True,  "desc": "ANGLE_A (R) Angle V vs IA"},
    {"name": "ANGLE_B_16", "address": 0x10D, "bits": 16, "signed": True,  "desc": "ANGLE_B (R) Angle V vs IB"},
    {"name": "PERIOD_16", "address": 0x10E, "bits": 16, "signed": False, "desc": "PERIOD (R)"},
    {"name": "ALT_OUTPUT_16", "address": 0x110, "bits": 16, "signed": False, "desc": "ALT_OUTPUT (R/W) Alt pin functions"},
    {"name": "LAST_ADD_16", "address": 0x1FE, "bits": 16, "signed": False, "desc": "LAST_ADD (R) Last successful address"},
    {"name": "LAST_RWDATA_16", "address": 0x1FF, "bits": 16, "signed": False, "desc": "LAST_RWDATA (R) 16-bit"},
    {"name": "Reserved_16", "address": 0x120, "bits": 16, "signed": False, "desc": "Reserved (R/W) set to 0x30 after unlock"},
    # 24/32-bit pairs & singles (subset includes all from your list)
    {"name": "SAGLVL_24", "address": 0x200, "bits": 24, "signed": False, "desc": "SAGLVL (R/W) Sag Voltage Level"},
    {"name": "SAGLVL_32", "address": 0x300, "bits": 32, "signed": False, "desc": "SAGLVL (R/W) Sag Voltage Level"},
    {"name": "ACCMODE_24", "address": 0x201, "bits": 24, "signed": False, "desc": "ACCMODE (R/W) Accumulation mode"},
    {"name": "ACCMODE_32", "address": 0x301, "bits": 32, "signed": False, "desc": "ACCMODE (R/W) Accumulation mode"},
    {"name": "AP_NOLOAD_24", "address": 0x203, "bits": 24, "signed": False, "desc": "Active power no-load level"},
    {"name": "AP_NOLOAD_32", "address": 0x303, "bits": 32, "signed": False, "desc": "Active power no-load level"},
    {"name": "VAR_NOLOAD_24", "address": 0x204, "bits": 24, "signed": False, "desc": "Reactive power no-load level"},
    {"name": "VAR_NOLOAD_32", "address": 0x304, "bits": 32, "signed": False, "desc": "Reactive power no-load level"},
    {"name": "VA_NOLOAD_24", "address": 0x205, "bits": 24, "signed": False, "desc": "Apparent power no-load level"},
    {"name": "VA_NOLOAD_32", "address": 0x305, "bits": 32, "signed": False, "desc": "Apparent power no-load level"},
    {"name": "AVA_24", "address": 0x210, "bits": 24, "signed": True,  "desc": "Instantaneous apparent power A"},
    {"name": "AVA_32", "address": 0x310, "bits": 32, "signed": True,  "desc": "Instantaneous apparent power A"},
    {"name": "BVA_24", "address": 0x211, "bits": 24, "signed": True,  "desc": "Instantaneous apparent power B"},
    {"name": "BVA_32", "address": 0x311, "bits": 32, "signed": True,  "desc": "Instantaneous apparent power B"},
    {"name": "AWATT_24", "address": 0x212, "bits": 24, "signed": True,  "desc": "Instantaneous active power A"},
    {"name": "AWATT_32", "address": 0x312, "bits": 32, "signed": True,  "desc": "Instantaneous active power A"},
    {"name": "BWATT_24", "address": 0x213, "bits": 24, "signed": True,  "desc": "Instantaneous active power B"},
    {"name": "BWATT_32", "address": 0x313, "bits": 32, "signed": True,  "desc": "Instantaneous active power B"},
    {"name": "AVAR_24", "address": 0x214, "bits": 24, "signed": True,  "desc": "Instantaneous reactive power A"},
    {"name": "AVAR_32", "address": 0x314, "bits": 32, "signed": True,  "desc": "Instantaneous reactive power A"},
    {"name": "BVAR_24", "address": 0x215, "bits": 24, "signed": True,  "desc": "Instantaneous reactive power B"},
    {"name": "BVAR_32", "address": 0x315, "bits": 32, "signed": True,  "desc": "Instantaneous reactive power B"},
    {"name": "IA_24", "address": 0x216, "bits": 24, "signed": True,  "desc": "Instantaneous current A"},
    {"name": "IA_32", "address": 0x316, "bits": 32, "signed": True,  "desc": "Instantaneous current A"},
    {"name": "IB_24", "address": 0x217, "bits": 24, "signed": True,  "desc": "Instantaneous current B"},
    {"name": "IB_32", "address": 0x317, "bits": 32, "signed": True,  "desc": "Instantaneous current B"},
    {"name": "V_24", "address": 0x218, "bits": 24, "signed": True,  "desc": "Instantaneous voltage"},
    {"name": "V_32", "address": 0x318, "bits": 32, "signed": True,  "desc": "Instantaneous voltage"},
    {"name": "IRMSA_24", "address": 0x21A, "bits": 24, "signed": False, "desc": "IRMS A"},
    {"name": "IRMSA_32", "address": 0x31A, "bits": 32, "signed": False, "desc": "IRMS A"},
    {"name": "IRMSB_24", "address": 0x21B, "bits": 24, "signed": False, "desc": "IRMS B"},
    {"name": "IRMSB_32", "address": 0x31B, "bits": 32, "signed": False, "desc": "IRMS B"},
    {"name": "VRMS_24", "address": 0x21C, "bits": 24, "signed": False, "desc": "VRMS"},
    {"name": "VRMS_32", "address": 0x31C, "bits": 32, "signed": False, "desc": "VRMS"},
    {"name": "AENERGYA_24", "address": 0x21E, "bits": 24, "signed": True,  "desc": "Active energy A"},
    {"name": "AENERGYA_32", "address": 0x31E, "bits": 32, "signed": True,  "desc": "Active energy A"},
    {"name": "AENERGYB_24", "address": 0x21F, "bits": 24, "signed": True,  "desc": "Active energy B"},
    {"name": "AENERGYB_32", "address": 0x31F, "bits": 32, "signed": True,  "desc": "Active energy B"},
    {"name": "RENERGYA_24", "address": 0x220, "bits": 24, "signed": True,  "desc": "Reactive energy A"},
    {"name": "RENERGYA_32", "address": 0x320, "bits": 32, "signed": True,  "desc": "Reactive energy A"},
    {"name": "RENERGYB_24", "address": 0x221, "bits": 24, "signed": True,  "desc": "Reactive energy B"},
    {"name": "RENERGYB_32", "address": 0x321, "bits": 32, "signed": True,  "desc": "Reactive energy B"},
    {"name": "APENERGYA_24", "address": 0x222, "bits": 24, "signed": True,  "desc": "Apparent energy A"},
    {"name": "APENERGYA_32", "address": 0x322, "bits": 32, "signed": True,  "desc": "Apparent energy A"},
    {"name": "APENERGYB_24", "address": 0x223, "bits": 24, "signed": True,  "desc": "Apparent energy B"},
    {"name": "APENERGYB_32", "address": 0x323, "bits": 32, "signed": True,  "desc": "Apparent energy B"},
    {"name": "OVLVL_24", "address": 0x224, "bits": 24, "signed": False, "desc": "Overvoltage level"},
    {"name": "OVLVL_32", "address": 0x324, "bits": 32, "signed": False, "desc": "Overvoltage level"},
    {"name": "OILVL_24", "address": 0x225, "bits": 24, "signed": False, "desc": "Overcurrent level"},
    {"name": "OILVL_32", "address": 0x325, "bits": 32, "signed": False, "desc": "Overcurrent level"},
    {"name": "VPEAK_24", "address": 0x226, "bits": 24, "signed": False, "desc": "Voltage peak"},
    {"name": "VPEAK_32", "address": 0x326, "bits": 32, "signed": False, "desc": "Voltage peak"},
    {"name": "RSTVPEAK_24", "address": 0x227, "bits": 24, "signed": False, "desc": "Read voltage peak with reset"},
    {"name": "RSTVPEAK_32", "address": 0x327, "bits": 32, "signed": False, "desc": "Read voltage peak with reset"},
    {"name": "IAPEAK_24", "address": 0x228, "bits": 24, "signed": False, "desc": "Current A peak"},
    {"name": "IAPEAK_32", "address": 0x328, "bits": 32, "signed": False, "desc": "Current A peak"},
    {"name": "RSTIAPEAK_24", "address": 0x229, "bits": 24, "signed": False, "desc": "Read current A peak with reset"},
    {"name": "RSTIAPEAK_32", "address": 0x329, "bits": 32, "signed": False, "desc": "Read current A peak with reset"},
    {"name": "IBPEAK_24", "address": 0x22A, "bits": 24, "signed": False, "desc": "Current B peak"},
    {"name": "IBPEAK_32", "address": 0x32A, "bits": 32, "signed": False, "desc": "Current B peak"},
    {"name": "RSTIBPEAK_24", "address": 0x22B, "bits": 24, "signed": False, "desc": "Read current B peak with reset"},
    {"name": "RSTIBPEAK_32", "address": 0x32B, "bits": 32, "signed": False, "desc": "Read current B peak with reset"},
    {"name": "IRQENA_24", "address": 0x22C, "bits": 24, "signed": False, "desc": "IRQ enable A"},
    {"name": "IRQENA_32", "address": 0x32C, "bits": 32, "signed": False, "desc": "IRQ enable A"},
    {"name": "IRQSTATA_24", "address": 0x22D, "bits": 24, "signed": False, "desc": "IRQ status A"},
    {"name": "IRQSTATA_32", "address": 0x32D, "bits": 32, "signed": False, "desc": "IRQ status A"},
    {"name": "RSTIRQSTATA_24", "address": 0x22E, "bits": 24, "signed": False, "desc": "Reset IRQ status A"},
    {"name": "RSTIRQSTATA_32", "address": 0x32E, "bits": 32, "signed": False, "desc": "Reset IRQ status A"},
    {"name": "IRQENB_24", "address": 0x22F, "bits": 24, "signed": False, "desc": "IRQ enable B"},
    {"name": "IRQENB_32", "address": 0x32F, "bits": 32, "signed": False, "desc": "IRQ enable B"},
    {"name": "IRQSTATB_24", "address": 0x230, "bits": 24, "signed": False, "desc": "IRQ status B"},
    {"name": "IRQSTATB_32", "address": 0x330, "bits": 32, "signed": False, "desc": "IRQ status B"},
    {"name": "RSTIRQSTATB_24", "address": 0x231, "bits": 24, "signed": False, "desc": "Reset IRQ status B"},
    {"name": "RSTIRQSTATB_32", "address": 0x331, "bits": 32, "signed": False, "desc": "Reset IRQ status B"},
    {"name": "CRC_24", "address": 0x000, "bits": 24, "signed": False, "desc": "Checksum (24-bit)"},
    {"name": "CRC_32", "address": 0x37F, "bits": 32, "signed": False, "desc": "Checksum (32-bit)"},
    {"name": "AIGAIN_24", "address": 0x280, "bits": 24, "signed": False, "desc": "Current gain A"},
    {"name": "AIGAIN_32", "address": 0x380, "bits": 32, "signed": False, "desc": "Current gain A"},
    {"name": "AVGAIN_24", "address": 0x281, "bits": 24, "signed": False, "desc": "Voltage gain"},
    {"name": "AVGAIN_32", "address": 0x381, "bits": 32, "signed": False, "desc": "Voltage gain"},
    {"name": "AWGAIN_24", "address": 0x282, "bits": 24, "signed": False, "desc": "Active power gain A"},
    {"name": "AWGAIN_32", "address": 0x382, "bits": 32, "signed": False, "desc": "Active power gain A"},
    {"name": "AVARGAIN_24", "address": 0x283, "bits": 24, "signed": False, "desc": "Reactive power gain A"},
    {"name": "AVARGAIN_32", "address": 0x383, "bits": 32, "signed": False, "desc": "Reactive power gain A"},
    {"name": "AVAGAIN_24", "address": 0x284, "bits": 24, "signed": False, "desc": "Apparent power gain A"},
    {"name": "AVAGAIN_32", "address": 0x384, "bits": 32, "signed": False, "desc": "Apparent power gain A"},
    {"name": "AIRMSOS_24", "address": 0x286, "bits": 24, "signed": True,  "desc": "IRMS offset A"},
    {"name": "AIRMSOS_32", "address": 0x386, "bits": 32, "signed": True,  "desc": "IRMS offset A"},
    {"name": "VRMSOS_24", "address": 0x288, "bits": 24, "signed": True,  "desc": "VRMS offset"},
    {"name": "VRMSOS_32", "address": 0x388, "bits": 32, "signed": True,  "desc": "VRMS offset"},
    {"name": "AWATTOS_24", "address": 0x289, "bits": 24, "signed": True,  "desc": "Active power offset A"},
    {"name": "AWATTOS_32", "address": 0x389, "bits": 32, "signed": True,  "desc": "Active power offset A"},
    {"name": "AVAROS_24", "address": 0x28A, "bits": 24, "signed": True,  "desc": "Reactive power offset A"},
    {"name": "AVAROS_32", "address": 0x38A, "bits": 32, "signed": True,  "desc": "Reactive power offset A"},
    {"name": "AVAOS_24", "address": 0x28B, "bits": 24, "signed": True,  "desc": "Apparent power offset A"},
    {"name": "AVAOS_32", "address": 0x38B, "bits": 32, "signed": True,  "desc": "Apparent power offset A"},
    {"name": "BIGAIN_24", "address": 0x28C, "bits": 24, "signed": False, "desc": "Current gain B"},
    {"name": "BIGAIN_32", "address": 0x38C, "bits": 32, "signed": False, "desc": "Current gain B"},
    {"name": "BVGAIN_24", "address": 0x28D, "bits": 24, "signed": False, "desc": "Reserved (do not modify)"},
    {"name": "BVGAIN_32", "address": 0x38D, "bits": 32, "signed": False, "desc": "Reserved (do not modify)"},
    {"name": "BWGAIN_24", "address": 0x28E, "bits": 24, "signed": False, "desc": "Active power gain B"},
    {"name": "BWGAIN_32", "address": 0x38E, "bits": 32, "signed": False, "desc": "Active power gain B"},
    {"name": "BVARGAIN_24", "address": 0x28F, "bits": 24, "signed": False, "desc": "Reactive power gain B"},
    {"name": "BVARGAIN_32", "address": 0x38F, "bits": 32, "signed": False, "desc": "Reactive power gain B"},
    {"name": "BVAGAIN_24", "address": 0x290, "bits": 24, "signed": False, "desc": "Apparent power gain B"},
    {"name": "BVAGAIN_32", "address": 0x390, "bits": 32, "signed": False, "desc": "Apparent power gain B"},
    {"name": "BIRMSOS_24", "address": 0x292, "bits": 24, "signed": False, "desc": "IRMS offset B"},
    {"name": "BIRMSOS_32", "address": 0x392, "bits": 32, "signed": False, "desc": "IRMS offset B"},
    {"name": "BWATTOS_24", "address": 0x295, "bits": 24, "signed": False, "desc": "Active power offset B"},
    {"name": "BWATTOS_32", "address": 0x395, "bits": 32, "signed": False, "desc": "Active power offset B"},
    {"name": "BVAROS_24", "address": 0x296, "bits": 24, "signed": False, "desc": "Reactive power offset B"},
    {"name": "BVAROS_32", "address": 0x396, "bits": 32, "signed": False, "desc": "Reactive power offset B"},
    {"name": "BVAOS_24", "address": 0x297, "bits": 24, "signed": False, "desc": "Apparent power offset B"},
    {"name": "BVAOS_32", "address": 0x397, "bits": 32, "signed": False, "desc": "Apparent power offset B"},
    {"name": "LAST_RWDATA_24", "address": 0x2FF, "bits": 24, "signed": False, "desc": "Last 24/32 read/write data (24)"},
    {"name": "LAST_RWDATA_32", "address": 0x3FF, "bits": 32, "signed": False, "desc": "Last 24/32 read/write data (32)"},
]

# quick index by name
REG_BY_NAME = {r["name"]: r for r in REGISTERS}

# -------------------- Flask app --------------------
app = Flask(__name__)
app.secret_key = SECRET_KEY

HTML = r"""
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>ADE7953 Register Tester</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    * { box-sizing: border-box; }
    body { 
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif; 
      margin: 0; padding: 20px; background: #f8fafc; color: #2d3748;
    }
    .container { max-width: 1400px; margin: 0 auto; }
    .header { background: white; border-radius: 12px; padding: 20px; margin-bottom: 20px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
    .config-row { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; align-items: end; }
    .main-row { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 20px; }
    .card { 
      background: white; border-radius: 12px; padding: 20px; 
      box-shadow: 0 1px 3px rgba(0,0,0,0.1); border: 1px solid #e2e8f0;
    }
    .card h3 { margin: 0 0 16px; color: #1a202c; font-size: 1.25rem; }
    .form-group { margin-bottom: 16px; }
    label { display: block; font-weight: 600; margin-bottom: 4px; color: #4a5568; font-size: 0.9rem; }
    input, select { 
      width: 100%; padding: 10px 12px; border: 2px solid #e2e8f0; border-radius: 8px; 
      font-size: 14px; transition: border-color 0.2s;
    }
    input:focus, select:focus { outline: none; border-color: #3182ce; box-shadow: 0 0 0 3px rgba(49,130,206,0.1); }
    button { 
      padding: 10px 16px; border: none; border-radius: 8px; cursor: pointer; font-weight: 600;
      transition: all 0.2s; font-size: 14px; min-width: 120px;
    }
    .btn-primary { background: #3182ce; color: white; }
    .btn-primary:hover { background: #2c5aa0; }
    .btn-secondary { background: #e2e8f0; color: #4a5568; }
    .btn-secondary:hover { background: #cbd5e0; }
    .btn-success { background: #38a169; color: white; }
    .btn-danger { background: #e53e3e; color: white; }
    .register-list { 
      height: 400px; overflow-y: auto; border: 2px solid #e2e8f0; border-radius: 8px; 
      background: #f7fafc;
    }
    .register-item { 
      padding: 12px 16px; border-bottom: 1px solid #e2e8f0; cursor: pointer; 
      transition: background-color 0.1s;
    }
    .register-item:hover { background: #edf2f7; }
    .register-item.selected { background: #3182ce; color: white; }
    .register-item.hidden { display: none; }
    .register-name { font-weight: 600; color: #2d3748; }
    .register-item.selected .register-name { color: white; }
    .register-details { font-size: 0.85rem; color: #718096; margin-top: 4px; }
    .register-item.selected .register-details { color: #e2e8f0; }
    .register-desc { font-size: 0.8rem; color: #a0aec0; margin-top: 2px; }
    .register-item.selected .register-desc { color: #cbd5e0; }
    .result-box { 
      background: #f7fafc; border: 2px solid #e2e8f0; border-radius: 8px; 
      padding: 16px; margin-top: 16px; font-family: 'SF Mono', 'Monaco', 'Consolas', monospace;
    }
    .result-success { border-color: #38a169; background: #f0fff4; }
    .result-error { border-color: #e53e3e; background: #fed7d7; }
    .timing { font-size: 0.8rem; color: #718096; margin-top: 8px; }
    .status { padding: 8px 12px; border-radius: 6px; font-size: 0.9rem; margin: 8px 0; }
    .status.success { background: #c6f6d5; color: #22543d; }
    .status.error { background: #fed7d7; color: #742a2a; }
    .status.info { background: #bee3f8; color: #2a4365; }
    .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    .value-display { 
      display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; 
      margin-top: 12px;
    }
    .value-item { 
      background: white; padding: 12px; border-radius: 6px; border: 1px solid #e2e8f0; 
      text-align: center;
    }
    .value-label { font-size: 0.8rem; color: #718096; margin-bottom: 4px; }
    .value-number { font-size: 1.1rem; font-weight: 600; color: #2d3748; font-family: monospace; }
    .loading { opacity: 0.6; pointer-events: none; }
    @media (max-width: 1024px) {
      .main-row { grid-template-columns: 1fr; }
      .config-row { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1 style="margin: 0 0 20px; color: #1a202c;">ADE7953 Register Tester</h1>
      
      <div class="config-row">
        <div class="form-group">
          <label for="device_ip">Device IP</label>
          <input id="device_ip" type="text" value="{{ current_ip }}" placeholder="192.168.1.60">
        </div>
        <div class="form-group">
          <label for="username">Username</label>
          <input id="username" type="text" value="{{ current_username }}" placeholder="admin">
        </div>
        <div class="form-group">
          <label for="password">Password</label>
          <input id="password" type="password" value="{{ current_password }}" placeholder="energyme">
        </div>
        <div class="form-group">
          <button id="update-config" class="btn-secondary">Update Connection</button>
        </div>
      </div>
      
      <div id="connection-status" class="status info">
        Device: <span id="current-device">{{ current_ip }}</span> | Auth: <span id="current-auth">{{ current_username }}/***</span>
      </div>
    </div>

    <div class="main-row">
      <!-- Register Selection -->
      <div class="card">
        <h3>📋 Register Selection</h3>
        <div class="form-group">
          <label for="search">🔍 Filter Registers</label>
          <input id="search" type="text" placeholder="Type to filter by name, address, or description...">
        </div>
        
        <div class="register-list" id="register-list">
          {% for r in registers %}
          <div class="register-item" 
               data-name="{{ r.name }}" 
               data-address="{{ r.address }}" 
               data-bits="{{ r.bits }}" 
               data-signed="{{ '1' if r.signed else '0' }}" 
               data-desc="{{ r.desc|e }}">
            <div class="register-name">{{ r.name }}</div>
            <div class="register-details">
              <span style="background: rgba(0,0,0,0.1); padding: 2px 6px; border-radius: 4px; margin-right: 8px;">
                0x{{ '%03X' % r.address }}
              </span>
              <span style="background: rgba(0,0,0,0.1); padding: 2px 6px; border-radius: 4px; margin-right: 8px;">
                {{ r.bits }}-bit
              </span>
              <span style="background: rgba(0,0,0,0.1); padding: 2px 6px; border-radius: 4px;">
                {{ 'signed' if r.signed else 'unsigned' }}
              </span>
            </div>
            <div class="register-desc">{{ r.desc }}</div>
          </div>
          {% endfor %}
        </div>

        <div class="grid-2" style="margin-top: 16px;">
          <div class="form-group">
            <label for="manual-address">Manual Address</label>
            <input id="manual-address" type="text" placeholder="0x21C or 540">
          </div>
          <div class="form-group">
            <label for="manual-bits">Bits</label>
            <select id="manual-bits">
              <option value="8">8</option>
              <option value="16">16</option>
              <option value="24">24</option>
              <option value="32">32</option>
            </select>
          </div>
        </div>
        <div class="form-group">
          <label><input id="manual-signed" type="checkbox"> Treat as signed</label>
        </div>
      </div>

      <!-- Read Operations -->
      <div class="card">
        <h3>📖 Read Register</h3>
        <div id="current-register-info" style="background: #f7fafc; padding: 12px; border-radius: 6px; margin-bottom: 16px; font-size: 0.9rem; color: #4a5568;">
          Select a register to read
        </div>
        
        <div style="display: flex; gap: 12px; flex-wrap: wrap; margin-bottom: 16px;">
          <button id="read-btn" class="btn-primary" disabled>Read Once</button>
          <button id="burst-btn" class="btn-secondary" disabled>🔄 Start Burst</button>
          <div style="display: flex; align-items: center; gap: 8px;">
            <label for="burst-interval" style="margin: 0; font-size: 0.85rem;">Interval:</label>
            <select id="burst-interval" style="width: auto; min-width: 80px;">
              <option value="10">10ms</option>
              <option value="100">100ms</option>
              <option value="250">250ms</option>
              <option value="500" selected>500ms</option>
              <option value="1000">1s</option>
              <option value="2000">2s</option>
              <option value="5000">5s</option>
            </select>
          </div>
        </div>
        
        <div id="read-result" style="display: none;">
          <div class="value-display">
            <div class="value-item">
              <div class="value-label">Decimal</div>
              <div class="value-number" id="read-decimal">-</div>
            </div>
            <div class="value-item">
              <div class="value-label">Hexadecimal</div>
              <div class="value-number" id="read-hex">-</div>
            </div>
            <div class="value-item">
              <div class="value-label">Binary</div>
              <div class="value-number" id="read-binary" style="font-size: 0.8rem; word-break: break-all;">-</div>
            </div>
          </div>
          <div style="display: flex; justify-content: space-between; align-items: center; margin-top: 8px;">
            <div class="timing" id="read-timing"></div>
            <div id="burst-stats" style="font-size: 0.8rem; color: #718096;"></div>
          </div>
        </div>
        
        <!-- Burst History -->
        <div id="burst-history" style="display: none; margin-top: 16px;">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
            <h4 style="margin: 0; font-size: 1rem; color: #4a5568;">📊 Real-time History</h4>
            <button id="clear-history-btn" class="btn-secondary" style="padding: 4px 8px; font-size: 0.8rem; min-width: auto;">Clear</button>
          </div>
          <div id="history-chart" style="height: 200px; border: 1px solid #e2e8f0; border-radius: 6px; background: white; position: relative; overflow: hidden;">
            <canvas id="chart-canvas" style="width: 100%; height: 100%;"></canvas>
          </div>
          <div id="history-log" style="height: 150px; overflow-y: auto; border: 1px solid #e2e8f0; border-radius: 6px; background: #f7fafc; padding: 8px; margin-top: 8px; font-family: monospace; font-size: 0.8rem;"></div>
        </div>
      </div>

      <!-- Write Operations -->
      <div class="card">
        <h3>✏️ Write Register</h3>
        <div class="form-group">
          <label for="write-value">Value (decimal, hex 0x..., or binary 0b...)</label>
          <input id="write-value" type="text" placeholder="4194304, 0x400000, or 0b10000000000000000000000">
        </div>
        
        <button id="write-btn" class="btn-primary" disabled>Write Register</button>
        
        <div id="write-result" style="display: none;">
          <div class="timing" id="write-timing"></div>
        </div>
      </div>
    </div>
  </div>

<script>
// Global state
let currentRegister = null;
let deviceConfig = {
  ip: "{{ current_ip }}",
  username: "{{ current_username }}",
  password: "{{ current_password }}"
};

// Burst mode state
let burstMode = {
  active: false,
  intervalId: null,
  readings: [],
  maxReadings: 100,
  startTime: null,
  totalReads: 0,
  errors: 0,
  abortController: null,
  pendingRequests: 0
};

// DOM elements
const registerList = document.getElementById('register-list');
const searchInput = document.getElementById('search');
const readBtn = document.getElementById('read-btn');
const burstBtn = document.getElementById('burst-btn');
const writeBtn = document.getElementById('write-btn');
const writeValueInput = document.getElementById('write-value');
const currentRegisterInfo = document.getElementById('current-register-info');
const burstIntervalSelect = document.getElementById('burst-interval');

// Initialize
document.addEventListener('DOMContentLoaded', function() {
  setupEventListeners();
  filterRegisters();
});

function setupEventListeners() {
  // Search functionality
  searchInput.addEventListener('input', filterRegisters);
  
  // Register selection
  registerList.addEventListener('click', function(e) {
    const item = e.target.closest('.register-item');
    if (item) selectRegister(item);
  });
  
  // Manual address inputs
  document.getElementById('manual-address').addEventListener('input', updateManualRegister);
  document.getElementById('manual-bits').addEventListener('change', updateManualRegister);
  document.getElementById('manual-signed').addEventListener('change', updateManualRegister);
  
  // Action buttons
  readBtn.addEventListener('click', performRead);
  burstBtn.addEventListener('click', toggleBurstMode);
  writeBtn.addEventListener('click', performWrite);
  document.getElementById('clear-history-btn').addEventListener('click', clearHistory);
  
  // Config update
  document.getElementById('update-config').addEventListener('click', updateConfig);
}

function filterRegisters() {
  const query = searchInput.value.toLowerCase();
  const items = registerList.querySelectorAll('.register-item');
  
  items.forEach(item => {
    const name = item.dataset.name.toLowerCase();
    const address = item.dataset.address;
    const desc = item.dataset.desc.toLowerCase();
    const addressHex = '0x' + parseInt(address).toString(16);
    
    const matches = name.includes(query) || 
                   address.includes(query) || 
                   addressHex.includes(query) ||
                   desc.includes(query);
    
    item.classList.toggle('hidden', !matches);
  });
}

function selectRegister(item) {
  // Remove previous selection
  registerList.querySelectorAll('.register-item').forEach(i => i.classList.remove('selected'));
  
  // Select new item
  item.classList.add('selected');
  
  currentRegister = {
    name: item.dataset.name,
    address: parseInt(item.dataset.address),
    bits: parseInt(item.dataset.bits),
    signed: item.dataset.signed === '1',
    desc: item.dataset.desc
  };
  
  updateRegisterInfo();
  enableButtons();
}

function updateManualRegister() {
  const address = document.getElementById('manual-address').value.trim();
  const bits = parseInt(document.getElementById('manual-bits').value);
  const signed = document.getElementById('manual-signed').checked;
  
  if (address) {
    try {
      const parsedAddress = parseAddress(address);
      currentRegister = {
        name: 'Manual',
        address: parsedAddress,
        bits: bits,
        signed: signed,
        desc: 'Manually entered address'
      };
      
      // Clear list selection
      registerList.querySelectorAll('.register-item').forEach(i => i.classList.remove('selected'));
      
      updateRegisterInfo();
      enableButtons();
    } catch (e) {
      currentRegister = null;
      updateRegisterInfo();
      disableButtons();
    }
  } else {
    currentRegister = null;
    updateRegisterInfo();
    disableButtons();
  }
}

function parseAddress(addr) {
  addr = addr.trim();
  if (addr.toLowerCase().startsWith('0x')) {
    return parseInt(addr, 16);
  } else if (addr.toLowerCase().startsWith('0b')) {
    return parseInt(addr.slice(2), 2);
  }
  return parseInt(addr, 10);
}

function parseValue(val) {
  val = val.trim();
  if (val.toLowerCase().startsWith('0x')) {
    return parseInt(val, 16);
  } else if (val.toLowerCase().startsWith('0b')) {
    return parseInt(val.slice(2), 2);
  }
  return parseInt(val, 10);
}

function updateRegisterInfo() {
  if (currentRegister) {
    currentRegisterInfo.innerHTML = `
      <strong>${currentRegister.name}</strong><br>
      Address: <code>0x${currentRegister.address.toString(16).toUpperCase().padStart(3, '0')}</code> 
      (${currentRegister.address})<br>
      ${currentRegister.bits}-bit ${currentRegister.signed ? 'signed' : 'unsigned'}<br>
      <em>${currentRegister.desc}</em>
    `;
  } else {
    currentRegisterInfo.textContent = 'Select a register or enter manual address';
  }
}

function enableButtons() {
  readBtn.disabled = false;
  burstBtn.disabled = false;
  writeBtn.disabled = false;
}

function disableButtons() {
  readBtn.disabled = true;
  burstBtn.disabled = true;
  writeBtn.disabled = true;
}

function updateConfig() {
  deviceConfig.ip = document.getElementById('device_ip').value.trim();
  deviceConfig.username = document.getElementById('username').value.trim();
  deviceConfig.password = document.getElementById('password').value;
  
  document.getElementById('current-device').textContent = deviceConfig.ip;
  document.getElementById('current-auth').textContent = deviceConfig.username + '/***';
  
  showStatus('Configuration updated', 'success');
}

function showStatus(message, type = 'info') {
  const status = document.getElementById('connection-status');
  const originalContent = status.innerHTML;
  
  status.className = `status ${type}`;
  status.textContent = message;
  
  setTimeout(() => {
    status.className = 'status info';
    status.innerHTML = originalContent;
  }, 3000);
}

function toggleBurstMode() {
  if (burstMode.active) {
    stopBurstMode();
  } else {
    startBurstMode();
  }
}

function startBurstMode() {
  if (!currentRegister) return;
  
  burstMode.active = true;
  burstMode.startTime = Date.now();
  burstMode.totalReads = 0;
  burstMode.errors = 0;
  burstMode.readings = [];
  burstMode.pendingRequests = 0;
  burstMode.abortController = new AbortController();
  
  // Update UI
  burstBtn.textContent = '⏹️ Stop Burst';
  burstBtn.className = 'btn-danger';
  readBtn.disabled = true;
  writeBtn.disabled = true;
  burstIntervalSelect.disabled = true;
  
  // Show history section
  document.getElementById('burst-history').style.display = 'block';
  initChart();
  
  // Start reading
  const interval = parseInt(burstIntervalSelect.value);
  burstMode.intervalId = setInterval(() => {
    if (burstMode.active) {
      performBurstRead();
    }
  }, interval);
  
  showStatus(`Burst mode started (${interval}ms interval)`, 'success');
}

function stopBurstMode() {
  burstMode.active = false;
  
  // Cancel all pending requests
  if (burstMode.abortController) {
    burstMode.abortController.abort();
    burstMode.abortController = null;
  }
  
  if (burstMode.intervalId) {
    clearInterval(burstMode.intervalId);
    burstMode.intervalId = null;
  }
  
  // Update UI
  burstBtn.textContent = '🔄 Start Burst';
  burstBtn.className = 'btn-secondary';
  readBtn.disabled = false;
  writeBtn.disabled = false;
  burstIntervalSelect.disabled = false;
  
  const duration = Math.round((Date.now() - burstMode.startTime) / 1000);
  const pendingMsg = burstMode.pendingRequests > 0 ? ` (${burstMode.pendingRequests} requests cancelled)` : '';
  showStatus(`Burst mode stopped. ${burstMode.totalReads} reads in ${duration}s${pendingMsg}`, 'info');
  
  // Reset pending counter
  burstMode.pendingRequests = 0;
}

async function performBurstRead() {
  // Double check if burst mode is still active
  if (!currentRegister || !burstMode.active) return;
  
  // Increment pending requests counter
  burstMode.pendingRequests++;
  
  try {
    const startTime = performance.now();
    
    const params = new URLSearchParams({
      address: currentRegister.address,
      bits: currentRegister.bits
    });
    
    if (currentRegister.signed) {
      params.append('signed', 'true');
    }
    
    const response = await fetch(`/api/read?${params}`, {
      method: 'GET',
      headers: {
        'X-Device-IP': deviceConfig.ip,
        'X-Username': deviceConfig.username,
        'X-Password': deviceConfig.password
      },
      signal: burstMode.abortController?.signal
    });
    
    // Check again if burst mode is still active after the request
    if (!burstMode.active) {
      burstMode.pendingRequests--;
      return;
    }
    
    const endTime = performance.now();
    const duration = Math.round(endTime - startTime);
    
    if (response.ok) {
      const data = await response.json();
      
      // Final check before processing the response
      if (!burstMode.active) {
        burstMode.pendingRequests--;
        return;
      }
      
      burstMode.totalReads++;
      
      // Add to readings history
      const reading = {
        timestamp: Date.now(),
        value: data.value,
        duration: duration
      };
      
      burstMode.readings.push(reading);
      if (burstMode.readings.length > burstMode.maxReadings) {
        burstMode.readings.shift(); // Remove oldest
      }
      
      // Update display
      displayReadResult(data.value, duration);
      updateBurstStats();
      updateChart();
      addToHistory(reading);
      
    } else {
      if (burstMode.active) {
        burstMode.errors++;
        updateBurstStats();
      }
      console.error('Burst read failed:', await response.text());
    }
  } catch (error) {
    if (error.name === 'AbortError') {
      // Request was cancelled, this is expected when stopping burst mode
      console.log('Burst read cancelled');
    } else if (burstMode.active) {
      burstMode.errors++;
      updateBurstStats();
      console.error('Burst read error:', error);
    }
  } finally {
    // Always decrement pending requests counter
    burstMode.pendingRequests--;
  }
}

function updateBurstStats() {
  const statsEl = document.getElementById('burst-stats');
  const elapsed = Math.round((Date.now() - burstMode.startTime) / 1000);
  const rate = elapsed > 0 ? (burstMode.totalReads / elapsed).toFixed(1) : '0.0';
  
  statsEl.textContent = `📊 ${burstMode.totalReads} reads, ${burstMode.errors} errors, ${rate} reads/s`;
}

function initChart() {
  const canvas = document.getElementById('chart-canvas');
  const ctx = canvas.getContext('2d');
  
  // Set canvas size
  canvas.width = canvas.offsetWidth;
  canvas.height = canvas.offsetHeight;
  
  // Clear canvas
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  
  // Draw axes
  ctx.strokeStyle = '#e2e8f0';
  ctx.lineWidth = 1;
  
  // Y axis
  ctx.beginPath();
  ctx.moveTo(40, 20);
  ctx.lineTo(40, canvas.height - 30);
  ctx.stroke();
  
  // X axis
  ctx.beginPath();
  ctx.moveTo(40, canvas.height - 30);
  ctx.lineTo(canvas.width - 20, canvas.height - 30);
  ctx.stroke();
}

function updateChart() {
  if (burstMode.readings.length < 2) return;
  
  const canvas = document.getElementById('chart-canvas');
  const ctx = canvas.getContext('2d');
  
  // Clear and redraw axes
  initChart();
  
  const readings = burstMode.readings;
  const chartWidth = canvas.width - 60;
  const chartHeight = canvas.height - 50;
  
  // Find min/max values
  const values = readings.map(r => r.value);
  const minVal = Math.min(...values);
  const maxVal = Math.max(...values);
  const range = maxVal - minVal || 1;
  
  // Draw value labels
  ctx.fillStyle = '#718096';
  ctx.font = '10px monospace';
  ctx.textAlign = 'right';
  ctx.fillText(maxVal.toString(), 35, 25);
  ctx.fillText(minVal.toString(), 35, canvas.height - 35);
  
  // Draw line
  ctx.strokeStyle = '#3182ce';
  ctx.lineWidth = 2;
  ctx.beginPath();
  
  readings.forEach((reading, index) => {
    const x = 40 + (index / (readings.length - 1)) * chartWidth;
    const y = 20 + (1 - (reading.value - minVal) / range) * chartHeight;
    
    if (index === 0) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }
  });
  
  ctx.stroke();
  
  // Draw points
  ctx.fillStyle = '#3182ce';
  readings.forEach((reading, index) => {
    const x = 40 + (index / (readings.length - 1)) * chartWidth;
    const y = 20 + (1 - (reading.value - minVal) / range) * chartHeight;
    
    ctx.beginPath();
    ctx.arc(x, y, 2, 0, 2 * Math.PI);
    ctx.fill();
  });
}

function addToHistory(reading) {
  const historyLog = document.getElementById('history-log');
  const timestamp = new Date(reading.timestamp).toLocaleTimeString();
  
  const logEntry = document.createElement('div');
  logEntry.innerHTML = `<span style="color: #718096;">${timestamp}</span> <span style="color: #2d3748; font-weight: 600;">${reading.value}</span> <span style="color: #a0aec0;">(0x${reading.value.toString(16).toUpperCase()}) ${reading.duration}ms</span>`;
  
  historyLog.appendChild(logEntry);
  historyLog.scrollTop = historyLog.scrollHeight;
  
  // Limit log entries
  while (historyLog.children.length > 100) {
    historyLog.removeChild(historyLog.firstChild);
  }
}

function clearHistory() {
  burstMode.readings = [];
  document.getElementById('history-log').innerHTML = '';
  initChart();
}

async function performRead() {
  if (!currentRegister) return;
  
  const startTime = performance.now();
  readBtn.classList.add('loading');
  readBtn.textContent = 'Reading...';
  
  try {
    const params = new URLSearchParams({
      address: currentRegister.address,
      bits: currentRegister.bits
    });
    
    if (currentRegister.signed) {
      params.append('signed', 'true');
    }
    
    const response = await fetch(`/api/read?${params}`, {
      method: 'GET',
      headers: {
        'X-Device-IP': deviceConfig.ip,
        'X-Username': deviceConfig.username,
        'X-Password': deviceConfig.password
      }
    });
    
    const endTime = performance.now();
    const duration = Math.round(endTime - startTime);
    
    if (response.ok) {
      const data = await response.json();
      displayReadResult(data.value, duration);
      showStatus(`Read successful (${duration}ms)`, 'success');
    } else {
      const error = await response.text();
      showStatus(`Read failed: ${error}`, 'error');
      hideReadResult();
    }
  } catch (error) {
    const endTime = performance.now();
    const duration = Math.round(endTime - startTime);
    showStatus(`Network error: ${error.message}`, 'error');
    hideReadResult();
  } finally {
    readBtn.classList.remove('loading');
    readBtn.textContent = 'Read Once';
  }
}

async function performWrite() {
  if (!currentRegister) return;
  
  const valueStr = writeValueInput.value.trim();
  if (!valueStr) {
    showStatus('Please enter a value to write', 'error');
    return;
  }
  
  try {
    const value = parseValue(valueStr);
    
    const startTime = performance.now();
    writeBtn.classList.add('loading');
    writeBtn.textContent = 'Writing...';
    
    const payload = {
      address: currentRegister.address,
      bits: currentRegister.bits,
      value: value
    };
    
    const response = await fetch('/api/write', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'X-Device-IP': deviceConfig.ip,
        'X-Username': deviceConfig.username,
        'X-Password': deviceConfig.password
      },
      body: JSON.stringify(payload)
    });
    
    const endTime = performance.now();
    const duration = Math.round(endTime - startTime);
    
    if (response.ok) {
      const result = await response.text();
      displayWriteResult(result, duration);
      showStatus(`Write successful (${duration}ms)`, 'success');
    } else {
      const error = await response.text();
      showStatus(`Write failed: ${error}`, 'error');
      hideWriteResult();
    }
  } catch (error) {
    showStatus(`Invalid value: ${error.message}`, 'error');
  } finally {
    writeBtn.classList.remove('loading');
    writeBtn.textContent = 'Write Register';
  }
}

function displayReadResult(value, duration) {
  document.getElementById('read-decimal').textContent = value;
  document.getElementById('read-hex').textContent = '0x' + value.toString(16).toUpperCase();
  document.getElementById('read-binary').textContent = '0b' + value.toString(2);
  document.getElementById('read-timing').textContent = `⏱️ ${duration}ms`;
  document.getElementById('read-result').style.display = 'block';
}

function hideReadResult() {
  document.getElementById('read-result').style.display = 'none';
}

function displayWriteResult(message, duration) {
  const writeResult = document.getElementById('write-result');
  writeResult.innerHTML = `
    <div style="color: #22543d; font-weight: 600;">✅ ${message}</div>
  `;
  document.getElementById('write-timing').textContent = `⏱️ ${duration}ms`;
  writeResult.style.display = 'block';
}

function hideWriteResult() {
  document.getElementById('write-result').style.display = 'none';
}
</script>
</body>
</html>
"""

def _parse_int_maybe_hex(s: str) -> int:
    s = (s or "").strip()
    if s.lower().startswith("0x"):
        return int(s, 16)
    return int(s, 10)

def _safe_int(value, default=None):
    try:
        return int(value)
    except Exception:
        return default

@app.route("/", methods=["GET"])
def index():
    return render_template_string(HTML, 
                                registers=REGISTERS, 
                                current_ip=current_device_ip,
                                current_username=current_username,
                                current_password=current_password)

@app.route("/api/read", methods=["GET"])
def api_read():
    # Get device config from headers
    device_ip = request.headers.get("X-Device-IP", current_device_ip)
    username = request.headers.get("X-Username", current_username)  
    password = request.headers.get("X-Password", current_password)
    
    addr_s = request.args.get("address", "").strip()
    bits_s = request.args.get("bits", "").strip()
    signed_s = request.args.get("signed", "false").strip().lower()

    if not addr_s or not bits_s:
        return "Address and bits are required for read", 400

    try:
        address = _parse_int_maybe_hex(addr_s)
        bits = int(bits_s)
        signed = (signed_s == "true")
    except Exception as e:
        return f"Invalid inputs: {e}", 400

    params = {"address": address, "bits": bits}
    if signed:
        params["signed"] = "true"

    try:
        base_url = f"http://{device_ip}/api/v1/ade7953/register"
        auth = HTTPDigestAuth(username, password)
        
        resp = requests.get(base_url, params=params, auth=auth, timeout=TIMEOUT)
        if resp.status_code != 200:
            return f"Device responded {resp.status_code}: {resp.text}", resp.status_code

        return resp.json()
    except requests.exceptions.RequestException as e:
        return f"Request error: {e}", 500

@app.route("/api/write", methods=["POST"])
def api_write():
    # Get device config from headers
    device_ip = request.headers.get("X-Device-IP", current_device_ip)
    username = request.headers.get("X-Username", current_username)
    password = request.headers.get("X-Password", current_password)
    
    data = request.get_json()
    if not data:
        return "JSON payload required", 400
        
    address = data.get("address")
    bits = data.get("bits") 
    value = data.get("value")

    if address is None or bits is None or value is None:
        return "Address, bits and value are required for write", 400

    try:
        address = int(address)
        bits = int(bits)
        value = int(value)
    except Exception as e:
        return f"Invalid inputs: {e}", 400

    payload = {"address": address, "bits": bits, "value": value}
    
    try:
        base_url = f"http://{device_ip}/api/v1/ade7953/register"
        auth = HTTPDigestAuth(username, password)
        
        # Double write if required by the register
        if address in DOUBLE_WRITE_ADDRESSES:
            r1 = requests.put(base_url, json=payload, auth=auth, timeout=TIMEOUT)
            r2 = requests.put(base_url, json=payload, auth=auth, timeout=TIMEOUT)
            ok = (r1.status_code == 200 and r2.status_code == 200)
            if not ok:
                return f"CFxDEN double-write failed: first={r1.status_code}, second={r2.status_code}", 500
            return "Double-write successful"
        else:
            r = requests.put(base_url, json=payload, auth=auth, timeout=TIMEOUT)
            if r.status_code != 200:
                return f"Write failed ({r.status_code}): {r.text}", r.status_code
            return "Write successful"
    except requests.exceptions.RequestException as e:
        return f"Request error: {e}", 500

if __name__ == "__main__":
    # Bind to localhost:5009
    app.run(host="127.0.0.1", port=5009, debug=False)
