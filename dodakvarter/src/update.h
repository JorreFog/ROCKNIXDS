// update.h: the game's own updates (update.c, device/update.sh)
#pragma once
enum { UPD_UNSUPPORTED, UPD_IDLE, UPD_CHECKING, UPD_LATEST, UPD_OFFLINE, UPD_AVAILABLE, UPD_INSTALLING, UPD_DONE, UPD_FAILED };
int update_supported(void);             /* the session named an updater (DK_UPDATER) */
void update_check(void);                /* in the background: is there a newer version */
void update_install(void);              /* in the background: get it and put it in place */
int update_state(char *ver, int vn, char *step, int sn);   /* UPD_*, the version it's about, the step it's at */
#define UPDATE_EXIT 75                  /* the game's exit status after an update: the session starts it again */
