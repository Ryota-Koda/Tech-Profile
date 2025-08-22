## メール分類返信アプリの実装方法

* GCPでプロジェクトを作成
  * プロジェクト名は任意，IDは後で利用するので覚えておく
* Gmail APIを有効化
* OAuth同意画面を設定
  * スコープ
  * OAuthクライアントIDの作成
    * デスクトップアプリ
    * client_secret.jsonを保存
  * 対象
    * 公開ステータスをテスト中に設定
    * テストユーザに分類したいユーザのメールアドレスを入力
* create_token.pyをローカルで実行
  * client_secret.jsonを入力し，テストユーザがログインして承認すると，ユーザ専用のtoken.jsonを生成
  * そのtoken.jsonを使ってGmail APIを利用すると，その人のGmailを操作可能
* Cloud Storageでバケットを作成し，token.jsonをアップロード
  * バケット名は任意，後で利用するので覚えておく
* VertexAI APIを有効化
* Cloud Run Jobが実行時に使うサービスアカウントを作成
  * IAMと管理からサービスアカウントに移動して作成
  * サービスアカウントアカウント名は任意，後で利用
  * 作成したサービスアカウントに以下の権限を付与 
    * Cloud Run ジョブ エグゼキュータ
    * Service Usage ユーザ
    * VertexAI ユーザ
    * サービスアカウントトークン作成者
* Cloud Storageでtoken.jsonを保存したバケットに移動
  * 権限を開き，先ほど作成したサービスアカウントにStorage オブジェクト管理者を付与
* 実行用コンテナの用意
  * Artifact Registry APIを有効化
  * レポジトリの作成
    * リポジトリ名は任意，後で利用   
    * Docker
    * リージョン us-central1
* Cloud Build APiを有効化
* Cloud shellを起動
  * bulid用のディレクトリを作成
    * ディレクトリ名は任意  
  * 作成したディレクトリに../appの4つのファイルをアップロード
    * Dockerfile
    * gmail_auth.py
    * gmail_vertex_classifier_with_reply_read.py
    * requirements.txt
  * その後，作成したディレクトリに移動し，以下のコマンドを実行
    * REGION=us-central1
    * REPO=(先ほど作成したリポジトリの名前)
    * IMAGE=$REGION-docker.pkg.dev/$(gcloud config get-value project)/$REPO/mailbot:latest
    * gcloud builds submit --tag $IMAGE
* Cloud Run Jobでジョブを作成
  * ジョブ名は任意
  * コンテナイメージは先ほど作成した，~/mailbot:latestに設定
  * サービスアカウントは先ほど作成したものを設定
  * 環境変数
    * GEMINI_MODEL　gemini-2.0-flash
    * GOOGLE_CLOUD_PROJECT　gmail-classification-and-reply (初めに作成したプロジェクトID)
    * TOKEN_PATH　/mnt/gcs/token.json
  * ボリュームの作成
    * Cloud Storage バケット
    * ボリューム名は任意
    * token.jsonが入っているバケットを選択
  * ボリュームのマウント
    * 作ったボリュームを選択
    * マウントパス /mnt/gcs
* 作成後，ジョブを動かすと動作する
  * 以下 10分おきに自動実行するための実装
* Cloud Scheduler APIを有効化
* スケジュールを作成
  * リージョン us-central1
  * 頻度 */10 * * * * (10分おき)
  * ターゲットタイプ HTTP
  * URL https://us-central1-run.googleapis.com/apis/run.googleapis.com/v1/namespaces/gmail-classification-and-reply/jobs/mailbot:run
    * gmail-classification-and-replyの部分にプロジェクトIDを入力
  * HTTPメソッド POST
  * Authヘッダー OAuthトークンを追加
    * サービスアカウント 作成したサービスアカウントを選択
    * 範囲 https://www.googleapis.com/auth/cloud-platform
* 作成したスケジュールを実行することで，10分に1回動作する
