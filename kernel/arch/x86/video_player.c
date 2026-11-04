/* ============================================================================
 * 赫普OS - 视频播放器
 * ========================================================================== */

#include <stdint.h>
#include "video_player.h"
#include "vga.h"
#include "string.h"

static video_player_t vp;

void video_player_init(void)
{
    memset(&vp, 0, sizeof(video_player_t));
    vp.initialized = 1;
    vp.enabled = 1;
    vp.state = VP_STATE_STOPPED;
    vp.volume = 80;
    vp.playback_speed = 1.0f;
    vp.brightness = 50;
    vp.contrast = 50;
    vp.saturation = 50;
    vp.aspect_ratio = 0;
    vp.current_video = -1;
    vp.current_playlist = -1;
    vp.current_subtitle = -1;

    /* 测试视频 */
    vp_add_video("/videos/movie1.mp4", " 示例电影 ");
    vp_add_video("/videos/tutorial.mp4", " 教程 video ");
    vp_add_video("/videos/music_video.mkv", " 音乐 MV");

    vga_puts("[VIDEO] video player init done \n");
    vga_printf(" video: %d, subtitle: %d \n", vp.video_count, vp.subtitle_count);
}

/* 播放控制 */
int vp_play(int video_id)
{
    if (video_id < 0 || video_id >= VP_MAX_VIDEOS || !vp.videos[video_id].used) return -1;
    vp.current_video = video_id;
    vp.state = VP_STATE_PLAYING;
    vp.current_position = vp.videos[video_id].resume_position;
    vp.videos[video_id].play_count++;
    vp.videos[video_id].last_played = 1;
    vp.total_plays++;
    return 0;
}

int vp_pause(void)
{
    if (vp.state != VP_STATE_PLAYING) return -1;
    vp.state = VP_STATE_PAUSED;
    if (vp.current_video >= 0)
        vp.videos[vp.current_video].resume_position = vp.current_position;
    return 0;
}

int vp_resume(void)
{
    if (vp.state != VP_STATE_PAUSED) return -1;
    vp.state = VP_STATE_PLAYING;
    return 0;
}

int vp_stop(void)
{
    if (vp.current_video >= 0)
        vp.videos[vp.current_video].resume_position = vp.current_position;
    vp.state = VP_STATE_STOPPED;
    vp.current_position = 0;
    return 0;
}

int vp_next(void)
{
    if (vp.current_video < 0) return -1;
    int next = vp.current_video + 1;
    if (next >= vp.video_count) next = 0;
    return vp_play(next);
}

int vp_prev(void)
{
    if (vp.current_video < 0) return -1;
    if (vp.current_position > 5000) {
        vp.current_position = 0;
        return 0;
    }
    int prev = vp.current_video - 1;
    if (prev < 0) prev = vp.video_count - 1;
    return vp_play(prev);
}

int vp_seek(uint64_t position)
{
    if (vp.current_video < 0) return -1;
    if (position > vp.videos[vp.current_video].duration) return -1;
    vp.current_position = position;
    return 0;
}

int vp_seek_relative(int64_t offset)
{
    if (vp.current_video < 0) return -1;
    int64_t new_pos = (int64_t)vp.current_position + offset;
    if (new_pos < 0) new_pos = 0;
    if (new_pos > (int64_t)vp.videos[vp.current_video].duration)
        new_pos = vp.videos[vp.current_video].duration;
    vp.current_position = (uint64_t)new_pos;
    return 0;
}

int vp_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    vp.volume = volume;
    return 0;
}

int vp_set_mute(int mute)
{
    vp.muted = mute ? 1 : 0;
    return 0;
}

int vp_set_speed(float speed)
{
    if (speed < 0.25f) speed = 0.25f;
    if (speed > 4.0f) speed = 4.0f;
    vp.playback_speed = speed;
    return 0;
}

