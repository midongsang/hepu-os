/* ============================================================================
 * 赫普OS - 汽车互联
 * CarPlay/Android Auto/HiCar + 蓝牙钥匙 + 车机投屏 + 车辆控制
 * ========================================================================== */

#include <stdint.h>
#include "carlink.h"
#include "vga.h"
#include "string.h"

static carlink_t cl;

void carlink_init(void)
{
    memset(&cl, 0, sizeof(carlink_t));
    cl.initialized = 1;
    cl.screen_width = 1920;
    cl.screen_height = 1080;
    cl.screen_dpi = 320;

    /* 默认车辆 */
    cl.vehicles[0].used = 1;
    cl.vehicles[0].id = 0;
    strncpy(cl.vehicles[0].name, " 我 车 ", 63);
    strncpy(cl.vehicles[0].vin, "HEPU2026000000001", 31);
    strncpy(cl.vehicles[0].make, " Hepu ", 31);
    strncpy(cl.vehicles[0].model, "Hepu EV", 31);
    cl.vehicles[0].year = 2026;
    cl.vehicles[0].fuel_level = 80;
    cl.vehicles[0].battery_level = 75;
    cl.vehicles[0].range_km = 450;
    cl.vehicles[0].odometer_km = 12000.5f;
    cl.vehicles[0].tire_pressure[0] = 2.4f;
    cl.vehicles[0].tire_pressure[1] = 2.4f;
    cl.vehicles[0].tire_pressure[2] = 2.4f;
    cl.vehicles[0].tire_pressure[3] = 2.4f;
    cl.vehicles[0].door_locked = 1;
    cl.vehicle_count = 1;
    cl.active_vehicle = 0;

    /* 蓝牙钥匙默认未配对 */
    cl.ble_key.enabled = 0;
    cl.ble_key.key_type = 1;  /* 数字钥匙 */

    /* 默认车机应用 */
    const char *app_names[] = {" 导航 ", " 音乐 ", " 电话 ", " 信息 ", " 播客 ", " has 声书 ", " 停车 ", " 充电 "};
    for (int i = 0; i < 8; i++) {
        cl.apps[i].used = 1;
        cl.apps[i].id = i;
        strncpy(cl.apps[i].name, app_names[i], 63);
        cl.apps[i].category = i;
        cl.apps[i].installed = 1;
    }
    cl.app_count = 8;

    vga_puts("[CARLINK] carlink init done \n");
    vga_printf(" supports: CarPlay/Android Auto/HiCar/CarLife/ Hepu Link, BT key, vehicle: %d vehicles \n",
               cl.vehicle_count);
}

int carlink_connect(carlink_type_t type, carlink_protocol_t protocol)
{
    cl.connected = 1;
    cl.link_type = type;
    cl.protocol = protocol;
    vga_printf("[CARLINK] connected(type=%d, protocol=%d)\n", type, protocol);
    return 0;
}

int carlink_disconnect(void)
{
    cl.connected = 0;
    cl.mirroring = 0;
    return 0;
}

int carlink_is_connected(void) { return cl.connected; }
int carlink_get_protocol(void) { return cl.protocol; }

int carlink_add_vehicle(vehicle_info_t *vehicle)
{
    if (!vehicle) return -1;
    for (int i = 0; i < CARLINK_MAX_VEHICLES; i++) {
        if (!cl.vehicles[i].used) {
            memcpy(&cl.vehicles[i], vehicle, sizeof(vehicle_info_t));
            cl.vehicles[i].used = 1;
            cl.vehicles[i].id = i;
            cl.vehicle_count++;
            return i;
        }
    }
    return -1;
}

int carlink_remove_vehicle(int vehicle_id)
{
    if (vehicle_id < 0 || vehicle_id >= CARLINK_MAX_VEHICLES || !cl.vehicles[vehicle_id].used) return -1;
    cl.vehicles[vehicle_id].used = 0;
    cl.vehicle_count--;
    return 0;
}

int carlink_set_active_vehicle(int vehicle_id)
{
    if (vehicle_id < 0 || vehicle_id >= CARLINK_MAX_VEHICLES || !cl.vehicles[vehicle_id].used) return -1;
    cl.active_vehicle = vehicle_id;
    return 0;
}

int carlink_get_vehicle(int vehicle_id, vehicle_info_t *vehicle)
{
    if (vehicle_id < 0 || vehicle_id >= CARLINK_MAX_VEHICLES || !cl.vehicles[vehicle_id].used || !vehicle) return -1;
    memcpy(vehicle, &cl.vehicles[vehicle_id], sizeof(vehicle_info_t));
    return 0;
}

int carlink_list_vehicles(vehicle_info_t *vehicles, int max)
{
    if (!vehicles || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CARLINK_MAX_VEHICLES && count < max; i++) {
        if (cl.vehicles[i].used) memcpy(&vehicles[count++], &cl.vehicles[i], sizeof(vehicle_info_t));
    }
    return count;
}

