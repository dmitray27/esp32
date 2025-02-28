from flask import Flask, jsonify, request
import requests
import hashlib
import time

app = Flask(__name__)
if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
GITHUB_API_URL = "https://api.github.com/repos/dmitray27/esp32/contents/tem.txt"
CACHE = {"etag": "", "data": None}

@app.route('/data')
def get_data():
    etag = hashlib.md5(str(time.time()).encode()).hexdigest()
    headers = {"Accept": "application/vnd.github.v3.raw", "If-None-Match": etag}
    
    try:
        response = requests.get(GITHUB_API_URL, headers=headers, timeout=5)
        if response.status_code == 304:
            return jsonify(CACHE["data"])
        
        data = response.json()
        CACHE.update({
            "etag": response.headers.get('ETag', ''),
            "data": {
                "temperature": data["temperature"],
                "timestamp": data["timestamp"],
                "version": hashlib.md5(response.text.encode()).hexdigest()[:6]
            }
        })
        return jsonify(CACHE["data"])
    
    except Exception as e:
        return jsonify({"error": str(e)}), 500

# ... остальные маршруты ...
