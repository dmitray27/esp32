from flask import Flask, render_template, jsonify
from datetime import datetime
import requests
import json
import time

app = Flask(__name__)

# Конфигурация GitHub
GITHUB_REPO = "dmitray27/esp32"
DATA_FILE = "tem.txt"
COMMITS_URL = f"https://api.github.com/repos/{GITHUB_REPO}/commits?path={DATA_FILE}&per_page=1"
RAW_DATA_URL = f"https://raw.githubusercontent.com/{GITHUB_REPO}/main/{DATA_FILE}"

# Состояние последнего коммита
last_commit = {
    'sha': None,
    'timestamp': 0
}

def get_commit_info():
    try:
        response = requests.get(
            COMMITS_URL,
            headers={'Accept': 'application/vnd.github.v3+json'},
            timeout=3
        )
        response.raise_for_status()
        
        commit_data = response.json()[0]
        return {
            'sha': commit_data['sha'],
            'timestamp': datetime.fromisoformat(
                commit_data['commit']['committer']['date'].replace('Z', '+00:00')
            .timestamp()
        }
    except Exception as e:
        app.logger.error(f"Commit check error: {str(e)}")
        return None

def fetch_latest_data():
    try:
        response = requests.get(RAW_DATA_URL, timeout=3)
        response.raise_for_status()
        return response.text.strip()
    except Exception as e:
        raise RuntimeError(f"Data fetch failed: {str(e)}")

@app.route('/check-update')
def check_update():
    global last_commit
    current_commit = get_commit_info()
    
    if not current_commit:
        return jsonify({'update': False, 'error': 'Commit check failed'})
    
    if current_commit['sha'] != last_commit['sha']:
        last_commit.update(current_commit)
        return jsonify({'update': True})
    
    return jsonify({'update': False})

@app.route('/get-data')
def get_data():
    try:
        raw_data = fetch_latest_data()
        data = json.loads(raw_data)
        
        dt = datetime.fromisoformat(data['timestamp'].replace('Z', '+00:00'))
        return jsonify({
            'temperature': data['temperature'],
            'date': dt.strftime("%Y-%m-%d"),
            'time': dt.strftime("%H:%M:%S"),
            'error': None
        })
    except Exception as e:
        return jsonify({'error': str(e)})

@app.after_request
def disable_caching(response):
    response.headers["Cache-Control"] = "no-store, max-age=0"
    response.headers["Pragma"] = "no-cache"
    return response

@app.route('/')
def index():
    return render_template('index.html')

if __name__ == '__main__':
    # Инициализация при запуске
    initial_commit = get_commit_info()
    if initial_commit:
        last_commit.update(initial_commit)
    app.run(host='0.0.0.0', port=5000, debug=False)