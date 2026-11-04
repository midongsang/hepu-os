/* ============================================================================
 * 赫普OS - 媒体播放框架
 * MediaPlayer + 播放列表 + 均衡器 + 媒体扫描
 * ========================================================================== */

#include <stdint.h>
#include "media.h"
#include "vga.h"
#include "string.h"

static media_player_t players[MEDIA_MAX_PLAYERS];
static media_playlist_t playlists[4];
static int player_count = 0;
static int playlist_count = 0;
static int media_initialized = 0;

/* 均衡器预设 */
static const float eq_presets[][MEDIA_EQ_BANDS] = {
    /* Flat */
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* Bass Boost */
    {6, 5, 4, 2, 0, 0, 0, 0, 0, 0},
    /* Treble Boost */
    {0, 0, 0, 0, 0, 0, 2, 4, 5, 6},
    /* Vocal */
    {-2, -1, 0, 2, 4, 4, 2, 0, -1, -2},
    /* Rock */
    {4, 3, 2, 0, -1, -1, 0, 2, 3, 4},
    /* Pop */
    {-1, 1, 3, 4, 3, 1, -1, -1, 1, 2},
    /* Jazz */
    {3, 2, 1, 2, -1, -1, 0, 1, 2, 3},
    /* Classical */
    {4, 3, 2, 1, 0, 0, 0, 1, 2, 3},
};

static const char *eq_preset_names[] = {
    "Flat", "Bass", "Treble", "Vocal", "Rock", "Pop", "Jazz", "Classical"
};

void media_init(void)
{
    for (int i = 0; i < MEDIA_MAX_PLAYERS; i++) players[i].used = 0;
    for (int i = 0; i < 4; i++) playlists[i].used = 0;
    player_count = 0;
    playlist_count = 0;
    media_initialized = 1;

    vga_puts("[MEDIA] media framework init done \n");
    vga_printf(" player: %d, playlists: %d, EQ preset: %d\n",
               MEDIA_MAX_PLAYERS, 4, 8);
}

int media_player_create(media_type_t type)
{
    if (!media_initialized) return -1;
    for (int i = 0; i < MEDIA_MAX_PLAYERS; i++) {
        if (!players[i].used) {
            memset(&players[i], 0, sizeof(media_player_t));
            players[i].used = 1;
            players[i].id = i;
            players[i].type = type;
            players[i].state = MEDIA_STATE_IDLE;
            players[i].volume = 1.0f;
            players[i].playback_speed = 1.0f;
            players[i].equalizer.preamp = 0.0f;
            player_count++;
            vga_printf("[MEDIA] create player #%d(%s)\n", i,
                       type == MEDIA_TYPE_AUDIO ? " audio " :
                       type == MEDIA_TYPE_VIDEO ? " video " : " streams media ");
            return i;
        }
    }
    return -1;
}

int media_player_destroy(int player_id)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    players[player_id].used = 0;
    player_count--;
    return 0;
}

int media_player_set_source(int player_id, const char *path)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    if (!path) return -1;
    strncpy(players[player_id].source, path, 255);
    players[player_id].source[255] = '\0';
    players[player_id].state = MEDIA_STATE_INITIALIZED;

    /* 模拟解析元数据 */
    media_metadata_t *m = &players[player_id].metadata;
    memset(m, 0, sizeof(media_metadata_t));
    m->type = players[player_id].type;
    m->duration = 180000;  /* 3分钟 */
    m->bitrate = 320;
    m->sample_rate = 44100;
    m->channels = 2;
    strcpy(m->title, "Unknown Track");
    strcpy(m->artist, "Unknown Artist");
    strcpy(m->album, "Unknown Album");

    vga_printf("[MEDIA] player #%d source: %s\n", player_id, path);
    return 0;
}

int media_player_prepare(int player_id)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    if (players[player_id].state != MEDIA_STATE_INITIALIZED) return -1;
    players[player_id].state = MEDIA_STATE_PREPARING;
    /* 原型阶段: 同步准备 */
    players[player_id].state = MEDIA_STATE_PREPARED;
    players[player_id].current_position = 0;
    return 0;
}

