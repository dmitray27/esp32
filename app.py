from flask import Flask, render_template, jsonify, make_response
from datetime import datetime, timezone
import requests
import json
import time
import logging

app = Flask(__name__)

# Настройка логирования
app.logger.setLevel(logging.DEBUG)
handler = logging.StreamHandler()
handler.setFormatter(logging.Formatter('[%(asctime)s] %(levelname)s: %(message)s'))
app.logger.addHandler(handler)

GITHUB_RAW_URL = "https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt"

def fetch_data():
    try:
        # Запрос с временной меткой для обхода кэша
        response = requests.get(f"{GITHUB_RAW_URL}?t={int(time.time())}", timeout=10)
        response.raise_for_status()
        data = json.loads(response.text)

        # Обработка и валидация времени
        raw_timestamp = data.get('timestamp', '')
        timestamp = raw_timestamp.replace("+0300", "+03:00")

        try:
            dt = datetime.fromisoformat(timestamp).astimezone(timezone.utc)
        except ValueError:
            app.logger.error(f"Invalid timestamp format: {raw_timestamp}")
            dt = datetime.now(timezone.utc)

        return {
            "temperature": f"{data.get('temperature', 'N/A'):.1f}",
            "time": dt.strftime("%H:%M:%S"),
            "date": dt.strftime("%d.%m.%Y"),
            "error": None
        }
    except Exception as e:
        app.logger.error(f"Data fetch error: {str(e)}")
        return {
            "temperature": "N/A",
            "time": "N/A",
            "date": "N/A",
            "error": f"Ошибка: {str(e)}"
        }

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/data')
def get_data():
    data = fetch_data()
    response = make_response(jsonify(data))
    # Заголовки против кэширования
    response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response

@app.route('/health')
def health_check():
    return jsonify({
        "status": "ok",
        "version": "1.0",
        "timestamp": datetime.now().isoformat()
    }), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000)
