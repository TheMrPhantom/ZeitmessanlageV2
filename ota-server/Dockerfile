FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1

WORKDIR /app

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt \
    && addgroup --system ota \
    && adduser --system --ingroup ota --home /nonexistent ota \
    && mkdir -p /data \
    && chown ota:ota /data

COPY ota_service ./ota_service
COPY gunicorn.conf.py wsgi.py ./

USER ota
EXPOSE 8000

CMD ["gunicorn", "--config", "gunicorn.conf.py", "wsgi:app"]
