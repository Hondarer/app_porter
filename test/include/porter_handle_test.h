#ifndef PORTER_HANDLE_TEST_H
#define PORTER_HANDLE_TEST_H
#include <porter/potr_handle.h>

/* スタック上の通信コンテキストを公開ハンドル経由で試験するための所有者。 */
class porter_handle_test
{
  public:
    // [サブ手順 名前=porter_handle_test.constructor]
    explicit porter_handle_test(potr_context *ctx) : ctx_(ctx)
    {
        EXPECT_EQ(POTR_OK, potr_internal_handle_prepare(ctx_));
        // [状態確認] - potr_internal_handle_prepare() が成功すること。
    }
    // [サブ手順終了]
    ~porter_handle_test()
    {
        potr_internal_handle_discard(ctx_);
    }
    potr_context *get() const
    {
        return potr_internal_handle_public(ctx_);
    }

  private:
    potr_context *ctx_;
};
#endif
