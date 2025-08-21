# gmail_auth.py  (Cloud Run 専用)
import os
from google.oauth2.credentials import Credentials
from googleapiclient.discovery import build

SCOPES = [
    "https://www.googleapis.com/auth/gmail.modify",
    "https://www.googleapis.com/auth/gmail.compose",
    "https://www.googleapis.com/auth/gmail.send",
    "https://www.googleapis.com/auth/gmail.readonly",
]

def get_gmail_service():
    token_path = os.getenv("TOKEN_PATH", "token.json")  # 例: /mnt/gcs/token.json
    if not os.path.exists(token_path):
        raise FileNotFoundError(f"token.json not found at {token_path}")
    creds = Credentials.from_authorized_user_file(token_path, scopes=SCOPES)
    # NOTE: Cloud Run では自動リフレッシュに refresh_token が必須です
    return build("gmail", "v1", credentials=creds, cache_discovery=False)