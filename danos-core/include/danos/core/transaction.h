/*
 * DANOS-Open Core: Transaction Engine (B3, B4, B5)
 *
 * State machine: OPEN → PREPARE → VALIDATE → COMMIT → VERIFY → DONE
 * Concurrency:   multi-reader, single-writer (global write mutex)
 * Timeout:       per-phase deadlines
 */

#ifndef DANOS_CORE_TRANSACTION_H__
#define DANOS_CORE_TRANSACTION_H__

#include <danos/dpa.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Transaction record (internal, extends public danos_tx_t) */
typedef struct danos_tx_record {
    danos_tx_t           pub;        /* public view */
    pthread_mutex_t     lock;       /* per-tx lock */
    bool                 in_use;
    struct danos_tx_record *next;   /* chain in pool */
} danos_tx_record_t;

/* Transaction manager */
typedef struct {
    pthread_mutex_t     write_lock;  /* single-writer mutex */
    pthread_mutex_t     pool_lock;   /* protects tx pool */
    danos_tx_record_t  *free_list;   /* recycled tx records */
    uint64_t            next_tx_id;  /* monotonic ID counter */
    uint64_t            active_count;
} danos_tx_manager_t;

/* Global transaction manager */
extern danos_tx_manager_t *g_tx_mgr;

/* Init/fini */
int danos_tx_manager_init(void);
void danos_tx_manager_fini(void);

/* Internal: allocate/free tx record */
danos_tx_record_t *danos_tx_record_alloc(void);
void danos_tx_record_free(danos_tx_record_t *rec);

/* Check if tx is in expected state */
bool danos_tx_in_state(danos_tx_record_t *rec, danos_tx_state_t expected);

/* Validate tx pointer */
danos_tx_record_t *danos_tx_validate_ptr(danos_tx_t *tx);

#ifdef __cplusplus
}
#endif

#endif /* DANOS_CORE_TRANSACTION_H__ */
