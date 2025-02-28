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

        # Парсинг времени
        dt = datetime.fromisoformat(data['timestamp'].replace('Z', '+00:00')).astimezone()

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
        with lock:
            latest_data['error'] = error_msg

    except Exception as e:
        error_msg = f"{type(e).__name__}: {str(e)}"
        app.logger.error(f"GitHub sync failed: {error_msg}")
        with lock:
            latest_data['error'] = error_msg

def background_updater():
    app.logger.info("Background updater started")
    while True:
        try:
            fetch_from_github()
        except Exception as e:
            app.logger.error(f"Critical error in updater: {str(e)}")
        finally:
            threading.Event().wait(UPDATE_INTERVAL)

# Запуск потока только в главном процессе Gunicorn
if (not IS_GUNICORN) or (os.environ.get("WERKZEUG_RUN_MAIN") == "true"):
    app.logger.info("Initializing background thread...")
    thread = threading.Thread(target=background_updater)
    thread.daemon = True
    thread.start()
    app.logger.info(f"Thread started: {thread.ident}")

@app.route("/health")
def health_check():
    return jsonify({
        "status": "OK",
        "timestamp": datetime.utcnow().isoformat(),
        "data_age": (datetime.utcnow() - datetime.fromisoformat(latest_data.get('timestamp', datetime.min.isoformat()))).total_seconds()
    }), 200

@app.route('/')
def index():
    with lock:
        current_data = latest_data.copy()
    
    # Дополнительная проверка данных
    if not current_data.get('temperature'):
        current_data['temperature'] = 'N/A'
    
    app.logger.debug(f"Rendering template with data: {current_data}")
    return render_template('index.html', data=current_data)

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