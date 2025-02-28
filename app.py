<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Монитор температуры</title>
    <style>
        body {
            font-family: 'Arial', sans-serif;
            background: #f5f7fa;
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            margin: 0;
        }

        .container {
            background: white;
            padding: 2rem;
            border-radius: 16px;
            box-shadow: 0 4px 12px rgba(0,0,0,0.1);
            width: 90%;
            max-width: 400px;
            text-align: center;
        }

        h1 {
            color: #4a5568;
            margin-bottom: 1.5rem;
            font-size: 1.8rem;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 0.5rem;
        }

        .temperature {
            font-size: 6rem;
            color: #2c3e50;
            font-weight: 300;
            margin: 1rem 0;
            line-height: 1;
        }

        .meta-info {
            display: grid;
            gap: 1rem;
            margin-top: 2rem;
            color: #718096;
        }

        .meta-item {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 0.5rem;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>
            <span>🌡️</span>
            Температура
        </h1>

        {% if data.error %}
            <div class="error">{{ data.error }}</div>
        {% else %}
            <div class="temperature">{{ data.temperature }}°C</div>

            <div class="meta-info">
                <div class="meta-item">
                    <span>📅</span>
                    {{ data.date }}
                </div>
                <div class="meta-item">
                    <span>🕒</span>
                    {{ data.time }}
                </div>
            </div>
        {% endif %}
    </div>
</body>
</html>