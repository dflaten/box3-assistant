#include "assistant/command_registry.h"

#include "commands/assistant_command_dispatch.h"
#include "hue/hue_command_handler.h"
#include "timer/timer_command_handler.h"
#include "weather/weather_command_handler.h"

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
    *out_handler = NULL;

    switch (out_dispatch->type) {
    case ASSISTANT_COMMAND_ACTION_SYNC_GROUPS:
    case ASSISTANT_COMMAND_ACTION_HUE_GROUP:
        *out_handler = hue_command_handler_get();
        break;
    case ASSISTANT_COMMAND_ACTION_WEATHER_TODAY:
    case ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW:
        *out_handler = weather_command_handler_get();
        break;
    case ASSISTANT_COMMAND_ACTION_SET_TIMER:
    case ASSISTANT_COMMAND_ACTION_STOP:
        *out_handler = timer_command_handler_get();
        break;
    case ASSISTANT_COMMAND_ACTION_UNKNOWN:
    default:
        break;
    }

    return *out_handler != NULL;
}
