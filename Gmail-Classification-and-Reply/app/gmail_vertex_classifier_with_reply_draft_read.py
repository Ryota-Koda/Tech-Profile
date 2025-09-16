# gmail_vertex_classifier_with_reply_draft_read.py

import os
import re
import json
import base64
import email
import textwrap
import traceback
import hashlib  # 追加
from typing import Optional, List, Tuple
from email.mime.text import MIMEText
from email.utils import getaddresses

from gmail_auth import get_gmail_service  # Cloud Run用にTOKEN_PATHを読む実装（Secret推奨）

from google import genai
from google.genai import types
from google.genai.errors import ClientError


# ========= 設定（環境変数で上書き可） =========
def _required_env(name: str) -> str:
    v = os.getenv(name)
    if not v:
        raise RuntimeError(f"Missing required env: {name}")
    return v

def shash(s: str) -> str:
    return hashlib.sha256(s.encode("utf-8", "ignore")).hexdigest()[:8]

def redact_email(addr: str) -> str:
    if not addr or "@" not in addr:
        return "redacted"
    local, domain = addr.split("@", 1)
    masked = (local[0] + "***" + (local[-1] if len(local) > 1 else ""))
    return f"{masked}@{domain}"

PROJECT_ID = _required_env("GOOGLE_CLOUD_PROJECT")

# 既定は asia-northeast1 を先頭に（あなたの環境で提供が確認できたため）
LOCATION   = os.getenv("GEMINI_LOCATION", "us-central1")
MODEL_NAME = os.getenv("GEMINI_MODEL", "gemini-2.0-flash")

# 署名や挙動のトグル
REPLY_SIGNATURE   = os.getenv("REPLY_SIGNATURE", "")
AUTO_MARK_READ    = os.getenv("AUTO_MARK_READ", "1") == "1"
LOG_MODEL_SELECTION = os.getenv("LOG_MODEL_SELECTION", "0") == "1"

# 複数候補（カンマ区切り）。指定がなければ安全な既定値を使用（*-001 は除外）
REGION_CANDIDATES_ENV = os.getenv("GEMINI_LOCATION_CANDIDATES", "")
MODEL_CANDIDATES_ENV  = os.getenv("GEMINI_MODEL_CANDIDATES", "")

def _parse_candidates(raw: str) -> List[str]:
    items = [x.strip() for x in raw.split(",") if x.strip()]
    seen, out = set(), []
    for it in items:
        if it not in seen:
            seen.add(it)
            out.append(it)
    return out

# 既定の順序: asia → us （あなたのログ実績に合わせる）
DEFAULT_REGION_CANDIDATES = [LOCATION] + [r for r in ("us-central1", "asia-northeast1") if r != LOCATION]
DEFAULT_MODEL_CANDIDATES  = [MODEL_NAME] + [m for m in (
    "gemini-1.5-flash",
    "gemini-1.5-flash-8b",
    "gemini-1.5-pro",
) if m != MODEL_NAME]

REGION_CANDIDATES = _parse_candidates(REGION_CANDIDATES_ENV) or DEFAULT_REGION_CANDIDATES
MODEL_CANDIDATES  = _parse_candidates(MODEL_CANDIDATES_ENV)  or DEFAULT_MODEL_CANDIDATES


# ========= genai クライアント =========
def make_client(location: str):
    """指定ロケーションで genai クライアントを作る（ADCを使用）"""
    return genai.Client(vertexai=True, project=PROJECT_ID, location=location)

# 起動時のデフォルトクライアント（最初の候補で）
client = make_client(REGION_CANDIDATES[0])


# ========= ログユーティリティ =========
def jlog(severity: str, **kv):
    # Cloud Loggingで見やすいよう、JSONで吐く
    print(json.dumps({"severity": severity, **kv}, ensure_ascii=False), flush=True)


