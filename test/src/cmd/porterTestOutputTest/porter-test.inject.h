#ifndef PORTER_TEST_INJECT_H
#define PORTER_TEST_INJECT_H

#include <cplat/prompt/pinned_prompt.h>
#include <porter/porter_type.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    void test_binary_receive(const void *data, size_t length);
    int test_do_open(potr_role role, const char *config_path, int64_t service_id, int *is_bidir_out, int *can_send_out);
    void test_set_stop_target_screen(cplat_pinned_prompt *screen);
    void test_invoke_shutdown_callback(void);
#ifdef __cplusplus
}
#endif

#endif /* PORTER_TEST_INJECT_H */
