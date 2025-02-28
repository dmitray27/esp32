from flask import Flask, jsonify
import requests

app = Flask(__name__)

@app.route('/')
def home():
    return """
    <html>
        <body>
            <h1>Температура: <span id="temp">-</span>°C</h1>
            <script>
                fetch('/data')
                    .then(r => r.json())
                    .then(data => {
                        document.getElementById('temp').textContent = data.temperature || 'ERROR';
                    });
            </script>
        </body>
    </html>
    """

@app.route('/data')
def get_data():
    try:
        response = requests.get("https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt")
        return jsonify(response.json())
    except:
        return jsonify({"temperature": "Данные не загружены"})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000)
