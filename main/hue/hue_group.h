#pragma once

#define HUE_GROUP_ID_LEN    16
#define HUE_GROUP_NAME_LEN  64
#define HUE_GROUP_MAX_COUNT 6

typedef struct {
    char id[HUE_GROUP_ID_LEN];
    char name[HUE_GROUP_NAME_LEN];
} hue_group_t;
