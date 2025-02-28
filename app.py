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
    headers = {
        "Accept": "application/vnd.github.v3+json",
        "If-None-Match": CACHE["etag"]
    }
    try:
        response = requests.get(GITHUB_API_URL, headers=headers, timeout=5)
        if response.status_code == 304:
            return jsonify(CACHE["data"])
        if response.status_code != 200:
            raise Exception(f"GitHub API error: {response.status_code}")
        content = base64.b64decode(response.json()["content"]).decode("utf-8")
        file_data = json.loads(content)
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

# Важно: эта функция должна быть без лишних отступов!
@app.after_request
def add_cors_headers(response):
    response.headers['Access-Control-Allow-Origin'] = '*'
    return response

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)