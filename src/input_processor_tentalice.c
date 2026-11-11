/*
 * Copyright (c) 2026 Tentalice contributors
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_tentalice

#include <drivers/input_processor.h>
#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zmk/behavior.h>
#include <zmk/keymap.h>

#include <string.h>

bool tentalice_mouse_gesture_enabled(void);

#define SETTINGS_LAYER 6
#define GESTURE_ACTION_BASE 26

enum setting_id {
    SETTING_ROTATION = 0,
    SETTING_MIN_FACTOR,
    SETTING_MAX_FACTOR,
    SETTING_SPEED_THRESHOLD,
    SETTING_SPEED_MAX,
    SETTING_EXPONENT,
    SETTING_TRACK_REMAINDERS,
    SETTING_SNAP_X_NUM,
    SETTING_SNAP_X_DEN,
    SETTING_SNAP_Y_NUM,
    SETTING_SNAP_Y_DEN,
    SETTING_SNAP_XY_NUM,
    SETTING_SNAP_XY_DEN,
    SETTING_SNAP_SAMPLES,
    SETTING_SNAP_IMMEDIATE,
    SETTING_SNAP_LOCK_MS,
    SETTING_SNAP_LOCK_EVENTS,
    SETTING_SNAP_IDLE_MS,
    SETTING_GESTURE_STROKE,
    SETTING_GESTURE_IDLE_MS,
    SETTING_GESTURE_COOLDOWN_MS,
    SETTING_GESTURE_PARTIAL_MS,
    SETTING_GESTURE_LEFT,
    SETTING_GESTURE_RIGHT,
    SETTING_GESTURE_DOWN,
    SETTING_GESTURE_UP,
};

enum processor_mode {
    PROCESSOR_ROTATION,
    PROCESSOR_ACCELERATION,
    PROCESSOR_SCROLL_SNAP,
    PROCESSOR_MOUSE_GESTURE,
};

struct processor_data {
    uint32_t last_time[2];
    int32_t last_delta[2];
    int16_t remainder[2];
    int32_t rotate_x;
    int32_t rotate_y;
    int32_t snap_sample_x[16];
    int32_t snap_sample_y[16];
    int32_t snap_remainder_x;
    int32_t snap_remainder_y;
    uint32_t snap_x;
    uint32_t snap_y;
    uint8_t snap_samples;
    uint8_t snap_head;
    uint8_t snap_capacity;
    uint8_t snap_lock_direction;
    uint16_t snap_lock_events;
    uint32_t snap_lock_expires;
    uint32_t snap_last_event;
    int32_t gesture_x;
    int32_t gesture_y;
    uint8_t gesture_pattern[4];
    uint8_t gesture_length;
    uint32_t last_motion_ms;
    uint32_t cooldown_until;
    struct k_work_delayable gesture_work;
};

struct processor_config {
    uint8_t mode;
};

static uint32_t setting_value(uint8_t id, uint32_t fallback) {
    const struct zmk_behavior_binding *binding =
        zmk_keymap_get_layer_binding_at_idx(SETTINGS_LAYER, id);
    if (!binding || !binding->behavior_dev ||
        strcmp(binding->behavior_dev, "tentalice_setting") || binding->param1 != id) {
        return fallback;
    }
    return binding->param2;
}

static void gesture_invoke(struct processor_data *data) {
    static const uint8_t ids[] = {SETTING_GESTURE_LEFT, SETTING_GESTURE_RIGHT,
                                  SETTING_GESTURE_DOWN, SETTING_GESTURE_UP};
    if (!data->gesture_length) {
        return;
    }
    uint32_t encoded = 0;
    for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
        encoded = setting_value(ids[i], i + 1);
        uint8_t expected[4] = {0};
        uint8_t expected_len = 0;
        while (encoded && expected_len < ARRAY_SIZE(expected)) {
            expected[expected_len++] = encoded % 5;
            encoded /= 5;
        }
        if (expected_len != data->gesture_length ||
            memcmp(expected, data->gesture_pattern, expected_len) != 0) {
            continue;
        }
        const struct zmk_behavior_binding *binding =
            zmk_keymap_get_layer_binding_at_idx(SETTINGS_LAYER, GESTURE_ACTION_BASE + i);
        if (!binding || !binding->behavior_dev) {
            break;
        }
        struct zmk_behavior_binding_event event = {
            .layer = zmk_keymap_highest_layer_active(),
            .position = 0,
            .timestamp = k_uptime_get(),
        };
        zmk_behavior_invoke_binding(binding, event, true);
        zmk_behavior_invoke_binding(binding, event, false);
        data->cooldown_until = k_uptime_get_32() +
                               setting_value(SETTING_GESTURE_COOLDOWN_MS, 10);
        break;
    }
    data->gesture_length = 0;
    data->gesture_x = 0;
    data->gesture_y = 0;
}

static void gesture_timeout(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct processor_data *data = CONTAINER_OF(delayable, struct processor_data, gesture_work);
    gesture_invoke(data);
}

static struct processor_data *gesture_processor;

void tentalice_gesture_finished(void) {
    if (!gesture_processor) {
        return;
    }
    k_work_cancel_delayable(&gesture_processor->gesture_work);
    gesture_invoke(gesture_processor);
}

static uint32_t abs_u32(int32_t value) {
    return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

static int handle_rotation(struct processor_data *data, struct input_event *event) {
    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }
    static const int16_t sin_table[] = {0, 87, 174, 259, 342, 423, 500, 574, 643,
                                         707, 766, 819, 866, 906, 940, 966, 985, 996, 1000};
    static const int16_t cos_table[] = {1000, 996, 985, 966, 940, 906, 866, 819, 766,
                                         707, 643, 574, 500, 423, 342, 259, 174, 87, 0};
    uint32_t angle = setting_value(SETTING_ROTATION, 45) % 360;
    uint32_t index = (angle % 90) / 5;
    int32_t sine = sin_table[index];
    int32_t cosine = cos_table[index];
    switch (angle / 90) {
    case 1: { int32_t temp = sine; sine = cosine; cosine = -temp; break; }
    case 2: sine = -sine; cosine = -cosine; break;
    case 3: { int32_t temp = sine; sine = -cosine; cosine = temp; break; }
    }
    if (event->code == INPUT_REL_X) {
        int32_t x = event->value;
        int32_t y = data->rotate_y;
        event->value = (x * cosine - y * sine) / 1000;
        data->rotate_x = x;
    } else {
        int32_t x = data->rotate_x;
        int32_t y = event->value;
        event->value = (x * sine + y * cosine) / 1000;
        data->rotate_y = y;
    }
    return ZMK_INPUT_PROC_CONTINUE;
}

static uint32_t acceleration_factor(uint32_t speed) {
    uint32_t min_factor = CLAMP(setting_value(SETTING_MIN_FACTOR, 800), 100, 20000);
    uint32_t max_factor = CLAMP(setting_value(SETTING_MAX_FACTOR, 3000), min_factor, 20000);
    uint32_t threshold = MAX(setting_value(SETTING_SPEED_THRESHOLD, 1200), 1);
    uint32_t speed_max = MAX(setting_value(SETTING_SPEED_MAX, 6000), threshold + 1);
    uint32_t exponent = CLAMP(setting_value(SETTING_EXPONENT, 2), 1, 4);
    uint32_t base = MAX(min_factor, 1000);
    uint32_t shaped;
    if (speed <= threshold) {
        shaped = speed * 1000 / threshold;
        uint32_t curve = shaped;
        for (uint32_t i = 1; i < exponent; i++) curve = curve * shaped / 1000;
        return min_factor + ((base - min_factor) * curve / 1000);
    }
    if (speed >= speed_max) return max_factor;
    shaped = (speed - threshold) * 1000 / (speed_max - threshold);
    uint32_t curve = shaped;
    for (uint32_t i = 1; i < exponent; i++) curve = curve * shaped / 1000;
    return base + ((max_factor - base) * curve / 1000);
}

static int handle_acceleration(struct processor_data *data, struct input_event *event) {
    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }
    uint8_t axis = event->code == INPUT_REL_X ? 0 : 1;
    uint32_t now = k_uptime_get_32();
    int32_t raw = event->value;
    if (!raw) {
        data->last_time[axis] = now;
        return ZMK_INPUT_PROC_CONTINUE;
    }
    uint32_t elapsed = data->last_time[axis] && now > data->last_time[axis]
                           ? MIN(now - data->last_time[axis], 100)
                           : 1;
    uint32_t speed = abs_u32(raw) * 1000 / MAX(elapsed, 1);
    uint32_t factor = acceleration_factor(speed);
    if ((int64_t)data->last_delta[axis] * raw < 0 && factor > 1000) factor = 1000;
    if (setting_value(SETTING_TRACK_REMAINDERS, 1)) {
        int64_t total = (int64_t)raw * factor + data->remainder[axis];
        event->value = total / 1000;
        data->remainder[axis] = total - (int64_t)event->value * 1000;
    } else {
        event->value = (int64_t)raw * factor / 1000;
    }
    data->last_delta[axis] = raw;
    data->last_time[axis] = now;
    return ZMK_INPUT_PROC_CONTINUE;
}

static int handle_scroll_snap(struct processor_data *data, struct input_event *event) {
    bool x_axis = event->code == INPUT_REL_HWHEEL;
    bool y_axis = event->code == INPUT_REL_WHEEL;
    if (event->type != INPUT_EV_REL || (!x_axis && !y_axis)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }
    uint32_t now = k_uptime_get_32();
    uint32_t idle_timeout = setting_value(SETTING_SNAP_IDLE_MS, 200);
    if (idle_timeout && data->snap_last_event && now - data->snap_last_event >= idle_timeout) {
        data->snap_x = data->snap_y = 0;
        data->snap_remainder_x = data->snap_remainder_y = 0;
        data->snap_samples = data->snap_head = 0;
        data->snap_lock_direction = 0;
        data->snap_lock_events = 0;
        data->snap_lock_expires = 0;
    }
    data->snap_last_event = now;
    uint32_t required = CLAMP(setting_value(SETTING_SNAP_SAMPLES, 10), 1, 16);
    if (data->snap_capacity != required) {
        data->snap_capacity = required;
        data->snap_x = data->snap_y = 0;
        data->snap_remainder_x = data->snap_remainder_y = 0;
        data->snap_samples = data->snap_head = 0;
        data->snap_lock_direction = 0;
        data->snap_lock_events = 0;
        data->snap_lock_expires = 0;
    }

    if (data->snap_lock_direction && data->snap_lock_expires &&
        now >= data->snap_lock_expires) {
        data->snap_lock_direction = 0;
        data->snap_lock_events = 0;
        data->snap_lock_expires = 0;
    }

    int32_t incoming_x = x_axis ? event->value : 0;
    int32_t incoming_y = y_axis ? event->value : 0;
    if (data->snap_samples >= required) {
        data->snap_x -= abs_u32(data->snap_sample_x[data->snap_head]);
        data->snap_y -= abs_u32(data->snap_sample_y[data->snap_head]);
    } else {
        data->snap_samples++;
    }
    data->snap_sample_x[data->snap_head] = incoming_x;
    data->snap_sample_y[data->snap_head] = incoming_y;
    data->snap_x += abs_u32(incoming_x);
    data->snap_y += abs_u32(incoming_y);
    data->snap_remainder_x += incoming_x;
    data->snap_remainder_y += incoming_y;
    data->snap_head = (data->snap_head + 1) % required;

    uint32_t immediate = setting_value(SETTING_SNAP_IMMEDIATE, 1500);
    if (data->snap_samples < required && data->snap_x <= immediate && data->snap_y <= immediate) {
        event->value = 0;
        event->sync = false;
        return ZMK_INPUT_PROC_STOP;
    }
    uint32_t x_num = MAX(setting_value(SETTING_SNAP_X_NUM, 5), 1);
    uint32_t x_den = MAX(setting_value(SETTING_SNAP_X_DEN, 8), 1);
    uint32_t y_num = MAX(setting_value(SETTING_SNAP_Y_NUM, 8), 1);
    uint32_t y_den = MAX(setting_value(SETTING_SNAP_Y_DEN, 5), 1);
    uint8_t detected_direction = 0;
    if ((uint64_t)data->snap_y * y_den > (uint64_t)data->snap_x * y_num) {
        detected_direction = 2;
    } else if ((uint64_t)data->snap_y * x_den < (uint64_t)data->snap_x * x_num) {
        detected_direction = 1;
    }
    bool lock_active = data->snap_lock_direction &&
                       ((data->snap_lock_expires && now < data->snap_lock_expires) ||
                        data->snap_lock_events);
    uint8_t direction = lock_active ? data->snap_lock_direction : detected_direction;
    int32_t output = direction == 1 ? data->snap_remainder_x
                                    : direction == 2 ? data->snap_remainder_y : 0;
    data->snap_x = data->snap_y = 0;
    data->snap_remainder_x = data->snap_remainder_y = 0;
    data->snap_samples = data->snap_head = 0;
    if (direction == 1) {
        event->code = INPUT_REL_HWHEEL;
    } else if (direction == 2) {
        event->code = INPUT_REL_WHEEL;
    }
    event->value = output;

    uint32_t lock_ms = setting_value(SETTING_SNAP_LOCK_MS, 200);
    uint16_t lock_events = setting_value(SETTING_SNAP_LOCK_EVENTS, 10);
    if (lock_active) {
        if (detected_direction == data->snap_lock_direction) {
            data->snap_lock_expires = lock_ms ? now + lock_ms : 0;
            data->snap_lock_events = lock_events;
        } else if (!lock_ms && data->snap_lock_events) {
            data->snap_lock_events--;
            if (!data->snap_lock_events) data->snap_lock_direction = 0;
        }
    } else if (direction && (lock_ms || lock_events)) {
        data->snap_lock_direction = direction;
        data->snap_lock_expires = lock_ms ? now + lock_ms : 0;
        data->snap_lock_events = lock_events;
    }
    return ZMK_INPUT_PROC_CONTINUE;
}

static void gesture_collect(struct processor_data *data, struct input_event *event) {
    if (!tentalice_mouse_gesture_enabled() || event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) return;
    uint32_t now = k_uptime_get_32();
    uint32_t partial_timeout = setting_value(SETTING_GESTURE_PARTIAL_MS, 400);
    if (partial_timeout && data->last_motion_ms &&
        now - data->last_motion_ms >= partial_timeout) {
        data->gesture_length = 0;
        data->gesture_x = data->gesture_y = 0;
    }
    data->last_motion_ms = now;
    if ((int32_t)(now - data->cooldown_until) < 0) {
        return;
    }
    if (event->code == INPUT_REL_X) data->gesture_x += event->value;
    if (event->code == INPUT_REL_Y) data->gesture_y += event->value;
    uint32_t stroke = MAX(setting_value(SETTING_GESTURE_STROKE, 50), 1);
    if (MAX(abs_u32(data->gesture_x), abs_u32(data->gesture_y)) < stroke) return;
    uint8_t direction;
    if (abs_u32(data->gesture_x) >= abs_u32(data->gesture_y)) {
        direction = data->gesture_x < 0 ? 1 : 2;
    } else {
        direction = data->gesture_y < 0 ? 4 : 3;
    }
    if (!data->gesture_length || data->gesture_pattern[data->gesture_length - 1] != direction) {
        if (data->gesture_length < ARRAY_SIZE(data->gesture_pattern)) {
            data->gesture_pattern[data->gesture_length++] = direction;
        }
    }
    data->gesture_x = data->gesture_y = 0;
    uint32_t timeout = setting_value(SETTING_GESTURE_IDLE_MS, 0);
    if (timeout) {
        k_work_reschedule(&data->gesture_work, K_MSEC(timeout));
    }
}

static int tentalice_handle_event(const struct device *dev, struct input_event *event,
                                  uint32_t param1, uint32_t param2,
                                  struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);
    const struct processor_config *config = dev->config;
    struct processor_data *data = dev->data;
    switch (config->mode) {
    case PROCESSOR_ROTATION:
        return handle_rotation(data, event);
    case PROCESSOR_ACCELERATION:
        return handle_acceleration(data, event);
    case PROCESSOR_SCROLL_SNAP:
        return handle_scroll_snap(data, event);
    case PROCESSOR_MOUSE_GESTURE:
        gesture_collect(data, event);
        return ZMK_INPUT_PROC_CONTINUE;
    default:
        return ZMK_INPUT_PROC_CONTINUE;
    }
}

static int tentalice_processor_init(const struct device *dev) {
    struct processor_data *data = dev->data;
    k_work_init_delayable(&data->gesture_work, gesture_timeout);
    const struct processor_config *config = dev->config;
    if (config->mode == PROCESSOR_MOUSE_GESTURE) {
        gesture_processor = data;
    }
    return 0;
}

static const struct zmk_input_processor_driver_api tentalice_processor_api = {
    .handle_event = tentalice_handle_event,
};

#define TENTALICE_PROCESSOR(inst)                                                   \
    static struct processor_data processor_data_##inst;                            \
    static const struct processor_config processor_config_##inst = {               \
        .mode = DT_INST_PROP(inst, mode),                                           \
    };                                                                              \
    DEVICE_DT_INST_DEFINE(inst, tentalice_processor_init, NULL,                    \
                          &processor_data_##inst, &processor_config_##inst,         \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,         \
                          &tentalice_processor_api);

DT_INST_FOREACH_STATUS_OKAY(TENTALICE_PROCESSOR)
