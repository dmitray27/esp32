from flask import Flask, render_template, jsonify, make_response
from datetime import datetime
import requests
import json
import time

app = Flask(__name__)

GITHUB_RAW_URL = "https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt?cache_buster="

def fetch_data():
    try:
        response = requests.get(f"{GITHUB_RAW_URL}?t={int(time.time())}")
        response.raise_for_status()
        data = json.loads(response.text)

        # Исправляем формат часового пояса
        timestamp = data['timestamp'].replace("+0300", "+03:00")
        
        try:
            # Пытаемся распарсить timestamp из файла
            dt = datetime.fromisoformat(timestamp)
        except ValueError:
            # Если формат неверный - используем текущее время и логируем ошибку
            app.logger.error(f"Неверный формат времени: {timestamp}")
            dt = datetime.now(timezone.utc)

        return {
            "temperature": f"{data['temperature']:.1f}",
            "time": dt.strftime("%H:%M:%S"),
            "date": dt.strftime("%d.%m.%Y"),
            "error": None
        }
    except Exception as e:
        return {
            "temperature": "N/A",
            "time": "N/A", 
            "date": "N/A",
            "error": f"Ошибка: {str(e)}"
        }

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/health')
def health_check():
    return jsonify({"status": "ok", "version": "1.0"}), 200

@app.route('/data')
def get_data():
    data = fetch_data()
    response = make_response(jsonify(data))
    response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000)
