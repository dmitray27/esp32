# app.py
from flask import Flask, render_template
from datetime import datetime
import os
import json

app = Flask(__name__)

DATA_FILE = os.path.join(os.path.dirname(__file__), 'tem.txt')

def get_sensor_data():
    try:
        with open(DATA_FILE, 'r') as f:
            raw_data = f.read().strip()
            
            # Парсим JSON-данные
            data = json.loads(raw_data)
            
            # Извлекаем значения
            temperature = data.get('temperature')
            timestamp = data.get('timestamp')
            
            # Проверяем наличие ключей
            if not temperature or not timestamp:
                return {"error": "Некорректная структура JSON"}
                
            # Парсим временную метку
            dt = datetime.fromisoformat(timestamp.replace('Z', '+00:00'))
            
            return {
                "temperature": temperature,
                "date": dt.strftime("%Y-%m-%d"),
                "time": dt.strftime("%H:%M:%S"),
                "error": None
            }
            
    except FileNotFoundError:
        return {"error": "Файл tem.txt не найден"}
    except json.JSONDecodeError:
        return {"error": "Ошибка декодирования JSON"}
    except ValueError as e:
        return {"error": f"Ошибка формата времени: {str(e)}"}
    except Exception as e:
        return {"error": f"Неизвестная ошибка: {str(e)}"}

@app.route('/')
def index():
    sensor_data = get_sensor_data()
    return render_template('index.html', data=sensor_data)

if __name__ == '__main__':
    app.run(debug=True)
