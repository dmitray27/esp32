from flask import Flask, render_template, jsonify
from datetime import datetime, timezone
import requests
import json
import logging
import threading

app = Flask(__name__)

# Настройка логирования
app.logger.setLevel(logging.DEBUG)  # Включить отладочный режим
handler = logging.StreamHandler()
handler.setFormatter(logging.Formatter('[%(asctime)s] %(levelname)s: %(message)s'))
app.logger.addHandler(handler)

# Конфигурация
GITHUB_RAW_URL = "https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt"
UPDATE_INTERVAL = 60  # Уменьшить интервал для тестов

latest_data = {
    "temperature": "N/A",
    "date": "N/A",
    "time": "N/A",
    "error": "Идет первоначальная загрузка..."
}
lock = threading.Lock()

def fetch_from_github():
    global latest_data
    try:
        app.logger.debug("Запрос к GitHub...")
        response = requests.get(GITHUB_RAW_URL, timeout=10)
        response.raise_for_status()
        
        data = json.loads(response.text)
        app.logger.debug(f"Данные получены: {data}")
        
        dt = datetime.fromisoformat(data['timestamp']).astimezone(timezone.utc)
        
        with lock:
            latest_data.update({
                "temperature": data["temperature"],
                "date": dt.strftime("%Y-%m-%d"),
                "time": dt.strftime("%H:%M:%S"),
                "error": None
            })
            
    except Exception as e:
        error_msg = f"Ошибка: {type(e).__name__} - {str(e)}"
        app.logger.error(error_msg)
        with lock:
            latest_data["error"] = error_msg

def background_updater():
    while True:
        fetch_from_github()
        threading.Event().wait(UPDATE_INTERVAL)

# Запуск фонового потока
thread = threading.Thread(target=background_updater)
thread.daemon = True
thread.start()

@app.route("/data")
def get_data():
    with lock:
        return jsonify(latest_data)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000)