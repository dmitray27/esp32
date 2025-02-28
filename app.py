# app.py
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

# Глобальные переменные для кеширования
latest_data = {"error": "Идет первоначальная загрузка..."}
lock = threading.Lock()

def fetch_from_github():
    global latest_data
    try:
        app.logger.info("Starting GitHub data sync...")
        response = requests.get(GITHUB_RAW_URL, timeout=10)
        response.raise_for_status()

        raw_data = response.text.strip()
        data = json.loads(raw_data)

        # Валидация данных
        required_keys = {'temperature', 'timestamp'}
        if not required_keys.issubset(data.keys()):
            raise ValueError(f"Отсутствуют обязательные поля: {required_keys - set(data.keys())}")

        # Парсинг времени с обработкой временной зоны
        dt = datetime.fromisoformat(data['timestamp'].replace('Z', '+00:00')).astimezone()

        with lock:
            latest_data = {
                "temperature": data['temperature'],
                "date": dt.strftime("%Y-%m-%d"),
                "time": dt.strftime("%H:%M:%S"),
                "error": None
            }
            app.logger.info(f"Data updated: {latest_data}")

    except Exception as e:
        error_msg = f"{type(e).__name__}: {str(e)}"
        app.logger.error(f"GitHub sync failed: {error_msg}")
        
        with lock:
            latest_data = {
                **latest_data,  # Сохраняем предыдущие значения
                "error": error_msg
            }

def background_updater():
    app.logger.info("Background updater started")
    while True:
        fetch_from_github()
        threading.Event().wait(UPDATE_INTERVAL)

# Запуск фонового потока только в главном процессе
if not IS_GUNICORN or os.environ.get("WERKZEUG_RUN_MAIN") == "true":
    app.logger.info("Initializing background thread...")
    thread = threading.Thread(target=background_updater)
    thread.daemon = True
    thread.start()

@app.route("/health")
def health_check():
    return jsonify(status="OK", timestamp=datetime.utcnow().isoformat()), 200

@app.route('/')
def index():
    with lock:
        current_data = latest_data.copy()
    
    response = make_response(
        render_template('index.html', 
                        temperature=current_data.get('temperature', 'N/A'),
                        date=current_data.get('date', 'N/A'),
                        time=current_data.get('time', 'N/A'),
                        error=current_data.get('error'))
    )
    response.headers["Cache-Control"] = "no-store, max-age=0"
    return response

@app.route('/data')
def get_data():
    with lock:
        response_data = latest_data.copy()
    
    response = jsonify(response_data)
    response.headers["Cache-Control"] = "no-store, must-revalidate"
    response.headers["Expires"] = "0"
    return response

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000, debug=False)