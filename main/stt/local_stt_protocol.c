#include "stt/local_stt_protocol.h"

#include <stdio.h>
#include <string.h>

bool local_stt_protocol_select_string(const char *preferred, const char *fallback, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return false;
    }

    const char *value = (preferred != NULL && preferred[0] != '\0')
                          ? preferred
                          : ((fallback != NULL && fallback[0] != '\0') ? fallback : NULL);
    if (value == NULL) {
        out[0] = '\0';
        return false;
    }

    snprintf(out, out_size, "%s", value);
    return true;
}

local_stt_protocol_event_result_t
local_stt_protocol_classify_event(const char *event_type, const char *text, const char *message) {
    if (event_type == NULL) {
        return LOCAL_STT_PROTOCOL_EVENT_CONTINUE;
    }

    if (strcmp(event_type, "error") == 0) {
        (void) message;
        return LOCAL_STT_PROTOCOL_EVENT_ERROR;
    }

    if (strcmp(event_type, "transcript") == 0 && text != NULL && text[0] != '\0') {
        return LOCAL_STT_PROTOCOL_EVENT_TRANSCRIPT;
    }

    return LOCAL_STT_PROTOCOL_EVENT_CONTINUE;
}
