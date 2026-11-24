#ifndef _IN_TEST_SRC
    #include "porter-test.c"
#endif

#include "porter-test.inject.h"

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

int potr_service_open_from_config(const char *config_path, int64_t service_id, potr_role role, potr_recv_fn callback,
                                  potr_context **handle)
{
    (void)config_path;
    (void)service_id;
    (void)role;
    (void)callback;
    if (handle != NULL)
    {
        *handle = NULL;
    }
    return POTR_ERR_UNKNOWN;
}

int potr_service_send(potr_context *handle, potr_peer_id peer_id, const void *data, size_t len, int flags)
{
    (void)handle;
    (void)peer_id;
    (void)data;
    (void)len;
    (void)flags;
    return POTR_ERR_UNKNOWN;
}

int potr_service_get_type(const char *config_path, int64_t service_id, potr_type *type)
{
    (void)config_path;
    (void)service_id;
    (void)type;
    return POTR_ERR_UNKNOWN;
}
