//#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "esp_netif.h"

#define LED_GPIO GPIO_NUM_2      // GPIO 2 para el primer NE555 (Astable)
#define LED_GPIO2 GPIO_NUM_4     // GPIO 4 para el segundo NE555 (Astable)
#define MONO_GPIO GPIO_NUM_5     // GPIO 5 para el temporizador Monostable

static const char *TAG = "PWM_555_WEB";

// Valores del primer NE555
static float ra = 10000.0;  // Ohmios para el primer astable
static float rb = 4700.0;   // Ohmios para el primer astable
static float c = 100.0;     // Microfaradios para el primer astable

// Valores del segundo NE555
static float ra2 = 10000.0; // Ohmios para el segundo astable
static float rb2 = 4700.0;  // Ohmios para el segundo astable
static float c2 = 100.0;    // Microfaradios para el segundo astable

// Tiempo del Monostable
static int mono_pulse_time = 2000; // Tiempo en milisegundos

static bool pwm_activo = false;  // Control del estado del PWM
static bool mono_active = false; // Estado del monostable

// Tareas para los temporizadores NE555
static TaskHandle_t pwm_task_handle = NULL;
static TaskHandle_t pwm_task_handle2 = NULL;
static TaskHandle_t mono_task_handle = NULL;

// Tarea para el primer NE555 (GPIO 2)
void pwm_ne555_task(void *pvParameters)
{
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    while (1) {
        if (pwm_activo) {
            float c_farads = c / 1000000.0; // µF → F
            float t1 = 0.693 * (ra + rb) * c_farads;
            float t2 = 0.693 * rb * c_farads;

            int t1_ms = (int)(t1 * 1000);
            int t2_ms = (int)(t2 * 1000);

            ESP_LOGI("PWM", "T1=%.2f ms, T2=%.2f ms", t1 * 1000, t2 * 1000);

            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(t1_ms > 1 ? t1_ms : 1));

            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(t2_ms > 1 ? t2_ms : 1));
        } else {
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Tarea para el segundo NE555 (GPIO 4)
void pwm_ne555_task2(void *pvParameters)
{
    gpio_set_direction(LED_GPIO2, GPIO_MODE_OUTPUT);
    while (1) {
        if (pwm_activo) {
            float c_farads2 = c2 / 1000000.0; // µF → F
            float t1_2 = 0.693 * (ra2 + rb2) * c_farads2;
            float t2_2 = 0.693 * rb2 * c_farads2;

            int t1_ms2 = (int)(t1_2 * 1000);
            int t2_ms2 = (int)(t2_2 * 1000);

            ESP_LOGI("PWM2", "T1=%.2f ms, T2=%.2f ms", t1_2 * 1000, t2_2 * 1000);

            gpio_set_level(LED_GPIO2, 1);
            vTaskDelay(pdMS_TO_TICKS(t1_ms2 > 1 ? t1_ms2 : 1));

            gpio_set_level(LED_GPIO2, 0);
            vTaskDelay(pdMS_TO_TICKS(t2_ms2 > 1 ? t2_ms2 : 1));
        } else {
            gpio_set_level(LED_GPIO2, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Tarea para el monostable (GPIO 5)
void mono_task(void *pvParameters)
{
    gpio_set_direction(MONO_GPIO, GPIO_MODE_OUTPUT);
    while (1) {
        if (mono_active) {
            gpio_set_level(MONO_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(mono_pulse_time));
            gpio_set_level(MONO_GPIO, 0);
            mono_active = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// HTML para la página web
const char* html_page =
"<html><head><meta charset='UTF-8'><title>NE555 PWM</title>"
"<style>body {background-color: black; color: white;} h1 {color: red; font-family: Arial, sans-serif;} "
"button {background-color: red; color: white; padding: 10px 20px; border: none; cursor: pointer;} "
"input[type='number'] {background-color: black; color: white; border: 1px solid red;} "
"input[type='submit'] {background-color: red; color: white;}</style>"
"</head><body>"
"<h1>Simulador NE555 y Monostable (ESP32)</h1>"
"<h2>Configuración del Astable (NE555):</h2>"
"<form action=\"/set\" method=\"GET\">"
"RA (ohmios): <input type=\"number\" name=\"ra\" value=\"10000\"><br><br>"
"RB (ohmios): <input type=\"number\" name=\"rb\" value=\"4700\"><br><br>"
"C (uF): <input type=\"number\" name=\"c\" value=\"100\" step=\"0.1\"><br><br>"
"<h2>Configuración del Segundo Astable (NE555):</h2>"
"RA2 (ohmios): <input type=\"number\" name=\"ra2\" value=\"10000\"><br><br>"
"RB2 (ohmios): <input type=\"number\" name=\"rb2\" value=\"4700\"><br><br>"
"C2 (uF): <input type=\"number\" name=\"c2\" value=\"100\" step=\"0.1\"><br><br>"
"<h2>Configuración del Monostable (Tiempo de Pulso):</h2>"
"Tiempo (ms): <input type=\"number\" name=\"mono_time\" value=\"2000\"><br><br>"
"<input type=\"submit\" value=\"Actualizar Parámetros\">"
"</form><br><br>"
"<button onclick=\"fetch('/led/on')\">ENCENDER ASTABLE</button>&nbsp;"
"<button onclick=\"fetch('/led/off')\">APAGAR ASTABLE</button><br><br>"
"<button onclick=\"fetch('/mono/on')\">EJECUTAR MONOSTABLE</button><br><br>"
"</body></html>";

// Manejadores para los endpoints HTTP
esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t led_on_handler(httpd_req_t *req)
{
    pwm_activo = true;
    httpd_resp_send(req, "PWM ACTIVADO", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t led_off_handler(httpd_req_t *req)
{
    pwm_activo = false;
    httpd_resp_send(req, "PWM DETENIDO", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t mono_on_handler(httpd_req_t *req)
{
    mono_active = true;
    httpd_resp_send(req, "Monostable EJECUTADO", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t set_handler(httpd_req_t *req)
{
    char buf[100];
    size_t len = httpd_req_get_url_query_len(req) + 1;
    if (len > 1) {
        httpd_req_get_url_query_str(req, buf, len);

        char param[20];
        if (httpd_query_key_value(buf, "ra", param, sizeof(param)) == ESP_OK) {
            ra = atof(param);
        }
        if (httpd_query_key_value(buf, "rb", param, sizeof(param)) == ESP_OK) {
            rb = atof(param);
        }
        if (httpd_query_key_value(buf, "c", param, sizeof(param)) == ESP_OK) {
            c = atof(param);
        }
        if (httpd_query_key_value(buf, "ra2", param, sizeof(param)) == ESP_OK) {
            ra2 = atof(param);
        }
        if (httpd_query_key_value(buf, "rb2", param, sizeof(param)) == ESP_OK) {
            rb2 = atof(param);
        }
        if (httpd_query_key_value(buf, "c2", param, sizeof(param)) == ESP_OK) {
            c2 = atof(param);
        }
        if (httpd_query_key_value(buf, "mono_time", param, sizeof(param)) == ESP_OK) {
            mono_pulse_time = atoi(param);
        }
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Función para iniciar el servidor web
httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
        httpd_uri_t on = { .uri = "/led/on", .method = HTTP_GET, .handler = led_on_handler };
        httpd_uri_t off = { .uri = "/led/off", .method = HTTP_GET, .handler = led_off_handler };
        httpd_uri_t mono_on = { .uri = "/mono/on", .method = HTTP_GET, .handler = mono_on_handler };
        httpd_uri_t set = { .uri = "/set", .method = HTTP_GET, .handler = set_handler };
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &on);
        httpd_register_uri_handler(server, &off);
        httpd_register_uri_handler(server, &mono_on);
        httpd_register_uri_handler(server, &set);
    }

    return server;
}

// Inicialización del WiFi en modo AP
void wifi_init_softap(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "PWM_555",
            .ssid_len = strlen("PWM_555"),
            .channel = 1,
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "SoftAP creado -> SSID: PWM_555 | PASS: 12345678");
}

// Función principal
void app_main(void)
{
    nvs_flash_init();
    wifi_init_softap();
    start_webserver();

    // Tarea para el primer NE555
    xTaskCreate(&pwm_ne555_task, "pwm_ne555_task", 2048, NULL, 5, &pwm_task_handle);

    // Tarea para el segundo NE555
    xTaskCreate(&pwm_ne555_task2, "pwm_ne555_task2", 2048, NULL, 5, &pwm_task_handle2);

    // Tarea para el monostable
    xTaskCreate(&mono_task, "mono_task", 2048, NULL, 5, &mono_task_handle);
}
