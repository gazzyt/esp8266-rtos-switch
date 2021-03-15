/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <sys/param.h>


#include "esp_system.h"
#include "esp_log.h"
//#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_http_server.h>
#include "freertos/task.h"
#include <freertos/timers.h>

#include "default_html.h"

static const char *TAG="SWITCHER";
static TimerHandle_t debounceTimer;

// Number of ms for the pushbutton debouncing timer
#define DEBOUNCE_TIME_MS    20

#define GPIO_OUTPUT_SWITCH  2
#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_SWITCH)

#define GPIO_INPUT_BUTTON   0
#define GPIO_INPUT_PIN_SEL  (1ULL<<GPIO_INPUT_BUTTON)

void toggle_switch()
{
    int current = gpio_get_level(GPIO_OUTPUT_SWITCH);
    if (current == 0)
    {
        gpio_set_level(GPIO_OUTPUT_SWITCH, 1);
    }
    else
    {
        gpio_set_level(GPIO_OUTPUT_SWITCH, 0);
    }
}

void switch_on()
{
    gpio_set_level(GPIO_OUTPUT_SWITCH, 0);
}

void switch_off()
{
    gpio_set_level(GPIO_OUTPUT_SWITCH, 1);
}


/* An HTTP GET handler */
esp_err_t root_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }


    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "on", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query1=%s", param);
                switch_on();
            }
            if (httpd_query_key_value(buf, "off", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query3=%s", param);
                switch_off();
            }
        }
        free(buf);
    }

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    httpd_resp_send(req, default_html_bytes, default_html_len);

    return ESP_OK;
}

httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler
};


httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    httpd_handle_t *server = (httpd_handle_t *) ctx;
    /* For accessing reason codes in case of disconnection */
    system_event_info_t *info = &event->event_info;

    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, CONFIG_WIFI_HOSTNAME);
        ESP_ERROR_CHECK(esp_wifi_connect());
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
        ESP_LOGI(TAG, "Got IP: '%s'",
                ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));

        /* Start the web server */
        if (*server == NULL) {
            *server = start_webserver();
        }
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        ESP_LOGE(TAG, "Disconnect reason : %d", info->disconnected.reason);
        if (info->disconnected.reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            /*Switch to 802.11 bgn mode */
            esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        }
        ESP_ERROR_CHECK(esp_wifi_connect());

        /* Stop the web server */
        if (*server) {
            stop_webserver(*server);
            *server = NULL;
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void *arg)
{
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, arg));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// This handler is called when the pushbutton is pressed
static void gpio_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken;

    // If the timer is already running then ignore this event
    if (!xTimerIsTimerActive(debounceTimer))
    {
        // Debounde timer is not running so start it
        xTimerStartFromISR(debounceTimer, &xHigherPriorityTaskWoken);

        if (xHigherPriorityTaskWoken == pdTRUE)
        {
            taskYIELD();
        }
    }
}

// This handler is called when the debounce timer expires
static void debounce_timer_handler(TimerHandle_t xTimer)
{
    int current = gpio_get_level(GPIO_INPUT_BUTTON);
    if (current == 0)
    {
        // If the button is still pressed after the debounce time has passed then it's a good press
        // Toggle the LED state
        toggle_switch();
    }
}

void app_main()
{
    static httpd_handle_t server = NULL;

    // Create the pushbutton debouncing timer
    debounceTimer = xTimerCreate("Debounce", DEBOUNCE_TIME_MS / portTICK_RATE_MS, false, NULL, debounce_timer_handler);

    gpio_config_t io_conf;

    // Configure output pin

    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO15/16
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    // Configure pushbutton input pin

    // enable interrupt on falling edge
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    // set as input
    io_conf.mode = GPIO_MODE_INPUT;
    //bit mask of the pins that you want to set,e.g.GPIO15/16
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    // Turn the LED on at startup
    gpio_set_level(GPIO_OUTPUT_SWITCH, 0);

    //install gpio isr service
    gpio_install_isr_service(0);
    //hook isr handler for pushbutton gpio pin
    gpio_isr_handler_add(GPIO_INPUT_BUTTON, gpio_isr_handler, (void *) GPIO_INPUT_BUTTON);


    ESP_ERROR_CHECK(nvs_flash_init());
    initialise_wifi(&server);
}
