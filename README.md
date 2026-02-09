# Pinao-glove
Pinao glove
# Piano Glove – Wearable MIDI Controller (BLE Client)

This repository contains the **ESP32-based controller code** for a wearable piano glove that converts **finger pressure inputs** into **real-time MIDI output**.

The device operates as a **BLE client**, receiving streamed sensor data (IMU orientation and pressure values) from an external BLE server node, and translating the data into MIDI note, chord, and arpeggio events.

---

## Functionality Overview

- Receives **pressure sensor and IMU data** via BLE notifications  
- Maps fingertip pressure to **MIDI Note On / Off** with velocity control  
- Supports **single-note and chord modes** using combo key logic  
- Implements **arpeggiated chord playback** triggered by hand orientation  
- Generates standard **MIDI messages** over UART to drive a digital piano or synthesizer  
- Sends **All Notes Off** on BLE disconnect to prevent stuck notes  

---

## System Architecture

- **BLE Client (this code):**  
  ESP32 glove controller that processes sensor data and generates MIDI output  

- **BLE Server (external):**  
  Sensor node providing IMU and pressure data (not included in this repository)

---

## Notes

This repository focuses on the **client-side control logic and system integration**.  
The BLE server firmware was provided separately and is not included.
