from flask import Flask, render_template
from datetime import datetime
import os
import json
import requests  # Добавляем импорт библиотеки для HTTP-запросов

app = Flask(__name__)

# URL сырого файла в репозитории GitHub
GITHUB_URL = "https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt"

def get_sensor_data():
    try:
        # Загружаем файл с GitHub
        response = requests.get(GITHUB_URL, timeout=5)
        
        # Проверяем статус ответа
        if response.status_code != 200:
            return {"error": f"Ошибка загрузки файла. Код: {response.status_code}"}
            
        raw_data = response.text.strip()

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

    except requests.exceptions.RequestException as e:
        return {"error": f"Ошибка соединения: {str(e)}"}
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