/* 媒体库 */
int vp_add_video(const char *path, const char *title)
{
    for (int i = 0; i < VP_MAX_VIDEOS; i++) {
        if (!vp.videos[i].used) {
            memset(&vp.videos[i], 0, sizeof(vp_video_t));
            vp.videos[i].used = 1;
            vp.videos[i].id = i;
            if (path) strncpy(vp.videos[i].path, path, 255);
            if (title) strncpy(vp.videos[i].title, title, 127);
            vp.videos[i].format = VP_FORMAT_MP4;
            vp.videos[i].video_codec = VP_CODEC_H264;
            vp.videos[i].width = 1920;
            vp.videos[i].height = 1080;
            vp.videos[i].fps = 30;
            vp.videos[i].duration = 600000 + i * 120000;  /* 10-14分钟 */
            vp.videos[i].size = 500 * 1024 * 1024;
            vp.videos[i].bitrate = 5000;
            vp.videos[i].audio_channels = 2;
            vp.videos[i].audio_sample_rate = 48000;
            vp.video_count++;
            vp.total_videos++;
            return i;
        }
    }
    return -1;
}

int vp_remove_video(int id)
{
    if (id < 0 || id >= VP_MAX_VIDEOS || !vp.videos[id].used) return -1;
    vp.videos[id].used = 0;
    vp.video_count--;
    return 0;
}

int vp_get_video(int id, vp_video_t *video)
{
    if (id < 0 || id >= VP_MAX_VIDEOS || !vp.videos[id].used || !video) return -1;
    memcpy(video, &vp.videos[id], sizeof(vp_video_t));
    return 0;
}

int vp_list_videos(vp_video_t *videos, int max)
{
    if (!videos || max <= 0) return 0;
    int count = vp.video_count < max ? vp.video_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&videos[i], &vp.videos[i], sizeof(vp_video_t));
    return count;
}

int vp_search(const char *query, vp_video_t *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < VP_MAX_VIDEOS && count < max; i++) {
        if (vp.videos[i].used) {
            int match = 1;
            int qlen = 0; while (query[qlen]) qlen++;
            for (int j = 0; j < qlen; j++) {
                int found = 0;
                for (int k = 0; vp.videos[i].title[k]; k++)
                    if (vp.videos[i].title[k] == query[j]) { found = 1; break; }
                if (!found) { match = 0; break; }
            }
            if (match) memcpy(&results[count++], &vp.videos[i], sizeof(vp_video_t));
        }
    }
    return count;
}

int vp_scan_directory(const char *path)
{
    (void)path;
    return 0;
}

/* 播放列表 */
int vp_playlist_create(const char *name)
{
    for (int i = 0; i < VP_MAX_PLAYLISTS; i++) {
        if (!vp.playlists[i].used) {
            memset(&vp.playlists[i], 0, sizeof(vp_playlist_t));
            vp.playlists[i].used = 1;
            vp.playlists[i].id = i;
            if (name) strncpy(vp.playlists[i].name, name, 63);
            vp.playlist_count++;
            return i;
        }
    }
    return -1;
}

int vp_playlist_delete(int id)
{
    if (id < 0 || id >= VP_MAX_PLAYLISTS || !vp.playlists[id].used) return -1;
    vp.playlists[id].used = 0;
    vp.playlist_count--;
    return 0;
}

int vp_playlist_add(int playlist_id, int video_id)
{
    if (playlist_id < 0 || playlist_id >= VP_MAX_PLAYLISTS || !vp.playlists[playlist_id].used) return -1;
    if (video_id < 0 || video_id >= VP_MAX_VIDEOS || !vp.videos[video_id].used) return -1;
    if (vp.playlists[playlist_id].video_count < 128) {
        vp.playlists[playlist_id].video_ids[vp.playlists[playlist_id].video_count++] = video_id;
        return 0;
    }
    return -1;
}

int vp_playlist_remove(int playlist_id, int video_id)
{
    if (playlist_id < 0 || playlist_id >= VP_MAX_PLAYLISTS || !vp.playlists[playlist_id].used) return -1;
    for (int i = 0; i < vp.playlists[playlist_id].video_count; i++) {
        if (vp.playlists[playlist_id].video_ids[i] == video_id) {
            for (int j = i; j < vp.playlists[playlist_id].video_count - 1; j++)
                vp.playlists[playlist_id].video_ids[j] = vp.playlists[playlist_id].video_ids[j+1];
            vp.playlists[playlist_id].video_count--;
            return 0;
        }
    }
    return -1;
}

int vp_playlist_get(int id, vp_playlist_t *pl)
{
    if (id < 0 || id >= VP_MAX_PLAYLISTS || !vp.playlists[id].used || !pl) return -1;
    memcpy(pl, &vp.playlists[id], sizeof(vp_playlist_t));
    return 0;
}

