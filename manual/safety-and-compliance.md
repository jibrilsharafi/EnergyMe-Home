# EnergyMe Home: Safety & Compliance Information

This document contains the regulatory, safety, warranty, and disposal information for *EnergyMe Home*. **Read it once before installing the device.** Keep this document for the lifetime of the product; if the product is transferred to a third party, this document must be transferred with it.

For the practical step-by-step installation and setup, see the [Installation Manual](README.md).

---

## 1. Manufacturer

| | |
| --- | --- |
| **Company** | EnergyMe S.r.l. |
| **Registered address** | Via Plezzo 56, 20132 Milano (MI), Italy |
| **VAT / Codice Fiscale** | IT14543830963 |
| **Phone** | +39 320 8765133 |
| **Email** | <jibril.sharafi@energyme.net> |
| **Website** | <https://www.energyme.net> |
| **Support** | <support@energyme.net> |

**Authorized service.** Service, repair, and extraordinary maintenance are carried out exclusively by the manufacturer or by personnel authorized in writing by the manufacturer. Any intervention by unauthorized personnel voids the warranty and may compromise safety.

---

## 2. Product identification

| | |
| --- | --- |
| **Product name** | EnergyMe Home |
| **Product code** | EM-HOME-1.0-KIT-STANDARD |
| **Hardware revision** | v6.1 |
| **Open-source certification** | OSHWA `IT000025` |

The serial number, hardware revision, and manufacturing data are printed on the label inside the device case. Record them before installing the product; they are required for support requests and warranty claims.

---

## 3. Declaration of Conformity

*EnergyMe Home* is supplied with a separate **Declaration of Conformity (DoC)**, drawn up by the manufacturer under the applicable European Union directives. Before using the device, verify that the DoC is present in the package or available from the manufacturer.

The device complies with the essential requirements of the following EU legislation:

- **2014/35/EU** — Low Voltage Directive (LVD)
- **2014/30/EU** — Electromagnetic Compatibility (EMC)
- **2014/53/EU** — Radio Equipment Directive (RED)
- **2011/65/EU** — Restriction of Hazardous Substances (RoHS), as amended by Delegated Directive (EU) **2015/863**
- **2012/19/EU** — Waste Electrical and Electronic Equipment (WEEE)
- **Regulation (EU) 2023/988** — General Product Safety Regulation (GPSR)

The full list of harmonized standards applied (including those relating to radio safety, EMC, RF exposure, and cyber security requirements for internet-connected radio equipment) is given in the Declaration of Conformity.

> If the Declaration of Conformity is missing, **do not use the device**. Contact the manufacturer to obtain a copy.

---

## 4. Symbols and conventions

### 4.1 Symbols on the product

The following symbols may appear on the device label, on the packaging, or in this documentation. Their presence is mandatory and they must not be removed, covered, or obscured.

| Symbol | Meaning |
| --- | --- |
| **CE** | Conformity with the applicable EU directives (see §3) |
| **Crossed-out wheeled bin** (WEEE) | Separate collection of electrical and electronic equipment required at end of life (see §11) |
| **Lightning bolt in triangle** | Risk of electric shock; refer to the manual before any intervention |
| **Open book / "i" in circle** | Read the user manual before using the device |
| **Class II (double square)** | Equipment with reinforced or double insulation; no protective earth connection required |

### 4.2 Conventions used in the documentation

The user-facing manuals (installation, setup, troubleshooting) use the following inline callouts:

| Callout | Purpose |
| --- | --- |
| `⚠ WARNING` | Procedures whose non-observance may cause damage to the device, to property, or expose persons to danger |
| `⚠ NOTE` | Important conditions or constraints highlighted outside the running text |
| `ⓘ NOTE` | Useful information that complements a procedure |
| `✅ TIP` | Recommendations and best practices |

A red `🔴` or "DANGER" marker, where used, indicates procedures whose non-observance may cause injury or serious damage to the device.

---

## 5. Intended use

*EnergyMe Home* is a DIN-rail-mounted IoT device for measuring and monitoring electrical energy consumption and production in **residential**, **prosumer**, and **small commercial** environments. It supports up to 16 measurement channels (one direct and fifteen multiplexed) using non-invasive split-core current transformers (CTs).

The device is intended to be installed by a **qualified electrician** inside a closed electrical panel, in an indoor environment, on a standard DIN rail (EN 60715, DIN 43880), in compliance with the local electrical code and regulations of the country where it is installed.

