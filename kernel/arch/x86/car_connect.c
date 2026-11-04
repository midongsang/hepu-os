/* ============================================================================
 * 赫普OS - 车机互联
 * ========================================================================== */

#include <stdint.h>
#include "car_connect.h"
#include "vga.h"
#include "string.h"

static car_connect_t car;

void car_connect_init(void)
{
    memset(&car, 0, sizeof(car_connect_t));
    car.initialized = 1;
    car.enabled = 1;
    car.config.auto_connect = 1;
    car.config.auto_project = 1;
    car.config.default_proj = CAR_PROJ_FULL;
    car.config.driver_mode = 1;
    car.config.voice_control = 1;
    car.config.steering_wheel_controls = 1;
    car.config.notification_filter = 1;

    vga_puts("[CAR] car connect init done \n");
    vga_printf(" auto connect: %s, voice control: %s\n",
               car.config.auto_connect ? " on " : " close ",
               car.config.voice_control ? " on " : " close ");
}

int car_pair(car_conn_type_t type, const char *device_id)
{
    (void)device_id;
    for (int i = 0; i < CAR_MAX_VEHICLES; i++) {
        if (!car.vehicles[i].used) {
            memset(&car.vehicles[i], 0, sizeof(car_vehicle_t));
            car.vehicles[i].used = 1;
            car.vehicles[i].id = i;
            strncpy(car.vehicles[i].name, " Hepu 汽车 ", 127);
            strncpy(car.vehicles[i].brand, " Hepu ", 63);
            strncpy(car.vehicles[i].model, "HEPU-Car", 63);
            strncpy(car.vehicles[i].vin, "HEPU0000000000001", 19);
            car.vehicles[i].year = 2026;
            car.vehicles[i].type = CAR_VEH_BEV;
            car.vehicles[i].conn_type = type;
            car.vehicles[i].state = CAR_STATE_PAIRING;
            car.vehicles[i].battery_level = 85.0f;
            car.vehicles[i].range_km = 450.0f;
            car.vehicle_count++;
            return i;
        }
    }
    return -1;
}

int car_connect(int vehicle_id)
{
    if (vehicle_id < 0 || vehicle_id >= CAR_MAX_VEHICLES || !car.vehicles[vehicle_id].used) return -1;
    car.vehicles[vehicle_id].state = CAR_STATE_CONNECTED;
    car.active_vehicle = vehicle_id;
    car.connect_count++;
    if (car.config.auto_project) car_project_start(car.config.default_proj);
    return 0;
}

int car_disconnect(int vehicle_id)
{
    if (vehicle_id < 0 || vehicle_id >= CAR_MAX_VEHICLES || !car.vehicles[vehicle_id].used) return -1;
    if (car.vehicles[vehicle_id].state == CAR_STATE_PROJECTING) car_project_stop();
    car.vehicles[vehicle_id].state = CAR_STATE_DISCONNECTED;
    if (car.active_vehicle == vehicle_id) car.active_vehicle = -1;
    return 0;
}

int car_get_state(car_state_t *state)
{
    if (!state) return -1;
    if (car.active_vehicle >= 0) *state = car.vehicles[car.active_vehicle].state;
    else *state = CAR_STATE_DISCONNECTED;
    return 0;
}

int car_get_vehicle(int id, car_vehicle_t *veh)
{
    if (id < 0 || id >= CAR_MAX_VEHICLES || !car.vehicles[id].used || !veh) return -1;
    memcpy(veh, &car.vehicles[id], sizeof(car_vehicle_t));
    return 0;
}

int car_list_vehicles(car_vehicle_t *vehs, int max)
{
    if (!vehs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CAR_MAX_VEHICLES && count < max; i++) {
        if (car.vehicles[i].used) memcpy(&vehs[count++], &car.vehicles[i], sizeof(car_vehicle_t));
    }
    return count;
}

int car_get_active(car_vehicle_t *veh)
{
    if (car.active_vehicle < 0) return -1;
    return car_get_vehicle(car.active_vehicle, veh);
}

int car_project_start(car_proj_mode_t mode)
{
    if (car.active_vehicle < 0) return -1;
    car.vehicles[car.active_vehicle].state = CAR_STATE_PROJECTING;
    car.vehicles[car.active_vehicle].proj_mode = mode;
    return 0;
}

int car_project_stop(void)
{
    if (car.active_vehicle < 0) return -1;
    car.vehicles[car.active_vehicle].state = CAR_STATE_CONNECTED;
    return 0;
}

int car_project_set_mode(car_proj_mode_t mode)
{
    if (car.active_vehicle < 0) return -1;
    car.vehicles[car.active_vehicle].proj_mode = mode;
    return 0;
}

int car_project_get_mode(car_proj_mode_t *mode)
{
    if (!mode || car.active_vehicle < 0) return -1;
    *mode = car.vehicles[car.active_vehicle].proj_mode;
    return 0;
}

int car_update_vehicle_data(int vehicle_id, const car_vehicle_t *data)
{
    if (vehicle_id < 0 || vehicle_id >= CAR_MAX_VEHICLES || !car.vehicles[vehicle_id].used) return -1;
    if (data) {
        car.vehicles[vehicle_id].speed_kmh = data->speed_kmh;
        car.vehicles[vehicle_id].rpm = data->rpm;
        car.vehicles[vehicle_id].fuel_level = data->fuel_level;
        car.vehicles[vehicle_id].battery_level = data->battery_level;
        car.vehicles[vehicle_id].range_km = data->range_km;
        car.vehicles[vehicle_id].gear = data->gear;
    }
    return 0;
}

int car_get_vehicle_data(int vehicle_id, car_vehicle_t *data)
{
    return car_get_vehicle(vehicle_id, data);
}

