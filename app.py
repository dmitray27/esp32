from flask import Flask, render_template, jsonify
import requests
import hashlib
import base64
import json

app = Flask(__name__)

GITHUB_API_URL = "https://api.github.com/repos/dmitray27/esp32/contents/tem.txt"
CACHE = {"etag": "", "data": None}

@app.route('/')
def index():
    return render_template('index.html', data=CACHE["data"])

@app.route('/data')
def get_data():
    headers = {"Accept": "application/vnd.github.v3.raw", "If-None-Match": CACHE["etag"]}

    try:
        response = requests.get(GITHUB_API_URL, headers=headers, timeout=5)
        if response.status_code == 304:
            return jsonify(CACHE["data"])

        # Декодируем содержимое файла из Base64
        data = response.json()
        content = base64.b64decode(data["content"]).decode("utf-8")
        file_data = json.loads(content)  # Парсим JSON из tem.txt

        # Обновляем кэш
        CACHE.update({
            "etag": response.headers.get('ETag', ''),
            "data": {
                "temperature": file_data["temperature"],
                "timestamp": file_data["timestamp"],
                "version": hashlib.md5(response.text.encode()).hexdigest()[:6]
            }
        })
        return jsonify(CACHE["data"])

    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)