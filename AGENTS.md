# AGENTS.md

## 重要事項

- 自動ステージング、コミット禁止。指示があるまでステージング、コミットは行わないこと。
- 思考の断片は英語でもよいが、ユーザーに気づきを与えたり報告する際は日本語を用いること。

## リポジトリ概要

UDP/IP および TCP/IP をサポートするクロスプラットフォーム (Linux / Windows) 通信ライブラリです。ユニキャスト・マルチキャスト・ブロードキャストの UDP 通信、TCP による接続型通信、スライディング ウィンドウによる NACK ベース再送制御、ヘルスチェック、データ圧縮、マルチパスを提供します。

## 作業時の入口

- `makefile` - ルートの入口
- `prod/include/` - 公開 API (ライブラリ利用者向けヘッダー)。関数宣言は `porter_spec.h` に集約
- `prod/include_internal/` - ライブラリ内部共有ヘッダー
- `prod/libsrc/` - ソース ファイル (`.c`)
- `prod/src/cmd/porter-test/` - 動作確認用コマンド
- `test/` - テスト本体、モック、`makepart.mk`
- `docs/` - 発行ドキュメントの目次、個別トピックの解説

## 主要コマンド

```bash
make
make test
```

## 注意点

- 公開 API (`prod/include/` 配下のヘッダー) に関数を追加、削除、またはシグネチャを変更した場合は、`test/src/libporter/exportTest/exportTest.cc` の `POTR_EXPORT_FUNCTION_TABLE` を同じコミットで見直すこと。公開変数を追加、削除、または型を変更した場合は、同ファイルの `POTR_EXPORT_VARIABLE_TABLE` も見直すこと。反映を怠ると `exportTest.symbol_names_match` が失敗する。
- 上位の `docs/general/coding-guideline.md` は一般則のみを扱う。porter 固有の規則、制限、遵守事項 (結果コード `POTR_OK` / `POTR_ERR_*`、適用対象外の範囲など) はすべて `docs/coding-guideline.md` に集約する。追加・変更時も同ファイルへ追記すること。
- 公開 API (`prod/include/` 配下のヘッダー) を変更した場合は、`docs/api.md` の該当記載 (戻り値表、スレッド セーフ表) を同じコミットで見直すこと。
- `docs/doxybook2_public/` と `docs/doxybook2_internal/` は Doxygen からの自動生成物であり、手編集しないこと。ヘッダーの Doxygen コメントを変更した場合は `make doxy` で再生成する。
