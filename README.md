# Medical Monitoring & Safety Device  
*(Medical Device Technology Course Project Fall 2025)*

This repository documents the iterative development of an **ESP32-based medical monitoring and safety system** completed over the duration of my **Medical Device Technology** course. The project was developed as a **breadboard-based prototype** (not physically wearable), while the firmware and system architecture were intentionally designed to **simulate the behavior of a wearable medical device**, including low-power operation, periodic sensing, and event-driven safety monitoring.

Development emphasized rapid prototyping, experimentation, and system-level iteration. The firmware was **vibe coded using Roo Code**, enabling fast iteration on embedded logic, sensor integration, and state-based behavior while maintaining a focus on engineering fundamentals and medical-device design constraints.

---

## Project Overview

The system monitors key physiological signals, including **heart rate (HR)** and **estimated blood oxygen saturation (SpO₂)**, using an optical pulse oximeter. Data is processed in real time and displayed on an OLED interface for immediate user feedback. To enhance safety monitoring, the system integrates a **3-axis accelerometer** to detect fall-like events and trigger on-device alerts.

A **deep-sleep power management strategy** allows the device to periodically wake, acquire measurements, and return to a low-power state, modeling battery-efficient behavior appropriate for wearable or home-care medical devices.

---

## Key Features
- Heart rate (BPM) estimation using optical PPG sensing  
- Estimated blood oxygen saturation (SpO₂) calculation  
- Real-time OLED display output  
- Accelerometer-based fall detection with alerting  
- Deep-sleep power cycling for low-power operation  
- Interrupt-driven push-button input for immediate user interaction  

---

## Project Evolution (Development Milestones)

Each folder in this repository represents a milestone completed during the course, showing how functionality and system complexity were incrementally introduced:

1. **WiFi Medical Alert Button** (`1medical_alert_button_sep18a/`)  
   - ESP32 web server prototype enabling network-based alert triggering.

2. **Button Interrupt Input** (`2button_interrupt_oct2a/`)  
   - Hardware interrupt-driven push button handling for responsive, low-power user input.

3. **Continuous Biometric Monitoring** (`3medical_device_nov27c/`)  
   - Heart rate and SpO₂ monitoring with OLED visualization.

4. **Low-Power Biometric Monitoring** (`4medical_device_sleep_nov27a/`)  
   - Deep-sleep cycling to simulate wearable-style battery operation.

5. **Biometric Monitoring with Fall Detection** (`5falldetector_dec18a/`)  
   - Integration of accelerometer-based fall detection and safety alerts.

This progression reflects a realistic medical device development workflow: **user input → alerting → sensing → power optimization → safety integration**.

---

## Hardware Used (Prototype)
- ESP32 microcontroller  
- MAX3010x pulse oximeter sensor  
- SSD1306 OLED display  
- ADXL345 3-axis accelerometer  
- Push button (interrupt input)  

---

## Notes & Disclaimer
This project was developed for **educational and prototyping purposes** as part of a university-level Medical Device Technology course. It is **not a clinical-grade medical device** and has not undergone medical calibration, validation, or regulatory review.

