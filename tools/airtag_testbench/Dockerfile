FROM python:3.11-slim

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    curl \
    && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY crypto_engine.py .
COPY backend_testbench.py .

EXPOSE 8000

CMD ["uvicorn", "backend_testbench:app", "--host", "0.0.0.0", "--port", "8000"]
