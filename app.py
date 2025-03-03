from flask import Flask, render_template
from datetime import datetime
import requests
import json
import os
import logging  # Добавлено для логирования

app = Flask(__name__)
GITHUB_URL = "https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt"

# Настройка логирования
logging.basicConfig(level=logging.INFO)

def fetch_github_data():
    try:
        # Добавляем параметр для обхода кэша GitHub
        timestamp = int(datetime.now().timestamp())
        url = f"{GITHUB_URL}?nocache={timestamp}"
        
        response = requests.get(
            url, 
            timeout=5,  # Увеличен таймаут
            headers={'Cache-Control': 'no-cache'}
        )
        response.raise_for_status()
        return response.text.strip()
    except requests.RequestException as e:
        logging.error(f"Ошибка запроса: {str(e)}")
        raise Exception(f"Не удалось получить данные")

def parse_sensor_data(raw_data):
    try:
        data = json.loads(raw_data)
        dt = datetime.fromisoformat(data['timestamp'].replace('Z', '+00:00'))
        return {
            'temperature': data['temperature'],
            'date': dt.strftime("%Y-%m-%d"),
            'time': dt.strftime("%H:%M:%S"),
            'error': None
        }
    except (KeyError, json.JSONDecodeError) as e:
        raise ValueError("Некорректный JSON")
    except Exception as e:
        raise ValueError(f"Ошибка: {str(e)}")

@app.after_request
def disable_caching(response):
    response.headers["Cache-Control"] = "no-store, max-age=0"
    return response

@app.route('/')
def index():
    sensor_data = {'error': None}
    try:
        raw_data = fetch_github_data()
        sensor_data = parse_sensor_data(raw_data)
    except Exception as e:
        sensor_data['error'] = str(e)
        logging.error(f"Ошибка: {e}")
    
    return render_template('index.html', data=sensor_data)

if __name__ == '__main__':
    port = int(os.environ.get("PORT", 10000))  # Исправлено: 10000 вместо 5000
    app.run(host='0.0.0.0', port=port, debug=False)