# ========= Gemini プロンプト =========
SYSTEM_PROMPT = textwrap.dedent("""\
あなたはメール分類アシスタントです。入力されたメールが以下の分類リストからどれに該当するかを判断し，最も適切な番号のみを JSON で返してください。
説明や他の文字を出力しないでください。

必ず次の形式だけを返してください:
{"category": 1|2|3|4}

分類リスト
1. 返信対応が必要なメール 
  (例: 日程の相談、書類の送信、質問に対する回答、承認依頼)
2. 返信対応の必要はないが、重要度が高く、必ず読む必要があるメール
  (例: 社内ルールの改訂、重要度が高いニュース、セキュリティ通知、公式発表)
3. 重要度が中程度で、目を通しておくべきメール
  (例: イベントの案内、定期的なニュース，参考資料)
4. 重要度が低く、読む必要がないメール
  (例: 広告、不審な詐欺、アンケート、SNS通知)

入力例:
件名: "ミーティング日程のご相談"
本文: "来週の空いている日時を教えてください。"
出力:
{"category": 1}

入力例:
件名: "セキュリティポリシー更新のお知らせ"
本文: "本日よりパスワードルールが変更されます。"
出力:
{"category": 2}

入力例:
件名: "学内システム停止のご案内"
本文: "今週末にサーバメンテナンスが行われます。利用できませんのでご注意ください。"
出力:
{"category": 2}

入力例:
件名: "研究セミナー開催のお知らせ"
本文: "来月の学会での公開セミナーの案内です。ご参加は任意です。"
出力:
{"category": 3}

入力例:
件名: "月次ニュースレター"
本文: "今月の研究トピックや最新情報をまとめました。"
出力:
{"category": 3}

入力例:
件名: "期間限定セールのご案内"
本文: "本日限定! 家電が最大50%オフになります。"
出力:
{"category": 4}

""")

REPLY_SYSTEM_PROMPT = textwrap.dedent("""\
あなたは優秀なメール作成アシスタントです。
入力されたメール本文と件名を読み取り、以下のルールに従って適切な返信メールを生成してください。

・件名は生成しない（本文だけを返す）
・メール送信者の名前は，佐藤 雅であり、東北大学 情報科学研究科の博士1年生
・一文ごとに改行、適宜段落分けする
・丁寧な敬語を用いて、ビジネスメールとして自然な表現にする
・文字数は基本的に100〜200文字程度だが、内容に応じて多少前後しても良い
・日本語のメールには日本語で返信文を作成し、日本語署名を付ける
・英語のメールには英語で返信文を作成し、英語署名を付ける

・メールの構成は以下の通りにすること
  1.返信先の名前 
    (件名や本文に相手の名前が含まれない場合は「お世話になっております」で始める） 
  2.簡単な挨拶と自分を名乗る
    (例: お世話になっております。○○大学の○○です。） 
  3.メール本文 (相手の依頼や質問に対する回答)
  4.締めの文章
    (例: どうぞよろしくお願いいたします。） 
  5.固定の署名（以下の署名ブロックを必ずそのまま出力する）

署名 (日本語版)
-----------------------------------------------------------------------
東北大学大学院情報科学研究科 博士1年
高橋研究室 (自然言語処理講座)
佐藤 雅
e-mail masashi.sato.t8@dc.tohoku.ac.jp
-----------------------------------------------------------------------

署名 (英語版)
-----------------------------------------------------
Masashi Sato
Tohoku University
e-mail masashi.sato.t8@dc.tohoku.ac.jp
-----------------------------------------------------

入力例:
  件名: "ミーティング日程のご相談"
  本文:
  佐藤 雅 様
  お世話になっております。
  北海道大学の中辻 孝明です。
  研究の進捗相談を行いたいのですが，来週の空いている日時を教えて頂けますか。

出力例:
  中辻 孝明様

  お世話になっております。
  東北大学大学院 情報科学研究科 博士1年の佐藤 雅です。

  来週は、月曜日の午後と水曜日の午後4時以降が開いております。
  それ以外でも調整可能ですので、ご希望があればお知らせいただけますと幸いです。

  どうぞよろしくお願い致します。

  -----------------------------------------------------------------------
  東北大学大学院情報科学研究科 博士1年
  高橋研究室 (自然言語処理講座)
  佐藤 雅
  e-mail masashi.sato.t8@dc.tohoku.ac.jp
  -----------------------------------------------------------------------
""")

def append_signature(body: str) -> str:
    sig = REPLY_SIGNATURE.strip()
    return f"{body}\n\n{sig}" if sig else body


