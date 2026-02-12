# 🛡️ Copper Guard – Version 4

> Optimized 4-Channel Monitoring System  
> Integrated PCB Design | GSM + WiFi + OLED

---

## 📌 Overview

Copper Guard Version 4 is the optimized and refined version of the system, designed specifically for 4-way cross road installations.

Unlike previous versions supporting 8 wires, Version 4 is intentionally limited to 4 wires, as a standard 4-way junction requires monitoring of only four copper lines.

This version integrates the ESP12F chip directly onto a custom PCB and includes an OLED display for real-time system status monitoring.

---

## 🚀 Key Improvements Over Version 3

- Optimized from 8 wires → 4 wires (junction-specific design)
- Integrated **ESP12F chip** directly on PCB
- Added **OLED display** for live system feedback
- Cleaner and more compact PCB layout
- Dedicated GSM board with antenna
- Reduced hardware complexity

---

## 🔧 Hardware Components

- 📡 GSM Board with Antenna
- 🌐 ESP12F WiFi Chip (Integrated on PCB)
- 🔌 4 Optocouplers
- ⚡ Buck Converter
- 🖥 OLED Display
- 🖨 Custom Designed PCB

---

## ⚙️ System Specifications

| Feature            | Description                      |
|--------------------|----------------------------------|
| Wire Capacity      | 4 Wires                          |
| Communication      | GSM + WiFi                       |
| Alert System       | SMS + Phone Call                 |
| Display            | OLED Status Display              |
| Power Regulation   | Buck Converter                   |
| Isolation Method   | Optocouplers                     |
| Design Type        | Fully Integrated Custom PCB      |

---

## 🧠 Working Principle

1. Each of the 4 copper lines is connected through an optocoupler for isolation.
2. The ESP12F continuously monitors line status.
3. If a wire is cut or disabled:
   - 📩 SMS alert is sent via GSM module
   - 📞 Phone call notification is triggered
4. The OLED display shows:
   - Line status
   - System state
   - Connectivity information
5. WiFi connectivity allows future IoT expansion or monitoring.

---

## 📂 Folder Structure
````
version4
│
├── firmware
│ ├── main.ino
│ └── libraries.txt
│
├── hardware
│ ├── schematic.pdf
│ ├── pcb_layout.pdf
│ └── gerber_files
│
├── Images
│ ├── pcb_top.jpg
│ ├── pcb_bottom.jpg
│ ├── assembled_unit.jpg
│ └── oled_display.jpg
│
└── README.md
````
---

## 📡 Communication Architecture
````
Copper Wire Inputs (4 Lines)
│
▼
Optocouplers (Isolation Stage)
│
▼
ESP12F (Main Controller)
│
├──► GSM Board
│ ├── SMS Alert
│ └── Phone Call Alert
│
├──► OLED Display
│ └── Real-Time Status Display
│
└──► WiFi Network
└── Remote Monitoring (Optional)
````
---

## 🌍 Deployment Design Context

Version 4 is designed specifically for:

- 4-way road junction installations
- Compact field deployment
- Reduced hardware overhead
- Improved maintainability

Since a standard 4-way cross road only requires monitoring of 4 copper lines, supporting 8 wires is unnecessary.  
Version 4 removes excess complexity while improving integration.

---

## ⚠️ Limitations

- Limited to 4 wire inputs (junction-specific)
- Requires stable GSM signal for reliable alerting
- WiFi functionality depends on network availability

---

## 🔮 System Evolution Summary

| Version | Key Change |
|----------|------------|
| V1 | Basic GSM prototype (4 wires) |
| V2 | Expanded to 8 wires + WiFi |
| V3 | Custom PCB implementation |
| V4 | Integrated ESP12F + OLED + Optimized 4-wire design |

---

## 🛠 Installation Overview

1. Connect 4 copper lines to input terminals.
2. Power the system through regulated supply.
3. Insert configured SIM card into GSM board.
4. Configure alert numbers in firmware.
5. Verify OLED display for system status.
6. Test by simulating wire disconnection.

---
