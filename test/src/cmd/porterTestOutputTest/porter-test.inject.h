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
    void test_ensure_tracer_started(void);
    int test_get_tracer_started(void);
    void test_reset_tracer_started(void);
    void test_do_close_handle(potr_context *handle, int64_t service_id);
    int test_format_path_states(char *dest, size_t dest_size, const int *states);
    size_t test_path_states_capacity(void);
    void test_on_path_state_event(int64_t service_id, potr_event event, size_t path_idx, const int *states);
#ifdef __cplusplus
}
#endif

#endif /* PORTER_TEST_INJECT_H */
