#pragma once

// Initialize HTTP server (monitoring + config API)
void webserver_init(void);

// Start captive portal DNS redirect (AP mode)
void webserver_start_captive_dns(void);

// Stop captive portal DNS
void webserver_stop_captive_dns(void);
