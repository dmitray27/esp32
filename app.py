from flask import Flask, jsonify, render_template
import requests
import os

app = Flask(__name__)

GITHUB_RAW_URL = "https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt"

def get_greeting():
    try:
        response = requests.get(GITHUB_RAW_URL)
        return response.text.strip()
    except Exception as e:
        return f"Ошибка: {str(e)}"

@app.route('/')
def index():
    greeting = get_greeting()
    return render_template('index.html', greeting=greeting)

@app.route('/health')
def health_check():
    return jsonify({"status": "ok"}), 200

if __name__ == '__main__':
    port = int(os.environ.get("PORT", 10000))
    app.run(host='0.0.0.0', port=port)
