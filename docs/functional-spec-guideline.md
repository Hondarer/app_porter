# porter 機能仕様の記載規範

## 概要

本書は、porter の機能仕様に固有の設定と追加規則を定めます。  
対象は `docs/functional-spec/` 配下の機能仕様です。

記載する内容、記載しない内容、章立て、記載粒度、要件 ID と UUID の運用、下流成果物での参照記法は、[機能仕様の記載規範](../../general/docs/functional-spec-guideline.md) に従います。  
本書は、同規範が app 固有規範に委ねる項目だけを定めます。

## 要件 ID の構成

| 項目 | 値 |
|---|---|
| 要件 ID 接頭辞 | `POTR` |
| 参照コメント タグ | `potr-req` |
| 機能要件表の見出し | `porter の要件` |

要件 ID は `POTR-<CATEGORY>-<TYPE>-NNN` の形式になります。  
例を次に示します。

```text
POTR-DELIVERY-FUNC-001
POTR-HEALTH-QUAL-001
```

## カテゴリごとの主語

要件文は、カテゴリに対応する次の主語で始めます。

| カテゴリ | 主語 |
|---|---|
| `BASE` | porter の基盤機能 |
| `CONFIG` | porter の構成機能 |
| `DELIVERY` | porter のメッセージ配送機能 |
| `HEALTH` | porter の死活監視機能 |
| `MESSAGE` | porter のメッセージ構成機能 |
| `MULTIPATH` | porter のマルチパス機能 |
| `SECURITY` | porter の通信保護機能 |
| `SERVICE` | porter のサービス管理機能 |
| `SESSION` | porter のセッション識別機能 |
| `TRANSPORT` | porter の通信種別機能 |

カテゴリは、`docs/functional-spec/` に配置する機能仕様のファイル名と一対一で対応します。  
機能カテゴリを追加または削除する場合は、この表も同じ変更で更新してください。

## porter 固有の追加規則

porter は下位の通信を他の app の抽象を通じて利用しますが、機能仕様には依存先の app 名を記載しません。  
下位の通信自体が備える性質は、porter の要件として記載せず、必要な場合だけ「前提と制約」または「適用範囲外」で境界を示します。

通信の対向側と交換する形式は、相互運用のために固定する条件です。  
形式を固定する必要がある場合は、パケットの内部構造や項目名ではなく、利用側または対向側から観測できる性質として記載します。

## 確認

機能仕様の変更後は、[機能仕様の記載規範](../../general/docs/functional-spec-guideline.md) の「確認」に記載されている項目を確認します。

要件 ID と UUID の形式および参照関係は、ワークスペース ルートで次のコマンドを実行して確認します。  
検査は、機能仕様が配置されているすべての app を対象とします。

```shell
python3 bin/check_functional_spec.py
```
