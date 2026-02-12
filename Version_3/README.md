# 🛡️ Copper Guard – Version 3

> 8-Channel Copper Wire Monitoring System  
> Custom PCB Implementation | GSM + WiFi Enabled

---

## 📌 Overview

Copper Guard Version 3 is an improved hardware refinement of Version 2.

The core functionality remains the same:
- 8 wire monitoring
- GSM alert system
- WiFi connectivity via ESP board

The major improvement in Version 3 is the transition from a dot board assembly to a custom-designed PCB, significantly improving reliability, structure, and maintainability.

---

## 🚀 Improvements Over Version 2

- Replaced dot board with **custom PCB**
- Reduced wiring complexity
- Improved circuit stability
- Better mechanical strength
- Cleaner and more professional hardware layout

---

## 🔧 Hardware Components

- 📡 GSM Module
- 🌐 ESP Board (WiFi enabled)
- 🔌 8 Optocouplers
- ⚡ Buck Converter
- 📶 External Antenna
- 🖥 Custom Designed PCB

---

## ⚙️ System Specifications

| Feature            | Description        |
|--------------------|--------------------|
| Wire Capacity      | 8 Wires            |
| Communication      | GSM + WiFi         |
| Alert System       | SMS + Phone Call   |
| Power Regulation   | Buck Converter     |
| Isolation Method   | Optocouplers       |
| Board Type         | Custom PCB         |

---

## 🧠 Working Principle

1. Each copper wire line is connected through an optocoupler.
2. The ESP board continuously monitors the status of all 8 lines.
3. If a wire is cut or disabled:
   - 📩 The GSM module sends an SMS alert
   - 📞 A phone call notification is triggered
4. WiFi connectivity allows remote monitoring or IoT expansion.

---

## 📂 Folder Structure
````
version3
│
├── firmware
│ ├── main.ino
│ └── libraries.txt
│
├── hardware
│ ├── schematic.png
│ └── gerber_files
│
├── Images
│ └── device.jpeg
│
└── README.md
````
---

## 📡 Communication Architecture
````
Copper Wire Inputs (8 Lines)
│
▼
Optocouplers (Isolation Stage)
│
▼
ESP Board (Controller)
│
├──► GSM Module
│ ├── SMS Alert
│ └── Phone Call Alert
│
└──► WiFi Network
└── Remote Monitoring (Optional)
````
---

## 🌍 Deployment Context

Version 3 represents the transition from prototype-level hardware to a more production-ready design.

The use of a custom PCB:
- Improves long-term durability
- Simplifies assembly
- Reduces electrical noise
- Enhances maintainability

---

## ⚠️ Limitations

- Requires stable GSM signal for reliable alerting
- Depends on WiFi availability for remote monitoring
- Still limited to 8 wire inputs

---

## 🔮 Evolution Path

Version 3 paved the way for:
- Integrated ESP12F chip on PCB (Version 4)
- OLED status display
- Optimized 4-wire configuration for junction installations

---
