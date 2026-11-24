#ifndef _IN_TEST_SRC
    #include "porter-test.c"
#endif
#include "porter-test.inject.h"
void test_binary_receive(const void *data, const size_t length)
{
    on_recv(1, 0, POTR_EVENT_DATA, data, length);
}

int test_do_open(potr_role role, const char *config_path, int64_t service_id, int *is_bidir_out, int *can_send_out)
{
    porter_test_session session = {0};
    int rc = do_open(&session, role, config_path, service_id);

    if (is_bidir_out != NULL)
    {
        *is_bidir_out = session.is_bidir;
    }
    if (can_send_out != NULL)
    {
        *can_send_out = session.can_send;
    }

    if (session.is_open)
    {
        do_close(&session);
    }

    return rc;
}

void test_set_stop_target_screen(cplat_pinned_prompt *screen)
{
    cplat_local_lock *stop_lock = get_screen_stop_lock();
    if (stop_lock != NULL)
    {
        set_stop_target_screen(stop_lock, screen);
    }
}

void test_invoke_shutdown_callback(void)
{
    s_shutdown_requested = 0;
    cplat_atomic_store_i32(&s_running, 1, CPLAT_MEMORY_ORDER_RELEASE);
    porter_test_shutdown_request_callback(NULL, NULL);
}

void test_ensure_tracer_started(void)
{
    ensure_tracer_started();
}

int test_get_tracer_started(void)
{
    return s_tracer_started;
}

void test_reset_tracer_started(void)
{
    s_tracer_started = 0;
}

void test_reset_tracer_screen(void)
{
    s_screen = NULL;
}

void test_do_close_handle(potr_context *handle, int64_t service_id)
{
    porter_test_session session = {0};

    session.handle = handle;
    session.service_id = service_id;
    session.is_open = 1;
    do_close(&session);
}

int test_format_path_states(char *dest, size_t dest_size, const int *states)
{
    return format_path_states(dest, dest_size, states);
}

size_t test_path_states_capacity(void)
{
    return (size_t)POTR_MAX_PATH * PATH_STATE_ENTRY_MAX_BYTES + 1U;
}

void test_on_path_state_event(int64_t service_id, potr_event event, size_t path_idx, const int *states)
{
    on_recv(service_id, 0, event, states, path_idx);
}