int carlink_update_vehicle_status(int vehicle_id, vehicle_info_t *status)
{
    if (vehicle_id < 0 || vehicle_id >= CARLINK_MAX_VEHICLES || !cl.vehicles[vehicle_id].used || !status) return -1;
    /* 只更新状态字段，不覆盖基本信息 */
    cl.vehicles[vehicle_id].state = status->state;
    cl.vehicles[vehicle_id].fuel_level = status->fuel_level;
    cl.vehicles[vehicle_id].battery_level = status->battery_level;
    cl.vehicles[vehicle_id].range_km = status->range_km;
    cl.vehicles[vehicle_id].door_locked = status->door_locked;
    cl.vehicles[vehicle_id].engine_on = status->engine_on;
    return 0;
}

int carlink_ble_key_enable(int enable)
{
    cl.ble_key.enabled = enable ? 1 : 0;
    vga_printf("[CARLINK] BT key %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int carlink_ble_key_pair(const char *vin)
{
    if (!vin) return -1;
    strncpy(cl.ble_key.vehicle_vin, vin, 31);
    cl.ble_key.paired = 1;
    cl.ble_key.authenticated = 1;
    cl.ble_key.in_range = 1;
    cl.ble_key.distance_m = 1.5f;
    vga_puts("[CARLINK] BT key pairing ok \n");
    return 0;
}

int carlink_ble_key_unpair(void)
{
    cl.ble_key.paired = 0;
    cl.ble_key.authenticated = 0;
    return 0;
}

int carlink_ble_key_authenticate(void)
{
    if (!cl.ble_key.paired) return -1;
    cl.ble_key.authenticated = 1;
    return 0;
}

int carlink_ble_key_get_status(ble_key_t *key)
{
    if (!key) return -1;
    memcpy(key, &cl.ble_key, sizeof(ble_key_t));
    return 0;
}

int carlink_ble_key_lock(int lock)
{
    if (!cl.ble_key.authenticated) return -1;
    cl.vehicles[cl.active_vehicle].door_locked = lock ? 1 : 0;
    vga_printf("[CARLINK] vehicle %s\n", lock ? " 锁车 " : " unlock ");
    return 0;
}

int carlink_ble_key_start_engine(int start)
{
    if (!cl.ble_key.authenticated) return -1;
    cl.vehicles[cl.active_vehicle].engine_on = start ? 1 : 0;
    vga_printf("[CARLINK] engine %s\n", start ? " started " : " close ");
    return 0;
}

int carlink_ble_key_open_trunk(void)
{
    if (!cl.ble_key.authenticated) return -1;
    vga_puts("[CARLINK] trunk open \n");
    return 0;
}

int carlink_ble_key_honk_flash(void)
{
    if (!cl.ble_key.authenticated) return -1;
    vga_puts("[CARLINK] horn & light \n");
    return 0;
}

int carlink_ble_key_set_climate(int ac, float temp)
{
    if (!cl.ble_key.authenticated) return -1;
    cl.vehicles[cl.active_vehicle].ac_on = ac;
    cl.vehicles[cl.active_vehicle].target_temp = temp;
    return 0;
}

int carlink_app_launch(int app_id)
{
    if (app_id < 0 || app_id >= CARLINK_MAX_APPS || !cl.apps[app_id].used) return -1;
    cl.apps[app_id].running = 1;
    vga_printf("[CARLINK] start app: %s\n", cl.apps[app_id].name);
    return 0;
}

int carlink_app_close(int app_id)
{
    if (app_id < 0 || app_id >= CARLINK_MAX_APPS || !cl.apps[app_id].used) return -1;
    cl.apps[app_id].running = 0;
    return 0;
}

int carlink_app_list(carlink_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CARLINK_MAX_APPS && count < max; i++) {
        if (cl.apps[i].used) memcpy(&apps[count++], &cl.apps[i], sizeof(carlink_app_t));
    }
    return count;
}

int carlink_app_get(int app_id, carlink_app_t *app)
{
    if (app_id < 0 || app_id >= CARLINK_MAX_APPS || !cl.apps[app_id].used || !app) return -1;
    memcpy(app, &cl.apps[app_id], sizeof(carlink_app_t));
    return 0;
}

int carlink_message_receive(carlink_message_t *msg)
{
    if (!msg) return -1;
    for (int i = 0; i < CARLINK_MAX_MESSAGES; i++) {
        if (!cl.messages[i].used) {
            memcpy(&cl.messages[i], msg, sizeof(carlink_message_t));
            cl.messages[i].used = 1;
            cl.messages[i].id = i;
            cl.message_count++;
            cl.message_count_total++;
            return i;
        }
    }
    return -1;
}

int carlink_message_list(carlink_message_t *messages, int max)
{
    if (!messages || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CARLINK_MAX_MESSAGES && count < max; i++) {
        if (cl.messages[i].used) memcpy(&messages[count++], &cl.messages[i], sizeof(carlink_message_t));
    }
    return count;
}

int carlink_message_reply(int message_id, const char *text)
{
    (void)message_id; (void)text;
    return 0;
}

int carlink_message_read(int message_id)
{
    if (message_id < 0 || message_id >= CARLINK_MAX_MESSAGES || !cl.messages[message_id].used) return -1;
    cl.messages[message_id].read = 1;
    return 0;
}

int carlink_contact_add(carlink_contact_t *contact)
{
    if (!contact) return -1;
    for (int i = 0; i < CARLINK_MAX_CONTACTS; i++) {
        if (!cl.contacts[i].used) {
            memcpy(&cl.contacts[i], contact, sizeof(carlink_contact_t));
            cl.contacts[i].used = 1;
            cl.contacts[i].id = i;
            cl.contact_count++;
            return i;
        }
    }
    return -1;
}

int carlink_contact_remove(int contact_id)
{
    if (contact_id < 0 || contact_id >= CARLINK_MAX_CONTACTS || !cl.contacts[contact_id].used) return -1;
    cl.contacts[contact_id].used = 0;
    cl.contact_count--;
    return 0;
}

int carlink_contact_list(carlink_contact_t *contacts, int max)
{
    if (!contacts || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CARLINK_MAX_CONTACTS && count < max; i++) {
        if (cl.contacts[i].used) memcpy(&contacts[count++], &cl.contacts[i], sizeof(carlink_contact_t));
    }
    return count;
}

int carlink_contact_call(int contact_id)
{
    if (contact_id < 0 || contact_id >= CARLINK_MAX_CONTACTS || !cl.contacts[contact_id].used) return -1;
    cl.call_count++;
    vga_printf("[CARLINK] call: %s\n", cl.contacts[contact_id].name);
    return 0;
}

int carlink_nav_start(const char *destination, double lat, double lng)
{
    (void)lat; (void)lng;
    cl.navigation.active = 1;
    if (destination) strncpy(cl.navigation.destination, destination, 127);
    cl.navigation.eta_seconds = 1800;
    cl.navigation.total_distance_m = 25000;
    vga_printf("[CARLINK] nav start: %s\n", destination);
    cl.navigation_sessions++;
    return 0;
}

int carlink_nav_stop(void)
{
    cl.navigation.active = 0;
    return 0;
}

int carlink_nav_get_status(carlink_nav_t *nav)
{
    if (!nav) return -1;
    memcpy(nav, &cl.navigation, sizeof(carlink_nav_t));
    return 0;
}

int carlink_nav_is_active(void) { return cl.navigation.active; }

int carlink_media_play(void) { cl.vehicles[cl.active_vehicle].media_playing = 1; return 0; }
int carlink_media_pause(void) { cl.vehicles[cl.active_vehicle].media_playing = 0; return 0; }
int carlink_media_next(void) { return 0; }
int carlink_media_prev(void) { return 0; }
int carlink_media_seek(int position_ms) { cl.vehicles[cl.active_vehicle].media_position = position_ms; return 0; }
int carlink_media_set_volume(int volume) { cl.vehicles[cl.active_vehicle].volume = volume; return 0; }

int carlink_media_get_info(char *title, int title_size, char *artist, int artist_size, int *position, int *duration)
{
    if (title) strncpy(title, cl.vehicles[cl.active_vehicle].media_title, title_size - 1);
    if (artist) strncpy(artist, cl.vehicles[cl.active_vehicle].media_artist, artist_size - 1);
    if (position) *position = cl.vehicles[cl.active_vehicle].media_position;
    if (duration) *duration = cl.vehicles[cl.active_vehicle].media_duration;
    return 0;
}

int carlink_call_start(const char *number)
{
    (void)number;
    cl.call_count++;
    return 0;
}

int carlink_call_answer(void) { return 0; }
int carlink_call_end(void) { return 0; }
int carlink_call_mute(int mute) { (void)mute; return 0; }
int carlink_call_speaker(int speaker) { (void)speaker; return 0; }

int carlink_mirror_start(int width, int height, int dpi)
{
    cl.mirroring = 1;
    cl.screen_width = width;
    cl.screen_height = height;
    cl.screen_dpi = dpi;
    vga_puts("[CARLINK] screen cast start \n");
    return 0;
}

int carlink_mirror_stop(void)
{
    cl.mirroring = 0;
    return 0;
}

int carlink_mirror_is_active(void) { return cl.mirroring; }
int carlink_set_dark_mode(int dark) { cl.dark_mode = dark; return 0; }

int carlink_get_stats(uint64_t *connect_time, int *calls, int *messages, int *nav_sessions)
{
    if (connect_time) *connect_time = cl.connect_time;
    if (calls) *calls = cl.call_count;
    if (messages) *messages = cl.message_count_total;
    if (nav_sessions) *nav_sessions = cl.navigation_sessions;
    return 0;
}
