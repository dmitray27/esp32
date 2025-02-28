from flask import Flask, jsonify, render_template_string
import requests

app = Flask(__name__)

HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<body>
    <h1>Температура: <span id="temp">-</span>°C</h1>
    <script>
        fetch('/data')
            .then(r => r.json())
            .then(data => {
                document.getElementById('temp').textContent = data.temperature || 'Ошибка';
            });
    </script>
</body>
</html>
"""

@app.route('/')
def home():
    return render_template_string(HTML_TEMPLATE)

@app.route('/data')
def get_data():
    try:
        url = "https://raw.githubusercontent.com/dmitray27/esp32/main/tem.txt"
        data = requests.get(url).json()
        return jsonify({"temperature": data["temperature"]})
    except:
        return jsonify({"temperature": "Данные недоступны"})

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=10000)
