/* Test: Object Registry (B1) */
#include <danos/core/object_registry.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

int test_object(void)
{
    danos_object_store_t *s = danos_object_store_create(64);
    assert(s != NULL);

    /* Create */
    const char *data = "hello";
    danos_status_t st = danos_object_create(s, DANOS_OBJ_ROUTE, 1, data, 6);
    assert(st == DANOS_OK);

    /* Duplicate create fails */
    st = danos_object_create(s, DANOS_OBJ_ROUTE, 1, data, 6);
    assert(st == DANOS_ERR_EXISTS);

    /* Read */
    char buf[16] = {0};
    size_t sz = sizeof(buf);
    st = danos_object_read(s, DANOS_OBJ_ROUTE, 1, buf, &sz);
    assert(st == DANOS_OK);
    assert(sz == 6);
    assert(strcmp(buf, "hello") == 0);

    /* Update */
    st = danos_object_update(s, DANOS_OBJ_ROUTE, 1, "world", 6);
    assert(st == DANOS_OK);
    sz = sizeof(buf);
    st = danos_object_read(s, DANOS_OBJ_ROUTE, 1, buf, &sz);
    assert(strcmp(buf, "world") == 0);

    /* Count */
    assert(danos_object_count(s, DANOS_OBJ_ROUTE) == 1);
    danos_object_create(s, DANOS_OBJ_ROUTE, 2, "foo", 4);
    assert(danos_object_count(s, DANOS_OBJ_ROUTE) == 2);

    /* Delete */
    st = danos_object_delete(s, DANOS_OBJ_ROUTE, 1);
    assert(st == DANOS_OK);
    st = danos_object_delete(s, DANOS_OBJ_ROUTE, 1);
    assert(st == DANOS_ERR_NOT_FOUND);
    assert(danos_object_count(s, DANOS_OBJ_ROUTE) == 1);

    danos_object_store_destroy(s);
    printf("[PASS] test_object: CRUD works\n");
    return 0;
}
