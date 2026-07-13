#ifndef MINI_REDIS_COMMAND_H
#define MINI_REDIS_COMMAND_H

#include "resp.h"
#include "store.h"

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
RespReply command_execute(Store *store, const Command *command);

#ifdef __cplusplus
}
#endif

#endif
