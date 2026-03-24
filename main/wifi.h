#pragma once

typedef void (*wifi_connected_cb_t)(void);

void        wifi_init(wifi_connected_cb_t on_connected);
const char *wifi_get_tb_token(void);
