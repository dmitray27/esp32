from flask import Flask, render_template, jsonify, request
from datetime import datetime
import requests
import json
import hmac
import hashlib

app = Flask(__name__)

# Конфигурация GitHub
REPO_OWNER = "dmitray27"
REPO_NAME = "esp32"
BRANCH = "main"  # Укажите нужную ветку
WEBHOOK_SECRET = "your_webhook_secret"  # Замените на свой секрет

# GitHub API Endpoints
COMMITS_URL = f"https://api.github.com/repos/{REPO_OWNER}/{REPO_NAME}/commits"
FILE_URL = f"https://raw.githubusercontent.com/{REPO_OWNER}/{REPO_NAME}/{BRANCH}/tem.txt"

# Хранилище последних данных
last_data = {
    'sha': None,
    'content': None,
    'timestamp': None
}

def verify_signature(payload, signature):
    digest = hmac.new(WEBHOOK_SECRET.encode(), payload, hashlib.sha256).hexdigest()
    return hmac.compare_digest(f'sha256={digest}', signature)

@app.route('/webhook', methods=['POST'])
def github_webhook():
    signature = request.headers.get('X-Hub-Signature-256', '')
    payload = request.data

    if not verify_signature(payload, signature):
        return jsonify({'status': 'invalid signature'}), 403

    event = request.headers.get('X-GitHub-Event')
    
    if event == 'push':
        commits = request.json.get('commits', [])
        for commit in commits:
            if 'tem.txt' in commit.get('modified', []):
                update_file_content()
                return jsonify({'status': 'update triggered'})
    
    return jsonify({'status': 'ignored'})

def update_file_content():
    try:
        # Получаем информацию о последнем коммите
        commits_response = requests.get(
            COMMITS_URL,
            params={'path': 'tem.txt', 'per_page': 1},
            headers={'Accept': 'application/vnd.github.v3+json'}
        )
        commits_response.raise_for_status()
        
        commit_data = commits_response.json()[0]
        sha = commit_data['sha']
        
        # Получаем содержимое файла
        file_response = requests.get(FILE_URL)
        file_response.raise_for_status()
        
        last_data.update({
            'sha': sha,
            'content': file_response.text.strip(),
            'timestamp': datetime.utcnow().isoformat()
        })
        
    except Exception as e:
        app.logger.error(f"Update error: {str(e)}")

def parse_sensor_data():
    try:
        data = json.loads(last_data['content'])
        dt = datetime.fromisoformat(data['timestamp'].replace('Z', '+00:00'))
        
        return {
            'temperature': data['temperature'],
            'date': dt.strftime("%Y-%m-%d"),
            'time': dt.strftime("%H:%M:%S"),
            'sha': last_data['sha'],
            'error': None
        }
    except Exception as e:
        return {'error': str(e)}

@app.route('/data')
def get_data():
    return jsonify(parse_sensor_data())

@app.route('/')
def index():
    return render_template('index.html')

if __name__ == '__main__':
    update_file_content()  # Инициализация при запуске
    app.run(host='0.0.0.0', port=5000)