# ========= Gemini 返り値の安全取り出し =========
def _extract_text(resp) -> str:
    """
    google-genai の返却形式差異に備えてテキストを頑健に取り出す。
    """
    try:
        t = getattr(resp, "text", None)
        if t:
            return t
    except Exception:
        pass

    try:
        # candidates[0].content.parts[0].text パターン
        return resp.candidates[0].content.parts[0].text
    except Exception:
        pass

    try:
        # 万一dict的にアクセスできる実装差分があった場合
        return resp.candidates[0].content.parts[0]["text"]
    except Exception:
        pass

    return ""


# ========= 共通：モデル/リージョンの自動フォールバック =========
def _generate_with_fallback(
    user_text: str,
    system_prompt: str,
    response_mime_type: str,
    temperature: float,
    max_tokens: int,
) -> str:
    """
    指定リージョン/モデルで失敗（特に 404 NOT_FOUND）したら
    候補リージョン／候補モデルへ順にフォールバックして text を返す。
    """
    contents = [{"role": "user", "parts": [{"text": user_text}]}]

    def _try_generate(_client, _model):
        return _client.models.generate_content(
            model=_model,
            contents=contents,
            config=types.GenerateContentConfig(
                system_instruction=system_prompt,
                temperature=temperature,
                max_output_tokens=max_tokens,
                response_mime_type=response_mime_type,
            ),
        )

    last_err = None
    for region in REGION_CANDIDATES:
        _client = client if region == REGION_CANDIDATES[0] else make_client(region)
        for model in MODEL_CANDIDATES:
            try:
                resp = _try_generate(_client, model)
                if LOG_MODEL_SELECTION:
                    jlog("INFO", step="model_used", region=region, model=model)
                return _extract_text(resp)
            except ClientError as ce:
                msg = str(ce)
                # 404/アクセス権なし → 次候補へ
                if "NOT_FOUND" in msg or "not found" in msg.lower() or "does not have access" in msg.lower():
                    jlog("WARN", step="model_try_fail", region=region, model=model, error=msg[:200] + ("…" if len(msg) > 200 else ""))
                    last_err = ce
                    continue
                # その他（レート等）も次候補へ
                jlog("WARN", step="model_try_error", region=region, model=model, error=msg[:200] + ("…" if len(msg) > 200 else ""))
                last_err = ce
                continue
            except Exception as e:
                em = str(e)
                jlog("WARN", step="model_try_exception", region=region, model=model, error=em[:200] + ("…" if len(em) > 200 else ""))
                last_err = e
                continue

    # すべて失敗
    raise last_err or RuntimeError("No model/region worked")


# ========= 分類 =========
def classify_email(text: str) -> int:
    """
    Gemini による分類。JSON文字列を返すよう誘導し、自前でパースする。
    """
    try:
        out = _generate_with_fallback(
            user_text=text,
            system_prompt=SYSTEM_PROMPT,
            response_mime_type="application/json",
            temperature=0.0,
            max_tokens=16,
        )
        jlog("INFO", step="classify_out", raw=(out or "")[:64])  # 生文字列の露出を最小化

        try:
            return int(json.loads(out)["category"])
        except Exception:
            m = re.search(r'"?category"?\D*([1-4])', out or "")
            return int(m.group(1)) if m else 4

    except Exception as e:
        jlog("ERROR", step="classify_exception", error=str(e), tb=traceback.format_exc())
        return 4


# ========= 返信ドラフト生成 =========
def generate_reply(subject: str, body: str) -> str:
    """
    Gemini で返信ドラフトを生成（本文のみ返す）。
    """
    try:
        input_text = f"件名: {subject}\n本文:\n{body}"
        out = _generate_with_fallback(
            user_text=input_text,
            system_prompt=REPLY_SYSTEM_PROMPT,
            response_mime_type="text/plain",
            temperature=0.7,
            max_tokens=256,
        )
        return (out or "").strip()
    except Exception as e:
        jlog("ERROR", step="generate_reply_exception", error=str(e), tb=traceback.format_exc())
        return ""


# ========= Gmailユーティリティ =========
LABEL_MAP = {
    1: "Auto/Important",
    2: "Auto/Read",
    3: "Auto/Event",
    4: "Auto/Ads",
}

def get_or_create_label(service, name):
    labels = service.users().labels().list(userId="me").execute().get("labels", [])
    for lbl in labels:
        if lbl.get("name") == name:
            return lbl["id"]
    body = {"name": name, "labelListVisibility": "labelShow", "messageListVisibility": "show"}
    return service.users().labels().create(userId="me", body=body).execute()["id"]