int car_get_speed(float *speed)
{
    if (!speed || car.active_vehicle < 0) return -1;
    *speed = car.vehicles[car.active_vehicle].speed_kmh;
    return 0;
}

int car_get_fuel(float *fuel)
{
    if (!fuel || car.active_vehicle < 0) return -1;
    *fuel = car.vehicles[car.active_vehicle].fuel_level;
    return 0;
}

int car_get_battery(float *battery)
{
    if (!battery || car.active_vehicle < 0) return -1;
    *battery = car.vehicles[car.active_vehicle].battery_level;
    return 0;
}

int car_get_range(float *range)
{
    if (!range || car.active_vehicle < 0) return -1;
    *range = car.vehicles[car.active_vehicle].range_km;
    return 0;
}

int car_get_location(float *lat, float *lon, float *heading)
{
    if (car.active_vehicle < 0) return -1;
    if (lat) *lat = car.vehicles[car.active_vehicle].latitude;
    if (lon) *lon = car.vehicles[car.active_vehicle].longitude;
    if (heading) *heading = car.vehicles[car.active_vehicle].heading;
    return 0;
}

int car_app_register(const char *name, const char *package, int category,
                     int car_optimized, int driver_safe)
{
    for (int i = 0; i < CAR_MAX_APPS; i++) {
        if (!car.apps[i].used) {
            memset(&car.apps[i], 0, sizeof(car_app_t));
            car.apps[i].used = 1;
            car.apps[i].id = i;
            if (name) strncpy(car.apps[i].name, name, 127);
            if (package) strncpy(car.apps[i].package, package, 127);
            car.apps[i].category = category;
            car.apps[i].car_optimized = car_optimized;
            car.apps[i].driver_safe = driver_safe;
            car.apps[i].installed = 1;
            car.app_count++;
            return i;
        }
    }
    return -1;
}

int car_app_unregister(int app_id)
{
    if (app_id < 0 || app_id >= CAR_MAX_APPS || !car.apps[app_id].used) return -1;
    car.apps[app_id].used = 0;
    car.app_count--;
    return 0;
}

int car_app_launch(int app_id)
{
    if (app_id < 0 || app_id >= CAR_MAX_APPS || !car.apps[app_id].used) return -1;
    /* 驾驶模式下检查安全限制 */
    if (car.config.driver_mode && !car.apps[app_id].driver_safe) {
        if (car.active_vehicle >= 0 && car.vehicles[car.active_vehicle].speed_kmh > 0)
            return -2;  /* 行驶中禁止 */
    }
    car.apps[app_id].running = 1;
    return 0;
}

int car_app_close(int app_id)
{
    if (app_id < 0 || app_id >= CAR_MAX_APPS || !car.apps[app_id].used) return -1;
    car.apps[app_id].running = 0;
    return 0;
}

int car_app_list(car_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CAR_MAX_APPS && count < max; i++) {
        if (car.apps[i].used) memcpy(&apps[count++], &car.apps[i], sizeof(car_app_t));
    }
    return count;
}

int car_app_get(int app_id, car_app_t *app)
{
    if (app_id < 0 || app_id >= CAR_MAX_APPS || !car.apps[app_id].used || !app) return -1;
    memcpy(app, &car.apps[app_id], sizeof(car_app_t));
    return 0;
}

int car_control_send(car_control_t control, int pressed, int long_press)
{
    int idx = car.event_pos % CAR_MAX_CONTROLS;
    car.events[idx].control = control;
    car.events[idx].pressed = pressed;
    car.events[idx].long_press = long_press;
    car.events[idx].timestamp = 1;
    car.event_pos++;
    if (car.event_count < CAR_MAX_CONTROLS) car.event_count++;
    car.control_events++;
    return 0;
}

int car_control_poll(car_control_event_t *event)
{
    if (!event || car.event_count == 0) return -1;
    static int poll_pos = 0;
    if (poll_pos >= car.event_pos) poll_pos = 0;
    int idx = poll_pos % CAR_MAX_CONTROLS;
    memcpy(event, &car.events[idx], sizeof(car_control_event_t));
    poll_pos++;
    return 0;
}

int car_control_register_handler(car_control_t control, void (*handler)(int))
{
    (void)control; (void)handler;
    return 0;
}

int car_voice_start(void)
{
    return 0;
}

int car_voice_stop(void)
{
    return 0;
}

int car_voice_process(const char *text, car_voice_cmd_t *cmd)
{
    if (!cmd) return -1;
    memset(cmd, 0, sizeof(car_voice_cmd_t));
    if (text) strncpy(cmd->command, text, 255);
    cmd->confidence = 0.95f;
    cmd->handled = 1;
    car.voice_commands++;
    return 0;
}

int car_voice_tts(const char *text)
{
    (void)text;
    return 0;
}

int car_notification_show(const char *title, const char *text, int type)
{
    (void)title; (void)text; (void)type;
    return 0;
}

int car_notification_dismiss(int id)
{
    (void)id;
    return 0;
}

int car_set_config(const car_config_t *config)
{
    if (!config) return -1;
    memcpy(&car.config, config, sizeof(car_config_t));
    return 0;
}

int car_get_config(car_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &car.config, sizeof(car_config_t));
    return 0;
}

int car_set_driver_mode(int enable)
{
    car.config.driver_mode = enable ? 1 : 0;
    return 0;
}

int car_set_night_mode(int enable)
{
    car.config.night_mode = enable ? 1 : 0;
    return 0;
}

int car_set_voice_control(int enable)
{
    car.config.voice_control = enable ? 1 : 0;
    return 0;
}

int car_get_stats(car_connect_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &car, sizeof(car_connect_t));
    return 0;
}
