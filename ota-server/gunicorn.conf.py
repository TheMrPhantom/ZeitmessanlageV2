import os


bind = os.environ.get("GUNICORN_BIND", "0.0.0.0:8000")
# Keep a single process so the publication lock covers every request. Threads can
# continue serving the previous binary while an update downloads from GitHub.
workers = 1
threads = int(os.environ.get("GUNICORN_THREADS", "4"))
worker_class = "gthread"
timeout = int(os.environ.get("GUNICORN_TIMEOUT", "180"))
graceful_timeout = 30
keepalive = 5
accesslog = "-"
errorlog = "-"
capture_output = True
