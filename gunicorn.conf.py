# gunicorn.conf.py

# Основные параметры
workers = 4
worker_class = "gevent"
bind = "0.0.0.0:10000"  # Порт Render по умолчанию
timeout = 160  # Увеличиваем таймаут до 60 секунд

# Логирование
capture_output = True  # Перехват логов из stdout/stderr
loglevel = "debug"     # Уровень детализации логов (debug, info, warning, error)
accesslog = "-"        # Логировать запросы в консоль
errorlog = "-"         # Логировать ошибки в консоль