def fetch_unread(service, max_n=10):
    # 二重処理を避けたい場合は Auto ラベルを除外してもOK
    # q = 'is:unread -label:"Auto/Important" -label:"Auto/Read" -label:"Auto/Event" -label:"Auto/Ads"'
    q = "is:unread"
    res = service.users().messages().list(userId="me", q=q, maxResults=max_n).execute()
    return res.get("messages", [])

def mark_as_read(service, msg_id):
    """既読化（UNREAD ラベルを外す）"""
    service.users().messages().modify(
        userId="me", id=msg_id, body={"removeLabelIds": ["UNREAD"]}
    ).execute()

def _extract_plain_or_html_as_text(msg_obj):
    """text/plain を優先。無ければ text/html をタグ落としでテキスト化"""
    body = ""
    if msg_obj.is_multipart():
        # まず text/plain
        for part in msg_obj.walk():
            if part.get_content_type() == "text/plain":
                body = part.get_payload(decode=True).decode(part.get_content_charset() or "utf-8", "ignore")
                if body:
                    return body
        # 次に text/html
        for part in msg_obj.walk():
            if part.get_content_type() == "text/html":
                html = part.get_payload(decode=True).decode(part.get_content_charset() or "utf-8", "ignore")
                body = re.sub(r"<[^>]+>", "", html)
                return body
        return body
    else:
        payload = msg_obj.get_payload(decode=True)
        return (payload.decode(msg_obj.get_content_charset() or "utf-8", "ignore")) if payload else ""

def msg_to_text(service, msg_id):
    """分類用テキスト（Subject + 本文）"""
    raw_msg = service.users().messages().get(userId="me", id=msg_id, format="raw").execute()
    raw = base64.urlsafe_b64decode(raw_msg["raw"])
    msg_obj = email.message_from_bytes(raw)
    subject = msg_obj.get("Subject") or ""
    body = _extract_plain_or_html_as_text(msg_obj)
    return f"Subject: {subject}\n{body}"

def get_msg_subject_and_body(service, msg_id):
    """返信用に件名と本文のみ取得"""
    raw_msg = service.users().messages().get(userId="me", id=msg_id, format="raw").execute()
    raw = base64.urlsafe_b64decode(raw_msg["raw"])
    msg_obj = email.message_from_bytes(raw)
    subject = msg_obj.get("Subject") or ""
    body = _extract_plain_or_html_as_text(msg_obj)
    return subject, body

def label_message(service, msg_id, label_id):
    service.users().messages().modify(
        userId="me", id=msg_id, body={"addLabelIds": [label_id]}
    ).execute()


# ========= アドレス抽出ユーティリティ =========
def _extract_first_email_address(header_value: str) -> Optional[str]:
    """
    "Name <a@b>" や カンマ区切り複数などを許容して、最初のメールアドレスだけ返す。
    """
    if not header_value:
        return None
    addrs: List[Tuple[str, str]] = getaddresses([header_value])
    for _, addr in addrs:
        if addr and "@" in addr:
            return addr.strip()
    return None


# ========= 下書き作成（新規/スレッド返信） =========
def _ensure_reply_subject(subj: str) -> str:
    s = subj or ""
    if not re.match(r"(?i)^\s*re\s*:", s):
        return f"Re: {s}"
    return s

def create_gmail_draft(service,
                       to_addr_header: str,
                       original_subject: str,
                       reply_body: str,
                       thread_id: Optional[str] = None,
                       in_reply_to: Optional[str] = None,
                       references: Optional[str] = None):
    """
    Gmailの下書きを作成する。可能なら元スレッドにぶら下げる。
    To は From ヘッダから純粋なメールアドレスのみ抽出して設定する。
    """
    # To 抽出（RFC準拠の単一アドレスにする）
    to_addr = _extract_first_email_address(to_addr_header)
    if not to_addr:
        jlog("WARN", step="draft_skip_invalid_to", detail=to_addr_header)
        return None

    subject = _ensure_reply_subject(original_subject)

    # MIME組み立て（本文はUTF-8）
    msg = MIMEText(reply_body, _subtype="plain", _charset="utf-8")
    msg["To"] = to_addr
    msg["Subject"] = subject

    # スレッドにぶら下げるためのヘッダ（threadId があればそれだけでも可だが保険で付与）
    if in_reply_to:
        msg["In-Reply-To"] = in_reply_to
    if references:
        if in_reply_to and in_reply_to not in references:
            msg["References"] = f"{references} {in_reply_to}"
        else:
            msg["References"] = references
    elif in_reply_to:
        msg["References"] = in_reply_to

    raw = base64.urlsafe_b64encode(msg.as_bytes()).decode("utf-8")
    body = {"message": {"raw": raw}}
    if thread_id:
        body["message"]["threadId"] = thread_id

    draft = service.users().drafts().create(userId="me", body=body).execute()
    return draft


