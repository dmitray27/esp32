from flask import Flask, render_template
import requests

app = Flask(__name__)

# URL к файлу tem.txt в вашем репозитории
GITHUB_RAW_URL = "https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt"

def get_greeting():
    try:
        response = requests.get(GITHUB_RAW_URL)
        response.raise_for_status()  # Проверка на ошибки HTTP
        return response.text.strip()
    except Exception as e:
        return f"Ошибка загрузки: {str(e)}"

@app.route('/')
def index():
    greeting = get_greeting()
    return render_template('index.html', greeting=greeting)

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000)
