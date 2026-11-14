# 相互認証テストの証明書

このディレクトリの秘密鍵はテスト専用です。  
製品や運用環境で使用しないでください。  
すべて ECDSA P-256 の鍵です。

`python3 generate_certificates.py` をこのディレクトリで実行すると、システムの `openssl` コマンドで PEM ファイルを生成します。  
鍵と署名は実行ごとに生成します。  
証明書の有効期間と名前、発行関係を再現します。

- `ca.pem` と `other_ca.pem`: 独立した CA。有効期間は 2020 年から 2120 年です。
- `client.pem`: `ca.pem` が発行した証明書。SAN の DNS 名は `porter.test` です。
- `server.pem`: `ca.pem` が発行した証明書。SAN の URI は `urn:porter:peer` です。
- `foreign.pem`: `other_ca.pem` が発行した証明書です。
- `expired.pem`: 有効期間が 2000 年から 2001 年の証明書です。
- `revoked.pem`: `crl.pem` に失効を登録した証明書です。
- `unregistered.pem`: 直接登録の不一致を確認する証明書です。
- `same_key.pem`: `client.pem` と同じ公開鍵と subject を持ち、証明書全体は異なる証明書です。
- `wildcard.pem`: SAN の DNS 名が `*.porter.test` の証明書です。
- `common_name.pem`: CN が `porter.test` で、SAN がない証明書です。
- `*-key.pem`: 対応する証明書のテスト専用の秘密鍵です。
- `crl.pem`: `ca.pem` の失効リスト。nextUpdate は生成日の 36500 日後です。

`expired.pem` 以外の証明書の有効期間は 2020 年から 2120 年です。  
CA の秘密鍵、証明書要求、発行データベースは生成処理の一時ディレクトリにだけ作成し、終了時に削除します。
