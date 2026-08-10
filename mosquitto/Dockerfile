# Minimal image for the HA decode bridge (ha-bridge.py).
FROM python:3.12-slim

# Flush stdout/stderr so `docker compose logs` shows output live.
ENV PYTHONUNBUFFERED=1

RUN pip install --no-cache-dir paho-mqtt msgpack

WORKDIR /app
COPY ha-bridge.py /app/ha-bridge.py

ENTRYPOINT ["python", "/app/ha-bridge.py"]