int vp_playlist_list(vp_playlist_t *playlists, int max)
{
    if (!playlists || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < VP_MAX_PLAYLISTS && count < max; i++) {
        if (vp.playlists[i].used) memcpy(&playlists[count++], &vp.playlists[i], sizeof(vp_playlist_t));
    }
    return count;
}

int vp_play_playlist(int playlist_id)
{
    if (playlist_id < 0 || playlist_id >= VP_MAX_PLAYLISTS || !vp.playlists[playlist_id].used) return -1;
    vp.current_playlist = playlist_id;
    if (vp.playlists[playlist_id].video_count > 0)
        return vp_play(vp.playlists[playlist_id].video_ids[0]);
    return -1;
}

/* 字幕 */
int vp_subtitle_load(const char *path, const char *language)
{
    for (int i = 0; i < VP_MAX_SUBTITLES; i++) {
        if (!vp.subtitles[i].used) {
            memset(&vp.subtitles[i], 0, sizeof(vp_subtitle_t));
            vp.subtitles[i].used = 1;
            vp.subtitles[i].id = i;
            if (path) strncpy(vp.subtitles[i].path, path, 255);
            if (language) strncpy(vp.subtitles[i].language, language, 15);
            vp.subtitles[i].format = 0;  /* SRT */
            vp.subtitle_count++;
            return i;
        }
    }
    return -1;
}

int vp_subtitle_select(int id)
{
    if (id < 0 || id >= VP_MAX_SUBTITLES || !vp.subtitles[id].used) return -1;
    for (int i = 0; i < VP_MAX_SUBTITLES; i++) vp.subtitles[i].enabled = 0;
    vp.subtitles[id].enabled = 1;
    vp.current_subtitle = id;
    return 0;
}

int vp_subtitle_toggle(void)
{
    if (vp.current_subtitle < 0) return -1;
    vp.subtitles[vp.current_subtitle].enabled = !vp.subtitles[vp.current_subtitle].enabled;
    return vp.subtitles[vp.current_subtitle].enabled;
}

int vp_subtitle_set_delay(int delay_ms)
{
    if (vp.current_subtitle < 0) return -1;
    vp.subtitles[vp.current_subtitle].delay = delay_ms;
    return 0;
}

int vp_subtitle_get_current(uint64_t position, char *buf, int size)
{
    (void)position;
    if (!buf || size <= 0 || vp.current_subtitle < 0) return -1;
    if (!vp.subtitles[vp.current_subtitle].enabled) return -1;
    /* 简化: 返回固定字幕 */
    strncpy(buf, " 示例 subtitle text ", size - 1);
    return 0;
}

int vp_subtitle_list(vp_subtitle_t *subs, int max)
{
    if (!subs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < VP_MAX_SUBTITLES && count < max; i++) {
        if (vp.subtitles[i].used) memcpy(&subs[count++], &vp.subtitles[i], sizeof(vp_subtitle_t));
    }
    return count;
}

/* 显示设置 */
int vp_set_fullscreen(int fullscreen)
{
    vp.fullscreen = fullscreen ? 1 : 0;
    return 0;
}

int vp_set_aspect_ratio(int ratio)
{
    vp.aspect_ratio = ratio;
    return 0;
}

int vp_set_rotation(int degrees)
{
    vp.rotation = degrees % 360;
    return 0;
}

int vp_set_brightness(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    vp.brightness = level;
    return 0;
}

int vp_set_contrast(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    vp.contrast = level;
    return 0;
}

int vp_set_saturation(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    vp.saturation = level;
    return 0;
}

/* 状态查询 */
int vp_get_state(vp_state_t *state)
{
    if (!state) return -1;
    *state = vp.state;
    return 0;
}

int vp_get_current_video(vp_video_t *video)
{
    if (vp.current_video < 0 || !video) return -1;
    return vp_get_video(vp.current_video, video);
}

int vp_get_position(uint64_t *pos)
{
    if (!pos) return -1;
    *pos = vp.current_position;
    return 0;
}

int vp_get_duration(uint64_t *dur)
{
    if (!dur || vp.current_video < 0) return -1;
    *dur = vp.videos[vp.current_video].duration;
    return 0;
}

int vp_get_stats(video_player_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &vp, sizeof(video_player_t));
    return 0;
}
