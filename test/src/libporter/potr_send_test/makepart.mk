# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/api/potr_send.c

# テスト対象が依存するソース ファイル
# potr_send.c が送信キューを呼ぶため追加する
# (potr_send_queue.c 自体の試験は potrSendQueueTest で行う)
ADD_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/infra/potr_send_queue.c

# ライブラリの指定
LIBS += mock_porter mock_com_util mock_libc
