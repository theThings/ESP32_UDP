/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
//#include "addr_from_stdin.h"

/* Note: ESP32 don't support temperature sensor */
#include "driver/temp_sensor.h"

#define ESP_WIFI_SSID      		""		// Please insert your SSID
#define ESP_WIFI_PASS      		""		// Please insert your password
#define ESP_WIFI_AUTH_MODE		WIFI_AUTH_WPA2_PSK // See esp_wifi_types.h
#define ESP_WIFI_MAX_RETRY 		5U

#define THETHINGSIO_TOKEN_ID 	""		// Please insert your TOKEN ID
#define THETHINGSIO_IP_ADDR 	"104.199.85.211"
#define THETHINGSIO_PORT 		28399U

#define TEMP_SENSOR_TASK_DELAY	1000U	// In milliseconds
#define UDP_POST_DELAY			300000U	// In milliseconds

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static ip_event_got_ip_t* event = NULL;
static uint8_t u8RetryCounter = 0U;

static const char *pcTAG = "TTIO_UDP_CLIENT";

static float fTemperature = 0.0f;

static void WIFI_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{

	if (event_base == WIFI_EVENT)
	{
		switch (event_id)
		{
			case WIFI_EVENT_STA_START:
				esp_wifi_connect();
				break;
			case WIFI_EVENT_STA_DISCONNECTED:
				if (u8RetryCounter < ESP_WIFI_MAX_RETRY)
				{
					esp_wifi_connect();
					u8RetryCounter++;
					ESP_LOGI(pcTAG, "Retry to connect to the access point");
				}
				else
				{
					xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
					ESP_LOGI(pcTAG,"Connect to the access point fail");
				}
				break;
			default:
				// Do nothing (see WiFi event declarations in the esp_wifi_types.h)
				break;
		}
	}
	else if (event_base == IP_EVENT)
	{
		switch (event_id)
		{
			case IP_EVENT_STA_GOT_IP:
				event = (ip_event_got_ip_t*) event_data;
				u8RetryCounter = 0U;
				xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
				ESP_LOGI(pcTAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
				break;
			default:
				// Do nothing (see WiFi event declarations in the esp_netif_types.h)
				break;
		}
	}

}

void wifi_init_sta(void)
{

	wifi_init_config_t sWifiInitCfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;

	EventBits_t WifiEventBits = 0U;

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_wifi_init(&sWifiInitCfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WIFI_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WIFI_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t sWifiConfig = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = ESP_WIFI_AUTH_MODE,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sWifiConfig));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(pcTAG, "Wi-Fi initializated");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by wifi_event_handler() (see above) */
    WifiEventBits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (WifiEventBits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(pcTAG, "Connected to access point SSID: %s, Password: %s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else if (WifiEventBits & WIFI_FAIL_BIT) {
        ESP_LOGI(pcTAG, "Failed to connect to SSID: %s, Password: %s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else {
        ESP_LOGE(pcTAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));

    vEventGroupDelete(s_wifi_event_group);

}

void TempSensor_Task(void *pvParams)
{

	temp_sensor_config_t sTemperatureInitCfg = TSENS_CONFIG_DEFAULT();

    // Initialize touch pad peripheral, it will start a timer to run a filter
    ESP_LOGI(pcTAG, "Initializing Temperature sensor");

    temp_sensor_get_config(&sTemperatureInitCfg);
    ESP_LOGI(pcTAG, "Default offset: %d, clk_div: %d", sTemperatureInitCfg.dac_offset, sTemperatureInitCfg.clk_div);
    temp_sensor_set_config(sTemperatureInitCfg);
    temp_sensor_start();
    ESP_LOGI(pcTAG, "Temperature sensor started");

    vTaskDelay(1000U / portTICK_RATE_MS);

    while (1)
	{
        temp_sensor_read_celsius(&fTemperature);
        ESP_LOGI(pcTAG, "Temperature out Celsius: %2.2f", fTemperature);
        vTaskDelay(TEMP_SENSOR_TASK_DELAY / portTICK_RATE_MS);
    }

    ESP_LOGI(pcTAG, "Finish temperature sensor");
    vTaskDelete(NULL);

}

static void UDP_task(void *pvParameters)
{

	static const char *payload = "h" THETHINGSIO_TOKEN_ID;
	uint32_t u32Temperature = 0U;
	struct sockaddr_in sDestAddr;
	char acTxBuffer[128U];
    char acRxBuffer[128U];
    char acHostIP[] = THETHINGSIO_IP_ADDR;
    int iAddrFamily = 0U;
    int iIPProtocol = 0U;

    // Header type
    acTxBuffer[0U] = 0x01;

    for (int i=1U; 44U>i; i++)
    {
    	acTxBuffer[i] = payload[i];
    }

    acTxBuffer[44U] = '\0';

    vTaskDelay(1100U / portTICK_PERIOD_MS);

    while (1)
    {
    	sDestAddr.sin_addr.s_addr = inet_addr(THETHINGSIO_IP_ADDR);
		sDestAddr.sin_family = AF_INET;
		sDestAddr.sin_port = htons(THETHINGSIO_PORT);
		iAddrFamily = AF_INET;
		iIPProtocol = IPPROTO_IP;

        int sock = socket(iAddrFamily, SOCK_DGRAM, iIPProtocol);

        if (sock < 0)
        {
            ESP_LOGE(pcTAG, "Unable to create socket: errno %d", errno);
            break;
        }

        ESP_LOGI(pcTAG, "Socket created, sending to %s:%d", THETHINGSIO_IP_ADDR, THETHINGSIO_PORT);

        while (1)
        {
        	u32Temperature = (uint32_t)(fTemperature * 100.0f);
			acTxBuffer[44U] = (char)(u32Temperature >> 8U);
			acTxBuffer[45U] = (char)u32Temperature;
			acTxBuffer[46U] = '\0';

            int err = sendto(sock, acTxBuffer, strlen(acTxBuffer), 0U, (struct sockaddr *)&sDestAddr, sizeof(sDestAddr));

            if (err < 0)
            {
                ESP_LOGE(pcTAG, "Error occurred during sending: errno %d", errno);
                break;
            }

            ESP_LOGI(pcTAG, "Message sent: %s", acTxBuffer);

            struct sockaddr_in source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, acRxBuffer, sizeof(acRxBuffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            if (len < 0)
            {
            	// Error occurred during receiving
                ESP_LOGE(pcTAG, "recvfrom failed: errno %d", errno);
                break;
            }
            else
            {
            	// Data received
                acRxBuffer[len] = 0U; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(pcTAG, "Received %d bytes from %s:", len, acHostIP);
                ESP_LOGI(pcTAG, "%s", acRxBuffer);

                if (acRxBuffer[0U] == 0x20)
                {
                    ESP_LOGI(pcTAG, "Received expected message, close connection");
                }
                else
                {
                	ESP_LOGI(pcTAG, "Received unexpected message, reconnecting");
                }
                break;
            }
        }

        if (sock != -1)
        {
            ESP_LOGE(pcTAG, "Shutting down socket...");
            shutdown(sock, 0U);
            close(sock);
        }

        vTaskDelay(UDP_POST_DELAY / portTICK_PERIOD_MS);
    }

    ESP_LOGI(pcTAG, "Finish UDP requests");
    vTaskDelete(NULL);

}

void app_main(void)
{

	// Initialize NVS
	esp_err_t ret = nvs_flash_init();

	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
	  ESP_ERROR_CHECK(nvs_flash_erase());
	  ret = nvs_flash_init();
	}

	ESP_ERROR_CHECK(ret);

	// Initialize station mode
	wifi_init_sta();

    xTaskCreate(TempSensor_Task, "tempsensor_task", 2048U, NULL, 5U, NULL);
    xTaskCreate(UDP_task, "udp_client", 4096U, NULL, 5U, NULL);

}
