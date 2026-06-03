/**
 * http_config.h
 * HTTP server settings and API path definitions for Smart Helmet System
 */

#ifndef HTTP_CONFIG_H
#define HTTP_CONFIG_H

// ---------------------------------------------------------------------------
// WiFi credentials  (update before flashing)
// ---------------------------------------------------------------------------
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ---------------------------------------------------------------------------
// HTTP backend
// ---------------------------------------------------------------------------
#define HTTP_SERVER_URL "http://192.168.1.100:5000"

// Optional bearer token (leave empty if not used)
#define HTTP_AUTH_TOKEN ""

// ---------------------------------------------------------------------------
// API routes (must match servers/mqtt_server.py)
// ---------------------------------------------------------------------------
#define API_SENSORS_BATCH    "/api/sensors/batch"
#define API_ALERT            "/api/alert"
#define API_COMMANDS_PENDING "/api/commands/pending"

// ---------------------------------------------------------------------------
// Publish / polling intervals (ms)
// ---------------------------------------------------------------------------
#define SENSOR_PUBLISH_INTERVAL   1000
#define COMMAND_POLL_INTERVAL     2000

// ---------------------------------------------------------------------------
// Retry / backoff for failed HTTP requests
// ---------------------------------------------------------------------------
#define HTTP_RETRY_INITIAL_DELAY  1000
#define HTTP_RETRY_MAX_DELAY      30000
#define HTTP_MAX_RETRIES          5

#endif // HTTP_CONFIG_H
