#ifndef PORTER_TEST_INJECT_H
#define PORTER_TEST_INJECT_H

#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    void test_binary_receive(const void *data, size_t length);
#ifdef __cplusplus
}
#endif

#endif /* PORTER_TEST_INJECT_H */
