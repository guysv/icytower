#ifndef ICYTOWER_FULLSCREEN_H
#define ICYTOWER_FULLSCREEN_H

/* Native window size; fixed 640x480 gameplay is scaled uniformly to fit. */
#define ICYTOWER_LOGICAL_W 640
#define ICYTOWER_LOGICAL_H 480
#define ICYTOWER_WINDOW_W  1280
#define ICYTOWER_WINDOW_H   960

void icytower_apply_window_viewport(void);
void enable_fullscreen(void);
void disable_fullscreen(void);

#endif /* ICYTOWER_FULLSCREEN_H */
