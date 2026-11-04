/* ============================================================================
 * 赫普OS - MIDI支持（从audio2.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "audio2.h"
#include "vga.h"
#include "string.h"

extern audio2_t a2;

int audio2_midi_connect(const char *name)
{
    for (int i = 0; i < A2_MAX_MIDI_DEVICES; i++) {
        if (!a2.midi_devices[i].used) {
            memset(&a2.midi_devices[i], 0, sizeof(a2_midi_device_t));
            a2.midi_devices[i].used = 1;
            a2.midi_devices[i].id = i;
            if (name) strncpy(a2.midi_devices[i].name, name, 63);
            a2.midi_devices[i].connected = 1;
            a2.midi_devices[i].channels = 16;
            a2.midi_device_count++;
            vga_printf("[AUDIO2] MIDI device connect: %s\n", name ? name : "unknown");
            return i;
        }
    }
    return -1;
}

int audio2_midi_disconnect(int device_id)
{
    if (device_id < 0 || device_id >= A2_MAX_MIDI_DEVICES || !a2.midi_devices[device_id].used) return -1;
    a2.midi_devices[device_id].used = 0;
    a2.midi_devices[device_id].connected = 0;
    a2.midi_device_count--;
    return 0;
}

int audio2_midi_send_event(int device_id, a2_midi_event_t *event)
{
    (void)event;
    if (device_id < 0 || device_id >= A2_MAX_MIDI_DEVICES || !a2.midi_devices[device_id].used) return -1;
    return 0;
}

int audio2_midi_note_on(int device_id, int channel, int note, int velocity)
{
    (void)channel;
    if (device_id < 0 || device_id >= A2_MAX_MIDI_DEVICES || !a2.midi_devices[device_id].used) return -1;
    if (note < 0 || note >= A2_MAX_MIDI_NOTES) return -1;
    a2.midi_devices[device_id].notes_active[note] = velocity;
    return 0;
}

int audio2_midi_note_off(int device_id, int channel, int note, int velocity)
{
    (void)channel; (void)velocity;
    if (device_id < 0 || device_id >= A2_MAX_MIDI_DEVICES || !a2.midi_devices[device_id].used) return -1;
    if (note < 0 || note >= A2_MAX_MIDI_NOTES) return -1;
    a2.midi_devices[device_id].notes_active[note] = 0;
    return 0;
}

int audio2_midi_program_change(int device_id, int channel, int program)
{
    (void)channel; (void)program;
    if (device_id < 0 || device_id >= A2_MAX_MIDI_DEVICES || !a2.midi_devices[device_id].used) return -1;
    a2.midi_devices[device_id].program = program;
    return 0;
}

int audio2_midi_control_change(int device_id, int channel, int controller, int value)
{
    (void)channel; (void)controller; (void)value;
    if (device_id < 0 || device_id >= A2_MAX_MIDI_DEVICES || !a2.midi_devices[device_id].used) return -1;
    return 0;
}

int audio2_midi_list_devices(a2_midi_device_t *devices, int max)
{
    if (!devices || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < A2_MAX_MIDI_DEVICES && count < max; i++) {
        if (a2.midi_devices[i].used) memcpy(&devices[count++], &a2.midi_devices[i], sizeof(a2_midi_device_t));
    }
    return count;
}