int media_player_start(int player_id)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    if (players[player_id].state != MEDIA_STATE_PREPARED &&
        players[player_id].state != MEDIA_STATE_PAUSED &&
        players[player_id].state != MEDIA_STATE_COMPLETED) return -1;
    players[player_id].state = MEDIA_STATE_STARTED;
    vga_printf("[MEDIA] player #%d start play \n", player_id);
    return 0;
}

int media_player_pause(int player_id)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    if (players[player_id].state != MEDIA_STATE_STARTED) return -1;
    players[player_id].state = MEDIA_STATE_PAUSED;
    return 0;
}

int media_player_stop(int player_id)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    players[player_id].state = MEDIA_STATE_STOPPED;
    players[player_id].current_position = 0;
    return 0;
}

int media_player_seek(int player_id, uint64_t position_ms)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    if (position_ms > players[player_id].metadata.duration) return -1;
    players[player_id].current_position = position_ms;
    return 0;
}

int media_player_set_volume(int player_id, float volume)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    players[player_id].volume = volume;
    return 0;
}

int media_player_set_mute(int player_id, int mute)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    players[player_id].muted = mute ? 1 : 0;
    return 0;
}

int media_player_set_speed(int player_id, float speed)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    if (speed < 0.5f) speed = 0.5f;
    if (speed > 2.0f) speed = 2.0f;
    players[player_id].playback_speed = speed;
    return 0;
}

int media_player_set_looping(int player_id, int loop)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    players[player_id].looping = loop ? 1 : 0;
    return 0;
}

uint64_t media_player_get_position(int player_id)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return 0;
    return players[player_id].current_position;
}

uint64_t media_player_get_duration(int player_id)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return 0;
    return players[player_id].metadata.duration;
}

media_state_t media_player_get_state(int player_id)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return MEDIA_STATE_ERROR;
    return players[player_id].state;
}

int media_player_get_metadata(int player_id, media_metadata_t *metadata)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used || !metadata) return -1;
    memcpy(metadata, &players[player_id].metadata, sizeof(media_metadata_t));
    return 0;
}

int media_eq_set_band(int player_id, int band, float gain_db)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    if (band < 0 || band >= MEDIA_EQ_BANDS) return -1;
    if (gain_db < -12.0f) gain_db = -12.0f;
    if (gain_db > 12.0f) gain_db = 12.0f;
    players[player_id].equalizer.bands[band] = gain_db;
    return 0;
}

float media_eq_get_band(int player_id, int band)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return 0;
    if (band < 0 || band >= MEDIA_EQ_BANDS) return 0;
    return players[player_id].equalizer.bands[band];
}

int media_eq_enable(int player_id, int enable)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    players[player_id].equalizer.enabled = enable ? 1 : 0;
    return 0;
}

int media_eq_set_preset(int player_id, const char *preset)
{
    if (player_id < 0 || player_id >= MEDIA_MAX_PLAYERS || !players[player_id].used) return -1;
    if (!preset) return -1;
    for (int i = 0; i < 8; i++) {
        if (strcmp(preset, eq_preset_names[i]) == 0) {
            for (int b = 0; b < MEDIA_EQ_BANDS; b++) {
                players[player_id].equalizer.bands[b] = eq_presets[i][b];
            }
            players[player_id].equalizer.enabled = 1;
            vga_printf("[MEDIA] EQ preset: %s\n", preset);
            return 0;
        }
    }
    return -1;
}

int media_playlist_create(const char *name)
{
    if (!media_initialized || !name) return -1;
    for (int i = 0; i < 4; i++) {
        if (!playlists[i].used) {
            memset(&playlists[i], 0, sizeof(media_playlist_t));
            playlists[i].used = 1;
            playlists[i].id = i;
            strncpy(playlists[i].name, name, 63);
            playlist_count++;
            return i;
        }
    }
    return -1;
}

int media_playlist_destroy(int playlist_id)
{
    if (playlist_id < 0 || playlist_id >= 4 || !playlists[playlist_id].used) return -1;
    playlists[playlist_id].used = 0;
    playlist_count--;
    return 0;
}

