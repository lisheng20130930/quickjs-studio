#ifndef QJS_DEBUG_AGENT_H
#define QJS_DEBUG_AGENT_H

#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Connects this QuickJS context to the pipe named by QJS_STUDIO_PIPE.
 * Returns 1 when connected, 0 when debugging was not requested, and -1 on
 * connection failure. The initial handshake intentionally blocks until the
 * IDE has installed breakpoints and starts the program.
 */
int qjs_debug_agent_connect_from_environment(JSContext *context);

#ifdef __cplusplus
}
#endif

#endif
