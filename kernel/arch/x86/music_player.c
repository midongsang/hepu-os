/* ============================================================================
 * 赫普OS - 音乐播放器
 * ========================================================================== */

#include <stdint.h>
#include "music_player.h"
#include "vga.h"
#include "string.h"

static music_player_t mp;

/* 均衡器预设 */
static const int eq_presets[7][10] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},        /* 自定义 */
    {0, 2, 4, 4, 2, 0, -1, -1, 0, 2},       /* 流行 */
    {6, 5, 4, 2, -1, 2, 4, 5, 6, 6},        /* 摇滚 */
    {4, 3, 2, 0, -2, -2, 0, 2, 3, 4},       /* 古典 */
    {2, 3, 2, 0, -1, -1, 0, 1, 2, 3},       /* 爵士 */
    {-2, -1, 0, 2, 4, 4, 3, 2, 1, 0},       /* 人声 */
    {6, 5, 4, 2, 0, -1, -1, 0, 1, 2},       /* 重低音 */
};

static const char *eq_preset_names[] = {" 自定义 ", " streams 行 ", " 摇滚 ", " 古典 ", " 爵士 ", " 人声 ", " 重 low 音 "};

void music_player_init(void)
{
    memset(&mp, 0, sizeof(music_player_t));
    mp.initialized = 1;
    mp.enabled = 1;
    mp.state = MP_STATE_STOPPED;
    mp.mode = MP_MODE_SEQUENCE;
    mp.volume = 70;
    mp.current_track = -1;
    mp.current_playlist = -1;
    mp.lyrics_enabled = 1;
    mp.eq_enabled = 0;
    mp.eq_preset = 0;

    /* 初始化均衡器频段 */
    int freqs[] = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    for (int i = 0; i < 10; i++) {
        mp.eq_bands[i].band = i;
        mp.eq_bands[i].freq = freqs[i];
        mp.eq_bands[i].gain = 0;
    }

    /* 系统播放列表 */
    mp_playlist_create(" all 音乐 ");
    mp.playlists[0].is_system = 1;
    mp_playlist_create(" 最近播放 ");
    mp.playlists[1].is_system = 1;
    mp_playlist_create(" 我 收藏 ");
    mp.playlists[2].is_system = 1;

    /* 测试歌曲 */
    mp_add_track("/music/song1.mp3", " 夜 empty in 最亮 星 ", " 逃跑计划 ", " 世界 ");
    mp_add_track("/music/song2.flac", " 海阔天 empty ", "Beyond", " 海阔天 empty ");
    mp_add_track("/music/song3.mp3", " 晴天 ", " 周杰伦 ", " 叶惠美 ");
    mp_add_track("/music/song4.wav", " 稻香 ", " 周杰伦 ", " 魔杰座 ");
    mp_add_track("/music/song5.mp3", " 平凡之路 ", " 朴 tree ", " 猎户星座 ");

    vga_puts("[MUSIC] music player init done \n");
    vga_printf(" tracks: %d, playlists: %d, EQ: %s\n",
               mp.track_count, mp.playlist_count,
               mp.eq_enabled ? " on " : " close ");
}

/* 播放控制 */
int mp_play(int track_id)
{
    if (track_id < 0 || track_id >= MP_MAX_TRACKS || !mp.tracks[track_id].used) return -1;
    mp.current_track = track_id;
    mp.state = MP_STATE_PLAYING;
    mp.current_position = 0;
    mp.tracks[track_id].play_count++;
    mp.tracks[track_id].last_played = 1;
    mp.total_plays++;
    return 0;
}

int mp_pause(void)
{
    if (mp.state != MP_STATE_PLAYING) return -1;
    mp.state = MP_STATE_PAUSED;
    return 0;
}

int mp_resume(void)
{
    if (mp.state != MP_STATE_PAUSED) return -1;
    mp.state = MP_STATE_PLAYING;
    return 0;
}

int mp_stop(void)
{
    mp.state = MP_STATE_STOPPED;
    mp.current_position = 0;
    return 0;
}

