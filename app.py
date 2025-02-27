from flask import Flask, render_template, jsonify, make_response
from datetime import datetime
import os
import json
import requests
import threading

app = Flask(__name__)

# Конфигурация GitHub
GITHUB_RAW_URL = "https://raw.githubusercontent.com/dmitray27/esp32-telegram/main/tem.txt"
UPDATE_INTERVAL = 5  # Интервал синхронизации с GitHub (секунды)

# Глобальные переменные для кеширования
latest_data = {"error": "Идет первоначальная загрузка..."}
lock = threading.Lock()

def fetch_from_github():
    global latest_data
    try:
        response = requests.get(GITHUB_RAW_URL)
        response.raise_for_status()
        
        raw_data = response.text.strip()
        data = json.loads(raw_data)
        
        # Валидация данных
        if not all(key in data for key in ('temperature', 'timestamp')):
            raise ValueError("Отсутствуют обязательные поля в JSON")
            
        dt = datetime.fromisoformat(data['timestamp'].replace('Z', '+00:00'))
        
        with lock:
            latest_data = {
                "temperature": data['temperature'],
                "date": dt.strftime("%Y-%m-%d"),
                "time": dt.strftime("%H:%M:%S"),
                "error": None
            }
            
    except Exception as e:
        error_msg = f"{type(e).__name__}: {str(e)}"
        with lock:
            latest_data = {"error": error_msg}

def background_updater():
    while True:
        fetch_from_github()
        threading.Event().wait(UPDATE_INTERVAL)

# Запуск фонового потока
thread = threading.Thread(target=background_updater)
thread.daemon = True
thread.start()

@app.route('/')
def index():
    response = make_response(render_template('index.html', data=latest_data))
    # Заголовки против кеширования
    response.headers["Cache-Control"] = "no-store, max-age=0"
    return response

@app.route('/data')
def data():
    with lock:
        response = jsonify(latest_data)
    # Заголовки для API
    response.headers["Cache-Control"] = "no-store, must-revalidate"
    response.headers["Expires"] = "0"
    return response

if __name__ == '__main__':
    app.run(debug=True)