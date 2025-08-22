FROM python:3.11-slim

# 依存ツール（最小）
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 依存インストール
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# ソース
COPY gmail_auth.py .
COPY gmail_vertex_classifier_with_reply_draft_read.py .

# 実行
CMD ["python", "gmail_vertex_classifier_with_reply_draft_read.py"]
