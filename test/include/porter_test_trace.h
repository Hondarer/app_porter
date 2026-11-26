#ifndef PORTER_TEST_TRACE_H
#define PORTER_TEST_TRACE_H

#include <cplat/base/result.h>
#include <cplat/trace/tracer.h>
#include <porter.h>

/**
 * porter のトレーサーの標準エラー出力の詳細度を CPLAT_TRACE_LEVEL_ERROR にして開始する。
 *
 * porter は待ち受け (bind) の失敗を、OS のエラー コードとともに ERROR で出力する。
 * 実際のソケットを使う結合テストの開始時に呼び出し、失敗の理由をテストの出力から分かるようにする。
 * 開始済みの場合も、詳細度を設定して true を返す。
 *
 * @return  設定と開始に成功した場合は true。
 */
inline bool porter_test_start_error_trace()
{
    cplat_tracer *tracer = potr_tracer_get();

    if (tracer == nullptr)
    {
        return false;
    }
    if (cplat_tracer_set_stderr_level(tracer, CPLAT_TRACE_LEVEL_ERROR) != CPLAT_OK)
    {
        return false;
    }
    return cplat_tracer_start(tracer) == CPLAT_OK;
}

#endif /* PORTER_TEST_TRACE_H */
