#pragma once

// 公开源码不绑定生产服务。私有构建可在未纳入 Git 的
// service_config.local.h 中覆盖这些宏，也可以通过构建参数定义。
#if defined(__has_include) && __has_include("service_config.local.h")
#include "service_config.local.h"
#endif

#ifndef DIJI_SERVICE_ORIGIN
#define DIJI_SERVICE_ORIGIN "https://example.invalid"
#endif

#ifndef DIJI_API_BASE_URL
#define DIJI_API_BASE_URL DIJI_SERVICE_ORIGIN "/api/v1"
#endif

#ifndef DIJI_GAME_STORE_BASE_URL
#define DIJI_GAME_STORE_BASE_URL DIJI_API_BASE_URL
#endif

#ifndef DIJI_CLOUD_BASE_URL
#define DIJI_CLOUD_BASE_URL DIJI_API_BASE_URL "/cloud"
#endif

#ifndef DIJI_DEVICE_REGISTER_URL
#define DIJI_DEVICE_REGISTER_URL DIJI_API_BASE_URL "/devices/register"
#endif

#ifndef DIJI_DEVICE_HEARTBEAT_URL
#define DIJI_DEVICE_HEARTBEAT_URL DIJI_API_BASE_URL "/devices/heartbeat"
#endif

#ifndef DIJI_REMOTE_CONFIG_URL
#define DIJI_REMOTE_CONFIG_URL DIJI_API_BASE_URL "/device/config"
#endif

#ifndef DIJI_USAGE_EVENT_URL
#define DIJI_USAGE_EVENT_URL DIJI_API_BASE_URL "/usage/events"
#endif

#ifndef DIJI_OTA_CHECK_URL
#define DIJI_OTA_CHECK_URL DIJI_API_BASE_URL "/ota/check"
#endif

#ifndef DIJI_OTA_EVENT_URL
#define DIJI_OTA_EVENT_URL DIJI_API_BASE_URL "/ota/events"
#endif
