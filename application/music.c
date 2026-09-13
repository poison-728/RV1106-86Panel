#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <pthread.h>
#include <stdint.h>

#include "music.h"
#include "cfg.h"

#define MUSIC_DIR  "/userdata/music"
#define MUSIC_MAX  32

static pthread_mutex_t m_lock = PTHREAD_MUTEX_INITIALIZER;

static char m_files[MUSIC_MAX][96];   /* 完整路径 */
static char m_names[MUSIC_MAX][64];   /* 显示名 */
static int  m_cnt  = 0;
static int  m_cur  = -1;
static int  m_gen  = 0;              /* 播放代数: 每次切歌/停止 +1, 旧线程据此自杀 */
static int  m_vol  = 80;
static music_state_t m_state = MUSIC_STOPPED;

static void make_name(const char *path, char *out, int len)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(out, len, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

int music_init(void)
{
    DIR *d = opendir(MUSIC_DIR);
    if (!d) return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && m_cnt < MUSIC_MAX) {
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcasecmp(dot, ".wav") != 0) continue;
        snprintf(m_files[m_cnt], sizeof(m_files[0]), "%s/%s", MUSIC_DIR, ent->d_name);
        make_name(m_files[m_cnt], m_names[m_cnt], sizeof(m_names[0]));
        m_cnt++;
    }
    closedir(d);

    m_vol = cfg_get_int("volume", 80);          /* 恢复上次音量 */

    /* 链路初始化: HPMIX 中性档(0dB), LINEOUT 按恢复的音量开 */
    system("amixer set 'DAC HPMIX' 50% >/dev/null 2>&1");
    music_set_volume(m_vol);
    return m_cnt;
}

/* 播放线程: system() 阻塞到播完或被 killall; 自然播完自动下一首 */
static void *worker(void *arg)
{
    intptr_t gen = (intptr_t)arg;

    for (;;) {
        pthread_mutex_lock(&m_lock);
        if (gen != m_gen || m_state != MUSIC_PLAYING || m_cur < 0) {
            pthread_mutex_unlock(&m_lock);
            return NULL;
        }
        char cmd[192];
        snprintf(cmd, sizeof(cmd), "aplay -q \"%s\" 2>/dev/null", m_files[m_cur]);
        pthread_mutex_unlock(&m_lock);

        system(cmd);

        pthread_mutex_lock(&m_lock);
        if (gen != m_gen) {            /* 播放期间发生了切歌/停止 */
            pthread_mutex_unlock(&m_lock);
            return NULL;
        }
        m_cur = (m_cur + 1) % m_cnt;  /* 自然播完 → 自动下一首 */
        pthread_mutex_unlock(&m_lock);
    }
}

void music_play(int idx)
{
    pthread_mutex_lock(&m_lock);
    if (m_cnt == 0) { pthread_mutex_unlock(&m_lock); return; }
    if (idx < 0) idx = (m_cur < 0) ? 0 : m_cur;   /* 续播当前, 首次从第一首 */
    idx %= m_cnt;

    m_gen++;                                /* 作废旧 worker */
    m_cur   = idx;
    m_state = MUSIC_PLAYING;
    system("killall aplay 2>/dev/null");    /* 立即打断当前声音 */
    pthread_t tid;
    pthread_create(&tid, NULL, worker, (void *)(intptr_t)m_gen);
    pthread_detach(tid);
    pthread_mutex_unlock(&m_lock);
}

void music_stop(void)
{
    pthread_mutex_lock(&m_lock);
    m_gen++;
    m_state = MUSIC_STOPPED;
    system("killall aplay 2>/dev/null");
    pthread_mutex_unlock(&m_lock);
}

void music_next(void)
{
    int idx;
    pthread_mutex_lock(&m_lock);
    idx = (m_cur < 0) ? 0 : (m_cur + 1) % m_cnt;
    pthread_mutex_unlock(&m_lock);
    music_play(idx);
}

void music_prev(void)
{
    int idx;
    pthread_mutex_lock(&m_lock);
    idx = (m_cur < 0) ? 0 : (m_cur - 1 + m_cnt) % m_cnt;
    pthread_mutex_unlock(&m_lock);
    music_play(idx);
}

void music_set_volume(int vol)
{
    char cmd[128];
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    m_vol = vol;
    cfg_set_int("volume", vol);                 /* 音量持久化 */

    if (vol == 0)
        snprintf(cmd, sizeof(cmd), "amixer set 'DAC LINEOUT' 0%% mute >/dev/null 2>&1");
    else
        snprintf(cmd, sizeof(cmd), "amixer set 'DAC LINEOUT' %d%% unmute >/dev/null 2>&1", vol);
    system(cmd);
}

int music_get_volume(void)          { return m_vol; }
int music_count(void)               { return m_cnt; }
int music_current(void)             { return m_cur; }
music_state_t music_get_state(void)  { return m_state; }

void music_get_name(int idx, char *buf, int len)
{
    if (!buf || len <= 0) return;
    buf[0] = '\0';
    if (idx < 0 || idx >= m_cnt) return;
    snprintf(buf, len, "%s", m_names[idx]);
}

