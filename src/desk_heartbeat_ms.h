#ifndef DESK_HEARTBEAT_MS_H
#define DESK_HEARTBEAT_MS_H
/* A 100 ms probe interval plus the loop's 100 ms poll leaves at least 550 ms
   of the unchanged 750 ms silence budget for a reply. Live capture measured
   a 513 ms master response and a separate loss/retransmission event. */
#define DESK_HEARTBEAT_MS 100
#endif
