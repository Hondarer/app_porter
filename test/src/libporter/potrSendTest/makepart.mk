# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/api/potrSend.c

# テスト対象が依存するソース ファイル
# potrSend.c が送信キューを呼ぶため追加する
# (potrSendQueue.c 自体の試験は potrSendQueueTest で行う)
ADD_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potrSendQueue.c

# ライブラリの指定
LIBS += mock_porter mock_com_util mock_libc
