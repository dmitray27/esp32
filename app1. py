from flask import Flask, render_template, jsonify, make_response
from datetime import datetime
import requests
import json
import time

app = Flask(__name__)

GITHUB_RAW_URL = "https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt?t="

def fetch_data():
    try:
        # Добавляем timestamp для обхода кэша
        response = requests.get(f"{GITHUB_RAW_URL}{int(time.time())}")
        response.raise_for_status()
        data = json.loads(response.text)
        
        # Конвертация времени
        dt = datetime.fromisoformat(data['timestamp'].replace("+0300", "+03:00"))
        return {
            "temperature": data["temperature"],
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

@app.route('/data')
def get_data():
    data = fetch_data()
    response = make_response(jsonify(data))
    # Запрет кэширования
    response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000)
