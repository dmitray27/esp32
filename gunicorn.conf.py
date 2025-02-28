# gunicorn.conf.py
workers = 2
worker_class = "gevent"
bind = "0.0.0.0:10000"
timeout = 160
keepalive = 75
max_requests = 1000
capture_output = True
loglevel = "info"
accesslog = "-"
errorlog = "-"
proxy_protocol = True
forwarded_allow_ips = "*"