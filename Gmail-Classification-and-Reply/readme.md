# Gmail Classification and Reply

Google Cloud Consorl (GCP)を用いて，Gmailの分類と返信の作成を自動で行うプログラム．

注意：このプログラムでは，メール本文をクラウド上のLLM(Gemini) に投げて分類しています．機密情報の取り扱いに注意してください．

実運用を想定する場合は，GPT-ossやllamaなどのローカルで動作するLLMを用いることを推奨します．

## 本リポジトリの構成
- HowtoUse
  - メール分類返信プログラムの実装方法を説明
- app
  - Dockerfile
  - requirements.txt
  - gmail_auth.py
  - gmail_vertex_classifier_with_reply_draft_read.py
- create_token.ipynb   

## 機能概要
* 10分おきに最新の未読メール10件を4種類の重要度に分類
  1. 非常に重要で，返信対応が必要なメール
  2. 返信対応の必要がないが，必ず読む必要があるメール
  3. イベントの開催など，目を投資ておくべきメール
  4. 特に自分に関係ない，イベントの開催や広告のメール
* 分類されたメールは、gmailのラベル機能でAuto/Importantのようなラベルを付与し，既読化
* 重要度1のメールは自動で返信文を生成し，下書きに保存
  * 敬語を使い100-200字程度
 
分類の判断基準や返信文の調整などは，gmail_vertex_classifier_with_reply_draft_read.pyにあるLLMへの命令文を変更することで調整可能
メールの自動送信や，既読化の削除などは各自調整してください


https://github.com/user-attachments/assets/69fba6f1-9db2-4b73-a071-7b87c5f9f5da

