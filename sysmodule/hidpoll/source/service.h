#pragma once

#define HP_MAX_SESSIONS 4

/* Register the "hidpoll" IPC service and dispatch requests forever.
 * Returns only if smRegisterService fails. */
void service_run(void);
