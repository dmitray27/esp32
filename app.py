from flask import Flask, jsonify
from datetime import datetime, timezone
import requests
import json
import logging
import threading

app = Flask(__name__)

# Настройка логирования
app.logger.setLevel(logging.DEBUG)
handler = logging.StreamHandler()
handler.setFormatter(logging.Formatter('[%(asctime)s] %(levelname)s: %(message)s'))
app.logger.addHandler(handler)

# Конфигурация
GITHUB_RAW_URL = "https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt"
UPDATE_INTERVAL = 60

latest_data = {
    "temperature": "N/A",
    "date": "N/A",
    "time": "N/A",
    "status": "loading",      # Новое поле: статус
    "progress": "0%",         # Прогресс загрузки
    "details": "Старт приложения...",  # Детализация
    "error": None
}
lock = threading.Lock()

def fetch_from_github():
    global latest_data
    try:
        with lock:
            latest_data["details"] = "Подключение к GitHub..."
            latest_data["progress"] = "25%"

        app.logger.debug("Запрос к GitHub...")
        response = requests.get(GITHUB_RAW_URL, timeout=10)
        response.raise_for_status()

        with lock:
            latest_data["details"] = "Чтение данных..."
            latest_data["progress"] = "50%"

        data = json.loads(response.text)
        dt = datetime.fromisoformat(data['timestamp']).astimezone(timezone.utc)

        with lock:
            latest_data.update({
                "temperature": data["temperature"],
                "date": dt.strftime("%Y-%m-%d"),
                "time": dt.strftime("%H:%M:%S"),
                "status": "ready",          # Успешная загрузка
                "progress": "100%",
                "details": "Данные актуальны",
                "error": None
            })
        app.logger.info("Данные успешно обновлены")

    except Exception as e:
        error_msg = f"Ошибка