int media_playlist_add_track(int playlist_id, const char *path)
{
    if (playlist_id < 0 || playlist_id >= 4 || !playlists[playlist_id].used) return -1;
    if (!path) return -1;
    if (playlists[playlist_id].track_count >= MEDIA_MAX_TRACKS) return -1;

    media_track_t *t = &playlists[playlist_id].tracks[playlists[playlist_id].track_count];
    t->used = 1;
    strncpy(t->path, path, 255);
    t->metadata.duration = 180000;
    strcpy(t->metadata.title, "Track");
    playlists[playlist_id].track_count++;
    return 0;
}

int media_playlist_remove_track(int playlist_id, int index)
{
    if (playlist_id < 0 || playlist_id >= 4 || !playlists[playlist_id].used) return -1;
    if (index < 0 || index >= playlists[playlist_id].track_count) return -1;
    for (int i = index; i < playlists[playlist_id].track_count - 1; i++) {
        playlists[playlist_id].tracks[i] = playlists[playlist_id].tracks[i + 1];
    }
    playlists[playlist_id].track_count--;
    return 0;
}

int media_playlist_get_current(int playlist_id, media_track_t *track)
{
    if (playlist_id < 0 || playlist_id >= 4 || !playlists[playlist_id].used || !track) return -1;
    int idx = playlists[playlist_id].current_index;
    if (idx < 0 || idx >= playlists[playlist_id].track_count) return -1;
    memcpy(track, &playlists[playlist_id].tracks[idx], sizeof(media_track_t));
    return 0;
}

int media_playlist_next(int playlist_id)
{
    if (playlist_id < 0 || playlist_id >= 4 || !playlists[playlist_id].used) return -1;
    int next = playlists[playlist_id].current_index + 1;
    if (next >= playlists[playlist_id].track_count) {
        if (playlists[playlist_id].repeat_mode == 1) next = 0;
        else return -1;
    }
    playlists[playlist_id].current_index = next;
    return next;
}

int media_playlist_prev(int playlist_id)
{
    if (playlist_id < 0 || playlist_id >= 4 || !playlists[playlist_id].used) return -1;
    int prev = playlists[playlist_id].current_index - 1;
    if (prev < 0) {
        if (playlists[playlist_id].repeat_mode == 1) prev = playlists[playlist_id].track_count - 1;
        else return -1;
    }
    playlists[playlist_id].current_index = prev;
    return prev;
}

int media_playlist_set_repeat(int playlist_id, int mode)
{
    if (playlist_id < 0 || playlist_id >= 4 || !playlists[playlist_id].used) return -1;
    playlists[playlist_id].repeat_mode = mode;
    return 0;
}

int media_playlist_set_shuffle(int playlist_id, int shuffle)
{
    if (playlist_id < 0 || playlist_id >= 4 || !playlists[playlist_id].used) return -1;
    playlists[playlist_id].shuffle = shuffle ? 1 : 0;
    return 0;
}

int media_playlist_get_info(int playlist_id, media_playlist_t *info)
{
    if (playlist_id < 0 || playlist_id >= 4 || !playlists[playlist_id].used || !info) return -1;
    memcpy(info, &playlists[playlist_id], sizeof(media_playlist_t));
    return 0;
}

int media_scan_directory(const char *path)
{
    if (!path) return -1;
    vga_printf("[MEDIA] scan dir: %s\n", path);
    /* 原型阶段: 模拟扫描发现3个媒体文件 */
    return 3;
}

int media_player_count(void) { return player_count; }

void media_tick(void)
{
    if (!media_initialized) return;
    /* 更新所有播放中的播放器位置 */
    for (int i = 0; i < MEDIA_MAX_PLAYERS; i++) {
        if (players[i].used && players[i].state == MEDIA_STATE_STARTED) {
            players[i].current_position += 10;  /* 每tick 10ms */
            players[i].total_played += 10;
            if (players[i].current_position >= players[i].metadata.duration) {
                if (players[i].looping) {
                    players[i].current_position = 0;
                } else {
                    players[i].state = MEDIA_STATE_COMPLETED;
                    if (players[i].on_completion) players[i].on_completion(i);
                }
            }
            if (players[i].on_progress) players[i].on_progress(i, players[i].current_position);
        }
    }
}
