/* Test: Transaction Engine (B3, B4, B5) */
#include <danos/core/transaction.h>
#include <stdio.h>
#include <assert.h>

int test_transaction(void)
{
    danos_tx_manager_init();

    /* B3: full lifecycle */
    danos_tx_t tx = {0};
    danos_status_t st = danos_tx_begin(&tx, "test", NULL);
    assert(st == DANOS_OK);
    assert(tx.id > 0);
    assert(tx.state == DANOS_TX_OPEN);

    st = danos_tx_prepare(&tx);
    assert(st == DANOS_OK);
    assert(tx.state == DANOS_TX_PREPARE);

    st = danos_tx_validate(&tx);
    assert(st == DANOS_OK);
    assert(tx.state == DANOS_TX_VALIDATE);

    st = danos_tx_commit(&tx);
    assert(st == DANOS_OK);
    assert(tx.state == DANOS_TX_COMMIT);

    st = danos_tx_verify(&tx);
    assert(st == DANOS_OK);
    assert(tx.state == DANOS_TX_DONE);

    /* B3: abort path */
    danos_tx_t tx2 = {0};
    st = danos_tx_begin(&tx2, "test", NULL);
    assert(st == DANOS_OK);
    st = danos_tx_abort(&tx2);
    assert(st == DANOS_OK);
    assert(tx2.state == DANOS_TX_ABORT);

    /* B3: invalid transitions */
    danos_tx_t tx3 = {0};
    st = danos_tx_begin(&tx3, "test", NULL);
    /* commit without prepare/validate should fail */
    st = danos_tx_commit(&tx3);
    assert(st == DANOS_ERR_TX_INVALID);
    danos_tx_abort(&tx3);

    /* B4: concurrent transactions (multi-begin, serial commit) */
    danos_tx_t txa = {0}, txb = {0};
    assert(danos_tx_begin(&txa, "a", NULL) == DANOS_OK);
    assert(danos_tx_begin(&txb, "b", NULL) == DANOS_OK);
    assert(danos_tx_prepare(&txa) == DANOS_OK);
    assert(danos_tx_prepare(&txb) == DANOS_OK);
    assert(danos_tx_validate(&txa) == DANOS_OK);
    assert(danos_tx_validate(&txb) == DANOS_OK);
    assert(danos_tx_commit(&txa) == DANOS_OK);
    assert(danos_tx_commit(&txb) == DANOS_OK);
    assert(danos_tx_verify(&txa) == DANOS_OK);
    assert(danos_tx_verify(&txb) == DANOS_OK);

    /* B5: atomic commit helper */
    danos_tx_t tx4 = {0};
    st = danos_tx_begin(&tx4, "atomic", NULL);
    assert(st == DANOS_OK);
    st = danos_tx_commit_atomic(&tx4);
    assert(st == DANOS_OK);
    assert(tx4.state == DANOS_TX_DONE);

    danos_tx_manager_fini();
    printf("[PASS] test_transaction: state machine + concurrency + atomic\n");
    return 0;
}
