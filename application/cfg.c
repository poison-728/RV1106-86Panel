#include <stdio.h>
#include <string.h>

#define CFG_PATH "/userdata/smarthome.cfg"
#define CFG_MAX  64

static void trim_key(char *k)
{
    char *e = k + strlen(k);
    while (e > k && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
}

int cfg_get_int(const char *key, int def)
{
    FILE *fp = fopen(CFG_PATH, "r");
    if (!fp) return def;

    char line[128], k[64];
    int v;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, " %63[^=]=%d", k, &v) == 2) {
            trim_key(k);
            if (strcmp(k, key) == 0) {
                fclose(fp);
                return v;
            }
        }
    }
    fclose(fp);
    return def;
}

int cfg_get_str(const char *key, const char *def, char *out, int out_sz)
{
    snprintf(out, out_sz, "%s", def);

    FILE *fp = fopen(CFG_PATH, "r");
    if (!fp) return -1;

    char line[128], k[64], v[64];
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, " %63[^=]=%63[^\n]", k, v) == 2) {
            trim_key(k);
            char *e = v + strlen(v);          /* 剥掉值尾部的 CR/空格(手工编辑易混入) */
            while (e > v && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = '\0';
            if (strcmp(k, key) == 0) {
                snprintf(out, out_sz, "%s", v);
                fclose(fp);
                return 0;
            }
        }
    }
    fclose(fp);
    return -1;
}

void cfg_set_int(const char *key, int val)
{
    char lines[CFG_MAX][128];
    int n = 0, updated = 0;

    /* 读出全部现有行, 就地更新目标 key */
    FILE *fp = fopen(CFG_PATH, "r");
    if (fp) {
        while (n < CFG_MAX && fgets(lines[n], sizeof(lines[n]), fp)) {
            char k[64];
            int v;
            if (sscanf(lines[n], " %63[^=]=%d", k, &v) == 2) {
                trim_key(k);
                if (strcmp(k, key) == 0) {
                    snprintf(lines[n], sizeof(lines[n]), "%s=%d\n", key, val);
                    updated = 1;
                }
            }
            n++;
        }
        fclose(fp);
    }
    if (!updated && n < CFG_MAX)
        snprintf(lines[n++], sizeof(lines[0]), "%s=%d\n", key, val);

    /* 原子落盘: 写临时文件后 rename 覆盖 */
    FILE *tf = fopen(CFG_PATH ".tmp", "w");
    if (!tf) return;
    for (int i = 0; i < n; i++) fputs(lines[i], tf);
    fclose(tf);
    rename(CFG_PATH ".tmp", CFG_PATH);
}
