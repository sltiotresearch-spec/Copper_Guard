# 🛡️ Copper Guard – Version 2

> Expanded 8-Channel Copper Wire Monitoring System  
> GSM + WiFi Enabled | Field Deployed Model

---

## 📌 Overview

Copper Guard Version 2 is an improved and expanded version of the original copper wire monitoring system.

This version:
- Increases wire capacity  
- Introduces WiFi connectivity  
- Improves system flexibility using an ESP-based board  

📍 **Five devices are currently deployed in the Ratnapura District.**

---

## 🚀 Key Improvements from Version 1

- Increased wire capacity from **4 → 8**
- Replaced STEM board with **ESP board**
- Added **WiFi connectivity**
- Improved scalability
- Field deployment of **5 devices**
- Dot board implementation for component connections

---

## 🔧 Hardware Components

- 📡 GSM Module (for SMS and Call alerts)
- 🌐 ESP Board (WiFi enabled)
- 🔌 8 Optocouplers (for wire monitoring)
- ⚡ Buck Converter (Voltage regulation)
- 📶 External Antenna
- 🧩 Dot Board (Prototype board assembly)

---

## ⚙️ System Specifications

| Feature            | Description        |
|--------------------|--------------------|
| Wire Capacity      | 8 Wires            |
| Communication      | GSM + WiFi         |
| Alert System       | SMS + Phone Call   |
| Power Regulation   | Buck Converter     |
| Isolation Method   | Optocouplers       |
| Deployment         | 5 Active Units     |

---

## 🧠 Working Principle

1. Each copper wire line is connected through an optocoupler.  
2. The ESP board continuously monitors the status of all 8 lines.  
3. If a wire is cut or disabled:
   - 📩 The GSM module sends an SMS alert  
   - 📞 A phone call notification is triggered  
4. WiFi connectivity allows remote monitoring or future IoT integration.

---

## 📂 Folder Structure
````
version2
│
├── firmware
│ ├── main.ino
│ └── libraries.txt
│
├── Images
│ ├── Image 1.jpeg
│ ├── Image 2.jpeg
│ └── Image 3.jpeg
│
└── README.md

````
---

## 📡 Communication Architecture
````
Wire Input
↓
Optocoupler
↓
ESP Board
├── GSM Module → SMS + Call
└── WiFi Network → Remote Monitoring (Optional)

````
---

## 🌍 Deployment Information

- **Location:** Ratnapura District  
- **Units Installed:** 5  
- **Environment:** Roadside / Field installations  
- **Status:** Operational  

---

## ⚠️ Limitations

- Built using dot board (not custom PCB)
- Manual assembly increases wiring complexity
- Requires stable GSM signal for reliable alerting

---

## 🔮 Future Improvements (Implemented in Later Versions)

- Custom PCB design (Version 3)
- Integrated ESP12F chip on PCB (Version 4)
- OLED status display (Version 4)
- Optimized wire capacity for junction-based installations

---

## 🛠 Installation Overview

1. Connect 8 copper lines to input terminals.  
2. Power the system via regulated supply (through buck converter).  
3. Insert configured SIM card into GSM module.  
4. Connect ESP to WiFi network (if remote monitoring required).  
5. Configure alert phone numbers in firmware.  
6. Deploy and test by simulating wire disconnection.

---

