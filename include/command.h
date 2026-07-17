#ifndef MINI_REDIS_COMMAND_H
#define MINI_REDIS_COMMAND_H

#include "resp.h"
#include "store.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Executes a parsed RESP command against the store.
 *
 * Supported commands:
 *   PING [message]
 *   ECHO message
 *   SET key value
 *   GET key
 *   DEL key
 *   EXISTS key
 *
 * Expiration commands are intentionally not implemented yet because the
 * current Store architecture has no TTL/expires-at model.
 */
/**
 * Returns the output-buffer capacity required to serialize the command's
 * possible reply without executing or mutating the store. Returns 0 only for
 * an invalid command object.
 */
size_t command_reply_capacity_required(const Store *store,
                                       const Command *command);

RespReply command_execute(Store *store, const Command *command);

#ifdef __cplusplus
}
#endif

#endif
