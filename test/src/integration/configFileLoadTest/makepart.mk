# 実ファイルを使う設定 API の組み合わせテスト
# カバレッジは各ソースの単体テストで集計する
INCDIR += \
	$(MYAPP_DIR)/test/include

# 実物の libporter と libcplat をリンクする (mock は使わない)
LIBS += porter cplat
