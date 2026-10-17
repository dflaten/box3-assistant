#include "assistant/command_registry.h"

#include "commands/assistant_command_dispatch.h"
#include "hue/hue_command_handler.h"
#include "time/time_command_handler.h"
#include "timer/timer_command_handler.h"
#include "volume/volume_command_handler.h"
#include "weather/weather_command_handler.h"

/**
 * @brief Locate the feature handler registered for a resolved action type.
 * @param action Resolved command action type.
 * @return Registered handler for the action, or NULL when no handler is registered.
 */
const assistant_command_handler_t *assistant_command_registry_handler_for(assistant_command_action_type_t action) {
    switch (action) {
    case ASSISTANT_COMMAND_ACTION_SYNC_GROUPS:
    case ASSISTANT_COMMAND_ACTION_HUE_GROUP:
        return hue_command_handler_get();
    case ASSISTANT_COMMAND_ACTION_WEATHER_TODAY:
    case ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW:
        return weather_command_handler_get();
    case ASSISTANT_COMMAND_ACTION_SET_TIMER:
    case ASSISTANT_COMMAND_ACTION_SET_TIMER_DURATION:
    case ASSISTANT_COMMAND_ACTION_STOP:
        return timer_command_handler_get();
    case ASSISTANT_COMMAND_ACTION_TIME_NOW:
    case ASSISTANT_COMMAND_ACTION_TIME_IN_LOCATION:
        return time_command_handler_get();
    case ASSISTANT_COMMAND_ACTION_VOLUME_UP:
    case ASSISTANT_COMMAND_ACTION_VOLUME_DOWN:
        return volume_command_handler_get();
    case ASSISTANT_COMMAND_ACTION_UNKNOWN:
    default:
        return NULL;
    }
}

/**
 * @brief Resolve a command id and locate the registered feature handler for it.
 * @param command_id Recognized MultiNet command id.
 * @param group_count Number of runtime Hue groups available for dynamic group commands.
 * @param out_dispatch Output dispatch description populated from the command id.
 * @param out_handler Output handler pointer for the resolved action, or NULL when no handler is registered.
 * @return True when a handler is registered for the resolved action, otherwise false.
 */
bool assistant_command_registry_lookup(int command_id,
                                       size_t group_count,
                                       assistant_command_dispatch_t *out_dispatch,
                                       const assistant_command_handler_t **out_handler) {
    if (out_dispatch == NULL || out_handler == NULL) {
        return false;
    }

    assistant_command_resolve(command_id, group_count, out_dispatch);
    *out_handler = assistant_command_registry_handler_for(out_dispatch->type);
    return *out_handler != NULL;
}
