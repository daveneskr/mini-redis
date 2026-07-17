#ifndef MINI_REDIS_LIMITS_H
#define MINI_REDIS_LIMITS_H

#include "resp.h"

/*
 * A single client stores at most one serialized reply at a time. The extra
 * bytes cover the RESP bulk-string header, integer formatting, CRLF markers,
 * and a terminating NUL used by the serializer.
 */
#define MINI_REDIS_MAX_REPLY_SIZE (RESP_MAX_COMMAND_SIZE + 64U)

#endif