The device monitors single-phase and three-phase electrical systems (see [Appendix B](appendices.md#appendix-b-three-phase-configuration)). It does not act on the electrical installation: it is a measurement and monitoring device only.

The device communicates over **Wi-Fi 2.4 GHz** (IEEE 802.11 b/g/n) and supports integration via REST API, MQTT, Modbus TCP, and InfluxDB.

---

## 6. Non-intended use

Any use other than the one described in §5 is considered improper. The manufacturer declines any responsibility for damage to persons, animals, or property arising from improper use. Improper use also voids the warranty.

In particular, the following are **strictly prohibited**:

- Use in environments other than indoor closed electrical panels (no outdoor use, no ATEX or potentially explosive atmospheres, no corrosive or high-dust environments, no exposure to direct sunlight, flames, or heat sources).
- Use with mains voltage or frequency outside the rated range (see [datasheet](../hardware/datasheet.md)).
- Use with current transformers, accessories, or spare parts not supplied or expressly approved by the manufacturer.
- Use as a billing-grade revenue meter for commercial energy transactions.
- Installation or intervention by unqualified personnel.
- Use by children (persons under 14 years of age) or by persons with reduced physical, sensory, or cognitive capacity, except under the supervision of a person responsible for their safety.
- Any modification, alteration, or tampering with the device, its case, internal circuitry, firmware, or labels.
- Operation of the device with a damaged case, exposed conductors, or visible signs of impact, deformation, or burning.
- Immersion in water or other liquids; exposure to dripping or splashing water.
- Disclosure of device access credentials to unauthorized persons.
- Continued use of the device after an anomaly has been detected.

The reuse of any component after the device has been decommissioned is the sole responsibility of the user and releases the manufacturer from any liability.

---

## 7. Residual risks

Despite the design and protective measures adopted by the manufacturer, the following residual risks remain:

- **Risk of electric shock** during installation, maintenance, and decommissioning. The device connects directly to AC mains. Always disconnect the supply via the main breaker and verify the absence of voltage with a tester before any intervention.
- **Risk of incorrect measurement** if current transformers are clamped incorrectly (on a shared input wire, on a comb bar, around both L and N, or around the protective earth). Refer to [Installation §3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-).
- **Risk of damage to the device** if the supply voltage exceeds the rated range, if the CT output voltage exceeds 0.5 V at maximum current, or if the device is subjected to mechanical impact.
- **Risk to network security** if the default password is not changed after first login or if the device is exposed to untrusted networks.

The user is responsible for reading this document and the Installation Manual in full before installing or operating the device.

---

## 8. Environmental and storage conditions

The device must be installed and operated **indoors only**, in environmental conditions compatible with its specifications.

| Parameter | Operating | Storage |
| --- | --- | --- |
| Temperature | 0 °C to +50 °C | -10 °C to +60 °C |
| Relative humidity | ≤ 80 %, non-condensing | ≤ 80 %, non-condensing |
| IP rating | IP20 (closed panel only) | — |
| Altitude | ≤ 2000 m | — |
| Pollution degree | 2 | — |

The storage area must be protected from direct sunlight, flames, heat sources, steam, corrosive vapors, and accessible to authorized personnel only.

---

## 9. Handling and unpacking

The device is supplied in a cardboard and plastic package designed to preserve its integrity during storage and transport. Despite this, handle the device with care to avoid impacts and falls.

**On unpacking, before installation:**

1. Verify that the package contains all the items listed in [Installation §1.1](01-installation.md#11-starter-kit-always-included).
2. Visually inspect the device, the CT clamps, and the cables for damage (cracks, deformation, exposed conductors, signs of impact).
3. Verify that the Declaration of Conformity (§3) and this document are present.

**If anything is missing, damaged, or non-conforming, do not install the device.** Contact `support@energyme.net` with a photograph of the package contents and the issue. Do not attempt to repair or modify the device.

Always handle the device by its ABS case; never pull it by the cables. After unpacking, dispose of the packaging according to local regulations and keep it out of the reach of children.

---

## 10. Maintenance

The device is designed for long operating life with minimal maintenance. **There are no user-serviceable parts inside.** Do not open the case; opening voids the warranty and exposes the user to electric shock.

### 10.1 Routine maintenance (by the user)

**At least once a year**, with the device powered off and disconnected from the mains:

1. Visually inspect the device, the cables, and the CT clamps for signs of damage, deformation, or overheating.
2. Verify that the labels on the device are legible and intact. If a label is damaged or unreadable, contact the manufacturer to obtain a replacement.
3. Remove dust from the case using a soft brush, a vacuum cleaner with a soft nozzle, or low-pressure compressed air.

> **⚠ WARNING: Cleaning agents**
>
> - ❌ Do **not** use solvents, alcohol, ammonia, or abrasive or corrosive cleaning agents.
> - ❌ Do **not** use abrasive cloths or sponges.
> - ❌ Do **not** spray liquids directly onto the device.
> - ✅ Use a dry or barely damp soft cloth on the outside of the case only, if needed.

### 10.2 Extraordinary maintenance (by the manufacturer)

Extraordinary maintenance (repairs, replacement of internal components, firmware recovery after physical fault) is performed exclusively by the manufacturer or by authorized personnel. Contact `support@energyme.net` for any intervention beyond routine cleaning.

### 10.3 Firmware updates

Firmware updates are part of normal product life and are delivered over-the-air through the web interface (see [Setup §4.8](02-setup.md#48-firmware-updates)). Apply available updates within a reasonable time to keep the device secure and aligned with the latest features.

### 10.4 Spare parts

Use original spare parts only. The use of non-original parts voids the warranty and releases the manufacturer from any liability for damage to persons or property. Order spare parts from `support@energyme.net`.

---

## 11. Disposal (WEEE)

<img src="https://upload.wikimedia.org/wikipedia/commons/7/7e/WEEE_symbol_vectors.svg" alt="WEEE symbol" width="60">

The **crossed-out wheeled bin** symbol printed on the device label indicates that at the end of its operational life, the device must be disposed of separately from household waste, in accordance with:

- **Directive 2012/19/EU** on Waste Electrical and Electronic Equipment (WEEE), as amended;
- **D.Lgs. 14 marzo 2014, n. 49** (Italian implementation of Directive 2012/19/EU).

**Do not dispose of the device or its accessories in general household waste.** Deliver them to an authorized WEEE collection point in your area, or return them to the manufacturer or the retailer when purchasing an equivalent new device, in accordance with local regulations.

Improper disposal is punishable under applicable national law and may cause environmental damage and harm to human health due to the presence of hazardous substances in electrical and electronic equipment.

The packaging materials (cardboard, plastic) must also be disposed of according to local recycling regulations.

---

## 12. Decommissioning

At the end of the device's operational life, or before permanent removal:

1. Disconnect the device from the mains via the main breaker.
2. Verify the absence of voltage with a tester.
3. Remove the device from the DIN rail and disconnect the L/N wires and the CT clamps.
4. **Erase all configuration data and credentials** via factory reset before disposal or transfer ([Appendix D](appendices.md#appendix-d-user-button-reference)). This protects your Wi-Fi credentials and any personal configuration.
5. Dispose of the device according to §11.

If the device is transferred to a third party (sale, donation, lease), **all the documentation accompanying the device must be transferred with it**, including this document, the Installation Manual, and the Declaration of Conformity.

---

## 13. Warranty

The product is covered by the warranty terms set out in the purchase contract and summarized below. The warranty applies only when the device is used under the intended-use conditions of §5 and in compliance with this documentation.

### 13.1 Duration

- **12 months** for business customers, in accordance with applicable EU and national legislation.
- **24 months** for end consumers, in accordance with Directive 1999/44/EC and D.Lgs. 206/2005 (Codice del Consumo).

The warranty period starts from the date of delivery, evidenced by the invoice or purchase receipt.

### 13.2 Coverage

The manufacturer undertakes, at its discretion, to repair or replace components found to be defective in materials or workmanship, following its own inspection. Replaced parts become the property of the manufacturer.

### 13.3 Exclusions

The warranty does **not** cover:

- Damage caused by improper use, negligence, accident, or non-observance of this documentation.
- Damage caused by external events (lightning, surges, flooding, fire, natural disasters).
- Damage caused by use with non-original accessories, spare parts, or current transformers.
- Damage to the case, labels, or electrical values caused by tampering, opening, or modification.
- Wear consistent with normal use.
- Costs of transport, shipping, and installation, which are at the buyer's expense.
- Difficulties arising from regulations in countries other than the country where the device was sold.

### 13.4 Claims

To claim the warranty, contact `support@energyme.net` providing:

1. Product code and serial number (printed on the label inside the device case).
2. Proof of purchase (invoice or receipt).
3. Detailed description of the issue and, where possible, photographs or video.

Any product returned without proof of purchase or with altered identification labels will be repaired at the buyer's expense.

### 13.5 Limitation of liability

The manufacturer is not liable for incidents or damage arising from improper use, unauthorized modification, or non-observance of this documentation. The full liability terms are set out in the purchase contract.

---

## 14. Document retention

The manufacturer retains the technical file for this product (including this document, the DoC, design documentation, and the records of conformity assessment) for **10 years** from the date the last unit is placed on the market, in accordance with the applicable EU directives.

During this period, the technical file is available exclusively to market surveillance authorities upon request.

---