# ========= メイン処理 =========
def main():
    jlog("INFO", step="precheck",
         TOKEN_PATH=os.getenv("TOKEN_PATH"),
         project=PROJECT_ID,
         location_candidates=REGION_CANDIDATES,
         model_candidates=MODEL_CANDIDATES)

    # Gmailサービス取得
    try:
        service = get_gmail_service()
    except Exception as e:
        jlog("ERROR", step="get_gmail_service", error=str(e), tb=traceback.format_exc())
        raise

    unread = fetch_unread(service, max_n=10)
    if not unread:
        print("未読メールはありません。")
        return

    for m in unread:
        msg_id = m["id"]

        # 1) 分類
        try:
            text_for_classify = msg_to_text(service, msg_id)
            cat = classify_email(text_for_classify)
        except Exception as e:
            jlog("ERROR", step="classify_fail", msg_id=msg_id, error=str(e), tb=traceback.format_exc())
            print(f"{msg_id} … 分類失敗（{e}）→ Category 4 にフォールバック")
            cat = 4

        # 2) ラベル付け
        try:
            label_id = get_or_create_label(service, LABEL_MAP[cat])
            label_message(service, msg_id, label_id)
            print(f"{msg_id} … Category {cat} → {LABEL_MAP[cat]}")
        except Exception as e:
            jlog("ERROR", step="label_fail", msg_id=msg_id, error=str(e), tb=traceback.format_exc())

        # 3) 重要度=1 の場合は返信ドラフトを作成
        if cat == 1:
            try:
                meta = service.users().messages().get(
                    userId="me", id=msg_id,
                    format="metadata",
                    metadataHeaders=["Subject", "From", "Message-Id", "References"]
                ).execute()
                subj = next((h["value"] for h in meta["payload"]["headers"] if h["name"] == "Subject"), "")
                from_hdr = next((h["value"] for h in meta["payload"]["headers"] if h["name"] == "From"), "")
                msg_id_hdr = next((h["value"] for h in meta["payload"]["headers"] if h["name"] == "Message-Id"), None)
                refs_hdr   = next((h["value"] for h in meta["payload"]["headers"] if h["name"] == "References"), None)
                thread_id  = meta.get("threadId")

                _, body = get_msg_subject_and_body(service, msg_id)

                # 公開版では原文の出力を抑制（個人情報・本文をログ/標準出力しない）
                # print("\n------- Original -------")
                # print("From:", from_hdr)
                # print("Subject:", subj)
                # print((body or "")[:800], "...\n")

                draft_text = generate_reply(subj, body or "")
                draft_text = append_signature(draft_text).strip()

                # 公開版ではAI出力の生文字列を標準出力しない
                # print("------- AI Reply Draft -------")
                # print(draft_text)

                if draft_text.strip():
                    create_gmail_draft(
                        service,
                        to_addr_header=from_hdr,
                        original_subject=subj,
                        reply_body=draft_text,
                        thread_id=thread_id,
                        in_reply_to=msg_id_hdr,
                        references=refs_hdr
                    )
                else:
                    jlog("WARN", step="empty_reply_draft", msg_id=msg_id)
            except Exception as e:
                jlog("ERROR", step="draft_fail", msg_id=msg_id, error=str(e), tb=traceback.format_exc())

        # 4) 既読化
        if AUTO_MARK_READ:
            try:
                mark_as_read(service, msg_id)
            except Exception as e:
                jlog("WARN", step="mark_read_fail", msg_id=msg_id, error=str(e)[:120])


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        jlog("ERROR", step="fatal", error=str(e), tb=traceback.format_exc())
        raise
