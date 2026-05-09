// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

// NVS keys written to the factory namespace (PREFERENCES_NAMESPACE_FACTORY)
// during manufacturing provisioning. Central source of truth for production
// firmware, provisioning firmware (app0), and the Python provisioning host.
// Keep in sync with the FactoryProvisioningPayload pydantic model.

// Device identity / configuration
#define FACTORY_KEY_SERIAL_NUMBER "serial_number"
#define FACTORY_KEY_PCB_REVISION  "pcb_revision"
#define FACTORY_KEY_MFG_TS        "mfg_ts"
#define FACTORY_KEY_AP_PASSWORD   "ap_password"

// AWS IoT Core credentials (PEM, as written by the provisioning flow)
#define FACTORY_KEY_CERT_PEM      "cert_pem"
#define FACTORY_KEY_KEY_PEM       "key_pem"

// ADE7953 factory calibration coefficients
#define FACTORY_KEY_AV_GAIN       "av_gain"
#define FACTORY_KEY_AI_GAIN       "ai_gain"
#define FACTORY_KEY_BI_GAIN       "bi_gain"
#define FACTORY_KEY_AIRMS_OS      "airms_os"
#define FACTORY_KEY_BIRMS_OS      "birms_os"
#define FACTORY_KEY_AW_GAIN       "aw_gain"
#define FACTORY_KEY_BW_GAIN       "bw_gain"
#define FACTORY_KEY_AWATT_OS      "awatt_os"
#define FACTORY_KEY_BWATT_OS      "bwatt_os"
#define FACTORY_KEY_AVAR_GAIN     "avar_gain"
#define FACTORY_KEY_BVAR_GAIN     "bvar_gain"
#define FACTORY_KEY_AVAR_OS       "avar_os"
#define FACTORY_KEY_BVAR_OS       "bvar_os"
#define FACTORY_KEY_AVA_GAIN      "ava_gain"
#define FACTORY_KEY_BVA_GAIN      "bva_gain"
#define FACTORY_KEY_AVA_OS        "ava_os"
#define FACTORY_KEY_BVA_OS        "bva_os"
#define FACTORY_KEY_PHCAL_A       "phcal_a"
#define FACTORY_KEY_PHCAL_B       "phcal_b"