int mp_next(void)
{
    if (mp.current_track < 0) return -1;
    int next = mp.current_track + 1;
    if (next >= mp.track_count) {
        if (mp.mode == MP_MODE_REPEAT_ALL) next = 0;
        else return -1;
    }
    return mp_play(next);
}

int mp_prev(void)
{
    if (mp.current_track < 0) return -1;
    if (mp.current_position > 3000) {
        mp.current_position = 0;
        return 0;
    }
    int prev = mp.current_track - 1;
    if (prev < 0) prev = mp.track_count - 1;
    return mp_play(prev);
}

int mp_seek(uint64_t position)
{
    if (mp.current_track < 0) return -1;
    if (position > mp.tracks[mp.current_track].duration) return -1;
    mp.current_position = position;
    return 0;
}

int mp_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    mp.volume = volume;
    return 0;
}

int mp_set_mute(int mute)
{
    mp.muted = mute ? 1 : 0;
    return 0;
}

int mp_set_play_mode(mp_play_mode_t mode)
{
    mp.mode = mode;
    return 0;
}

/* 媒体库 */
int mp_add_track(const char *path, const char *title, const char *artist, const char *album)
{
    for (int i = 0; i < MP_MAX_TRACKS; i++) {
        if (!mp.tracks[i].used) {
            memset(&mp.tracks[i], 0, sizeof(mp_track_t));
            mp.tracks[i].used = 1;
            mp.tracks[i].id = i;
            if (path) strncpy(mp.tracks[i].path, path, 255);
            if (title) strncpy(mp.tracks[i].title, title, 127);
            if (artist) strncpy(mp.tracks[i].artist, artist, 63);
            if (album) strncpy(mp.tracks[i].album, album, 63);
            mp.tracks[i].format = MP_FORMAT_MP3;
            mp.tracks[i].duration = 180000 + i * 30000;  /* 3-5分钟 */
            mp.tracks[i].size = 5 * 1024 * 1024;
            mp.tracks[i].sample_rate = 44100;
            mp.tracks[i].bitrate = 320;
            mp.tracks[i].channels = 2;
            mp.tracks[i].year = 2020 + (i % 5);
            mp.track_count++;
            mp.total_tracks++;
            mp_playlist_add_track(0, i);
            return i;
        }
    }
    return -1;
}

int mp_remove_track(int id)
{
    if (id < 0 || id >= MP_MAX_TRACKS || !mp.tracks[id].used) return -1;
    mp.tracks[id].used = 0;
    mp.track_count--;
    return 0;
}

int mp_get_track(int id, mp_track_t *track)
{
    if (id < 0 || id >= MP_MAX_TRACKS || !mp.tracks[id].used || !track) return -1;
    memcpy(track, &mp.tracks[id], sizeof(mp_track_t));
    return 0;
}

int mp_list_tracks(mp_track_t *tracks, int max)
{
    if (!tracks || max <= 0) return 0;
    int count = mp.track_count < max ? mp.track_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&tracks[i], &mp.tracks[i], sizeof(mp_track_t));
    return count;
}

int mp_list_by_artist(const char *artist, mp_track_t *tracks, int max)
{
    if (!artist || !tracks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MP_MAX_TRACKS && count < max; i++) {
        if (mp.tracks[i].used && strcmp(mp.tracks[i].artist, artist) == 0)
            memcpy(&tracks[count++], &mp.tracks[i], sizeof(mp_track_t));
    }
    return count;
}

int mp_list_by_album(const char *album, mp_track_t *tracks, int max)
{
    if (!album || !tracks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MP_MAX_TRACKS && count < max; i++) {
        if (mp.tracks[i].used && strcmp(mp.tracks[i].album, album) == 0)
            memcpy(&tracks[count++], &mp.tracks[i], sizeof(mp_track_t));
    }
    return count;
}

