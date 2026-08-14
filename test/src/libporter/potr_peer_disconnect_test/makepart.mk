# テスト対象のソース ファイル
TEST_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/api/potr_peer_disconnect.c

# テスト対象が依存するソース ファイル
# potr_peer_disconnect.c が経路イベントの通知関数を呼ぶため追加する
# (potr_path_event.c 自体の試験は potrPathEventTest で行う)
ADD_SRCS := \
	$(MYAPP_DIR)/prod/libsrc/porter/potr_path_event.c

# ライブラリの指定
LIBS += mock_porter mock_com_util mock_libc
