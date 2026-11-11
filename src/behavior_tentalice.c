/*
 * Copyright (c) 2026 Tentalice contributors
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_tentalice_setting

#include <drivers/behavior.h>
#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#include <zmk/behavior.h>

void tentalice_gesture_finished(void);

static int setting_binding(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata setting_ids[] = {
    {.display_name = "Setting ID", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
     .range = {.min = 0, .max = 25}},
};
static const struct behavior_parameter_value_metadata setting_values[] = {
    {.display_name = "Setting value", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
     .range = {.min = 0, .max = 100000}},
};
static const struct behavior_parameter_metadata_set setting_metadata_set = {
    .param1_values_len = ARRAY_SIZE(setting_ids),
    .param1_values = setting_ids,
    .param2_values_len = ARRAY_SIZE(setting_values),
    .param2_values = setting_values,
};
static const struct behavior_parameter_metadata setting_metadata = {
    .sets_len = 1,
    .sets = &setting_metadata_set,
};
static const struct behavior_driver_api setting_api = {
    .binding_pressed = setting_binding,
    .binding_released = setting_binding,
    .parameter_metadata = &setting_metadata,
};
#else
static const struct behavior_driver_api setting_api = {
    .binding_pressed = setting_binding,
    .binding_released = setting_binding,
};
#endif

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &setting_api);

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT zmk_behavior_tentalice_mouse_gesture

static bool gesture_enabled;

bool tentalice_mouse_gesture_enabled(void) { return gesture_enabled; }

static int gesture_pressed(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    gesture_enabled = true;
    return ZMK_BEHAVIOR_OPAQUE;
}

static int gesture_released(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    gesture_enabled = false;
    tentalice_gesture_finished();
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_driver_api gesture_api = {
    .binding_pressed = gesture_pressed,
    .binding_released = gesture_released,
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
};
#else
static const struct behavior_driver_api gesture_api = {
    .binding_pressed = gesture_pressed,
    .binding_released = gesture_released,
};
#endif

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &gesture_api);