int mp_search(const char *query, mp_track_t *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MP_MAX_TRACKS && count < max; i++) {
        if (mp.tracks[i].used) {
            int match = 1;
            int qlen = 0; while (query[qlen]) qlen++;
            for (int j = 0; j < qlen; j++) {
                int found = 0;
                for (int k = 0; mp.tracks[i].title[k]; k++)
                    if (mp.tracks[i].title[k] == query[j]) { found = 1; break; }
                for (int k = 0; mp.tracks[i].artist[k] && !found; k++)
                    if (mp.tracks[i].artist[k] == query[j]) { found = 1; break; }
                if (!found) { match = 0; break; }
            }
            if (match) memcpy(&results[count++], &mp.tracks[i], sizeof(mp_track_t));
        }
    }
    return count;
}

int mp_scan_directory(const char *path)
{
    (void)path;
    return 0;
}

/* 播放列表 */
int mp_playlist_create(const char *name)
{
    for (int i = 0; i < MP_MAX_PLAYLISTS; i++) {
        if (!mp.playlists[i].used) {
            memset(&mp.playlists[i], 0, sizeof(mp_playlist_t));
            mp.playlists[i].used = 1;
            mp.playlists[i].id = i;
            if (name) strncpy(mp.playlists[i].name, name, 63);
            mp.playlists[i].created = 1;
            mp.playlists[i].modified = 1;
            mp.playlist_count++;
            return i;
        }
    }
    return -1;
}

int mp_playlist_delete(int id)
{
    if (id < 0 || id >= MP_MAX_PLAYLISTS || !mp.playlists[id].used) return -1;
    if (mp.playlists[id].is_system) return -1;
    mp.playlists[id].used = 0;
    mp.playlist_count--;
    return 0;
}

int mp_playlist_add_track(int playlist_id, int track_id)
{
    if (playlist_id < 0 || playlist_id >= MP_MAX_PLAYLISTS || !mp.playlists[playlist_id].used) return -1;
    if (track_id < 0 || track_id >= MP_MAX_TRACKS || !mp.tracks[track_id].used) return -1;
    if (mp.playlists[playlist_id].track_count < 256) {
        mp.playlists[playlist_id].track_ids[mp.playlists[playlist_id].track_count++] = track_id;
        mp.playlists[playlist_id].modified = 1;
        return 0;
    }
    return -1;
}

int mp_playlist_remove_track(int playlist_id, int track_id)
{
    if (playlist_id < 0 || playlist_id >= MP_MAX_PLAYLISTS || !mp.playlists[playlist_id].used) return -1;
    for (int i = 0; i < mp.playlists[playlist_id].track_count; i++) {
        if (mp.playlists[playlist_id].track_ids[i] == track_id) {
            for (int j = i; j < mp.playlists[playlist_id].track_count - 1; j++)
                mp.playlists[playlist_id].track_ids[j] = mp.playlists[playlist_id].track_ids[j+1];
            mp.playlists[playlist_id].track_count--;
            return 0;
        }
    }
    return -1;
}

int mp_playlist_get(int id, mp_playlist_t *pl)
{
    if (id < 0 || id >= MP_MAX_PLAYLISTS || !mp.playlists[id].used || !pl) return -1;
    memcpy(pl, &mp.playlists[id], sizeof(mp_playlist_t));
    return 0;
}

int mp_playlist_list(mp_playlist_t *playlists, int max)
{
    if (!playlists || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MP_MAX_PLAYLISTS && count < max; i++) {
        if (mp.playlists[i].used) memcpy(&playlists[count++], &mp.playlists[i], sizeof(mp_playlist_t));
    }
    return count;
}

int mp_play_playlist(int playlist_id)
{
    if (playlist_id < 0 || playlist_id >= MP_MAX_PLAYLISTS || !mp.playlists[playlist_id].used) return -1;
    mp.current_playlist = playlist_id;
    if (mp.playlists[playlist_id].track_count > 0)
        return mp_play(mp.playlists[playlist_id].track_ids[0]);
    return -1;
}

