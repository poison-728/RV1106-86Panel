#ifndef MUSIC_H
#define MUSIC_H

typedef enum {
    MUSIC_STOPPED = 0,
    MUSIC_PLAYING,
} music_state_t;

int  music_init(void);                       /* 扫描 /userdata/music/ *.wav, 初始化混音器 */
int  music_count(void);                      /* 曲库歌曲数 */
int  music_current(void);                    /* 当前曲目下标, 无则 -1 */
void music_get_name(int idx, char *buf, int len);   /* 取歌名(去路径去后缀) */
music_state_t music_get_state(void);

void music_play(int idx);    /* 播第 idx 首(0起); idx<0 = 续播当前/从第一首 */
void music_stop(void);
void music_next(void);
void music_prev(void);

void music_set_volume(int vol);   /* 0~100 */
int  music_get_volume(void);

#endif /* MUSIC_H */

