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
