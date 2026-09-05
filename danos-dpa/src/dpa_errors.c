/*
 * DANOS-Open DPA error code string mapping
 * Implements danos_status_str() from danos/dpa.h
 */

#include <danos/dpa.h>

const char *danos_status_str(danos_status_t st)
{
    switch (st) {
    case DANOS_OK:                return "OK";
    case DANOS_ERR_INVALID_ARG:   return "INVALID_ARG";
    case DANOS_ERR_NOT_FOUND:     return "NOT_FOUND";
    case DANOS_ERR_EXISTS:        return "EXISTS";
    case DANOS_ERR_NO_MEMORY:     return "NO_MEMORY";
    case DANOS_ERR_NO_CAPACITY:   return "NO_CAPACITY";
    case DANOS_ERR_NOT_SUPPORTED: return "NOT_SUPPORTED";
    case DANOS_ERR_PERMISSION:    return "PERMISSION";
    case DANOS_ERR_TX_CONFLICT:   return "TX_CONFLICT";
    case DANOS_ERR_TX_TIMEOUT:    return "TX_TIMEOUT";
    case DANOS_ERR_TX_ABORTED:    return "TX_ABORTED";
    case DANOS_ERR_TX_ROLLBACK:   return "TX_ROLLBACK";
    case DANOS_ERR_TX_INVALID:    return "TX_INVALID";
    case DANOS_ERR_BACKEND_DOWN:  return "BACKEND_DOWN";
    case DANOS_ERR_BACKEND_BUSY:  return "BACKEND_BUSY";
    case DANOS_ERR_BACKEND_IO:    return "BACKEND_IO";
    case DANOS_ERR_VERIFY_FAIL:   return "VERIFY_FAIL";
    case DANOS_ERR_PARTIAL:       return "PARTIAL";
    case DANOS_ERR_VERSION:       return "VERSION";
    case DANOS_ERR_CAPABILITY:    return "CAPABILITY";
    case DANOS_ERR_INTERNAL:      return "INTERNAL";
    default:                      return "UNKNOWN";
    }
}