/* 歌词 */
int mp_lyrics_load(int track_id, const char *lyrics_text)
{
    (void)lyrics_text;
    if (track_id < 0 || track_id >= MP_MAX_TRACKS || !mp.tracks[track_id].used) return -1;
    memset(&mp.current_lyrics, 0, sizeof(mp_lyrics_t));
    mp.current_lyrics.used = 1;
    mp.current_lyrics.track_id = track_id;
    strncpy(mp.current_lyrics.title, mp.tracks[track_id].title, 127);
    strncpy(mp.current_lyrics.artist, mp.tracks[track_id].artist, 63);
    /* 模拟歌词 */
    const char *lines[] = {"♪ prev 奏 ♪", " 第一句歌 words ", " 第二句歌 words ", " 副歌部分 ", " 间奏 ", " 第三句歌 words ", " 结尾 ♪"};
    for (int i = 0; i < 7; i++) {
        mp.current_lyrics.lines[i].timestamp = i * 10000;
        strncpy(mp.current_lyrics.lines[i].text, lines[i], 255);
    }
    mp.current_lyrics.line_count = 7;
    return 0;
}

int mp_lyrics_get_current(uint64_t position, char *buf, int size)
{
    if (!buf || size <= 0 || !mp.current_lyrics.used) return -1;
    int current = 0;
    for (int i = 0; i < mp.current_lyrics.line_count; i++) {
        if (position >= mp.current_lyrics.lines[i].timestamp) current = i;
    }
    strncpy(buf, mp.current_lyrics.lines[current].text, size - 1);
    return current;
}

int mp_lyrics_toggle(void)
{
    mp.lyrics_enabled = !mp.lyrics_enabled;
    return mp.lyrics_enabled;
}

int mp_lyrics_set_offset(int offset)
{
    mp.current_lyrics.offset = offset;
    return 0;
}

/* 均衡器 */
int mp_eq_set_band(int band, int gain)
{
    if (band < 0 || band >= 10) return -1;
    if (gain < -12) gain = -12;
    if (gain > 12) gain = 12;
    mp.eq_bands[band].gain = gain;
    mp.eq_preset = 0;  /* 自定义 */
    return 0;
}

int mp_eq_get_band(int band, mp_eq_band_t *eq)
{
    if (band < 0 || band >= 10 || !eq) return -1;
    memcpy(eq, &mp.eq_bands[band], sizeof(mp_eq_band_t));
    return 0;
}

int mp_eq_set_preset(int preset)
{
    if (preset < 0 || preset >= 7) return -1;
    for (int i = 0; i < 10; i++)
        mp.eq_bands[i].gain = eq_presets[preset][i];
    mp.eq_preset = preset;
    return 0;
}

int mp_eq_toggle(void)
{
    mp.eq_enabled = !mp.eq_enabled;
    return mp.eq_enabled;
}

int mp_eq_list_presets(char names[][32], int max)
{
    if (!names || max <= 0) return 0;
    int count = 7 < max ? 7 : max;
    for (int i = 0; i < count; i++)
        strncpy(names[i], eq_preset_names[i], 31);
    return count;
}

/* 音效 */
int mp_set_bass_boost(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    mp.bass_boost = level;
    return 0;
}

int mp_set_virtualizer(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    mp.virtualizer = level;
    return 0;
}

int mp_set_reverb(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    mp.reverb = level;
    return 0;
}

/* 状态查询 */
int mp_get_state(mp_state_t *state)
{
    if (!state) return -1;
    *state = mp.state;
    return 0;
}

int mp_get_current_track(mp_track_t *track)
{
    if (mp.current_track < 0 || !track) return -1;
    return mp_get_track(mp.current_track, track);
}

int mp_get_position(uint64_t *pos)
{
    if (!pos) return -1;
    *pos = mp.current_position;
    return 0;
}

int mp_get_duration(uint64_t *dur)
{
    if (!dur || mp.current_track < 0) return -1;
    *dur = mp.tracks[mp.current_track].duration;
    return 0;
}

int mp_get_stats(music_player_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &mp, sizeof(music_player_t));
    return 0;
}
