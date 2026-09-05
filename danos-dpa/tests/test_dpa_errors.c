/*
 * Test: DPA error code string mapping (A3)
 */
#include <danos/dpa.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int test_errors(void)
{
    /* Every error code must map to a non-empty string */
    assert(strcmp(danos_status_str(DANOS_OK), "OK") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_INVALID_ARG), "INVALID_ARG") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_NOT_FOUND), "NOT_FOUND") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_EXISTS), "EXISTS") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_NO_MEMORY), "NO_MEMORY") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_NO_CAPACITY), "NO_CAPACITY") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_NOT_SUPPORTED), "NOT_SUPPORTED") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_PERMISSION), "PERMISSION") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_TX_CONFLICT), "TX_CONFLICT") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_TX_TIMEOUT), "TX_TIMEOUT") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_TX_ABORTED), "TX_ABORTED") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_TX_ROLLBACK), "TX_ROLLBACK") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_TX_INVALID), "TX_INVALID") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_BACKEND_DOWN), "BACKEND_DOWN") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_BACKEND_BUSY), "BACKEND_BUSY") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_BACKEND_IO), "BACKEND_IO") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_VERIFY_FAIL), "VERIFY_FAIL") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_PARTIAL), "PARTIAL") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_VERSION), "VERSION") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_CAPABILITY), "CAPABILITY") == 0);
    assert(strcmp(danos_status_str(DANOS_ERR_INTERNAL), "INTERNAL") == 0);

    /* Unknown code returns "UNKNOWN", not NULL */
    assert(danos_status_str((danos_status_t)999) != NULL);
    assert(strcmp(danos_status_str((danos_status_t)999), "UNKNOWN") == 0);

    printf("[PASS] test_errors: 21 error codes mapped\n");
    return 0;
}

int main(void)
{
    if (test_errors() != 0) return 1;
    printf("=== test_dpa_errors: all passed ===\n");
    return 0;
}
