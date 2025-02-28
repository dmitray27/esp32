from flask import Flask, render_template, jsonify, make_response
from datetime import datetime
import os
import json
import requests
import threading
import logging

# Проверка запуска через Gunicorn
IS_GUNICORN = "gunicorn" in os.environ.get("SERVER_SOFTWARE", "")

app = Flask(__name__)

# Настройка логирования
app.logger.setLevel(logging.INFO)
handler = logging.StreamHandler()
handler.setFormatter(logging.Formatter('[%(asctime)s] %(levelname)s: %(message)s'))
app.logger.addHandler(handler)

# Конфигурация GitHub
GITHUB_RAW_URL = "https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt"
UPDATE_INTERVAL = 180  # Интервал синхронизации (секунды)

# Инициализация данных с default-значениями
DEFAULT_DATA = {
    "temperature": "N/A",
    "date": "N/A",
    "time": "N/A",
    "error": "Идет первоначальная загрузка..."
}
latest_data = DEFAULT_DATA.copy()
lock = threading.Lock()

def fetch_from_github():
    global latest_data
    try:
        app.logger.info("Starting GitHub data sync...")
        response = requests.get(GITHUB_RAW_URL, timeout=10)
        response.raise_for_status()

        app.logger.debug(f"Raw response: {response.text}")
        raw_data = response.text.strip()
        data = json.loads(raw_data)

        # Валидация данных
        required_keys = {'temperature', 'timestamp'}
        if missing_keys := required_keys - set(data.keys()):
            raise ValueError(f"Отсутствуют обязательные поля: {missing_keys}")

        # Парсинг времени с часовым поясом (+0300)
        dt = datetime.fromisoformat(data['timestamp'])

        with lock:
            latest_data.update({
                "temperature": data.get('temperature', 'N/A'),
                "date": dt.strftime("%Y-%m-%d"),
                "time": dt.strftime("%H:%M:%S"),
                "error": None
            })
            app.logger.info(f"Data updated successfully")

    except json.JSONDecodeError as e:
        error_msg = f"Ошибка формата JSON: {str(e)}"
        app.logger.error(f"GitHub sync failed: {error_msg}")
       