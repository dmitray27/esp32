# ESP32 Data Monitoring System

**Repository**:  
https://github.com/dmitray27/esp32

[![Live Demo](https://img.shields.io/badge/Live_Demo-Timeweb_Cloud-blue)](https://dmitray27-esp32-7017.twc1.net/)

## 📌 Overview
A real-time IoT monitoring system using ESP32 microcontroller with web interface via Flask. Deployed on **Timeweb Cloud**.

## 🌟 Key Features
- Real-time sensor data logging (temperature, voltage, CPU metrics)
- JSON data storage in `tem.txt` and `logs/temp_log.txt`
- Auto-refreshing web interface (5-second updates)
- Production deployment with Nginx reverse proxy

## 🚀 Quick Access
Live demo: [https://dmitray27-esp32-7017.twc1.net/](https://dmitray27-esp32-7017.twc1.net/)

## 🛠️ Technical Stack
| Component       | Technology          |
|-----------------|---------------------|
| Microcontroller | ESP32-WROOM-32      |
| Backend         | Python Flask        |
| Frontend        | HTML/CSS/JavaScript |
| Hosting         | Timeweb Cloud       |

## 📥 Installation
```bash
git clone https://github.com/dmitray27/esp32.git
cd esp32
pip install -r requirements.txt
python web/app.py