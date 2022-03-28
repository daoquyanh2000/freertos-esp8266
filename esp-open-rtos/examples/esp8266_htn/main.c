#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "espressif/esp_common.h"
#include "esp/uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "i2c/i2c.h"
#include "bh1750/bh1750.h"

#include <dht/dht.h>
#include "esp8266.h"
#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

#include <ssid_config.h>
#include <cJSON/CJSON.h>

#include <espressif/esp_sta.h>
#include <espressif/esp_wifi.h>

#include <paho_mqtt_c/MQTTESP8266.h>
#include <paho_mqtt_c/MQTTClient.h>

#include <semphr.h>

/* Add extras/sntp component to makefile for this include to work */
#include <sntp.h>
#include <time.h>

#define SNTP_SERVERS "0.pool.ntp.org", "1.pool.ntp.org", \
                     "2.pool.ntp.org", "3.pool.ntp.org"

//#define MQTT_HOST ("test.mosquitto.org")
#define MQTT_HOST ("broker.emqx.io")
#define MQTT_PORT 1883

#define MQTT_USER NULL
#define MQTT_PASS NULL

#define topicSensor "esp8266_htn_sensor"
#define topicRelay1 "esp8266_htn_relay1"
#define topicRelay2 "esp8266_htn_relay2"
#define topicRelay3 "esp8266_htn_relay3"
#define topicRelay4 "esp8266_htn_relay4"

// define dht pin
#define dht_gpio 0
#define UNUSED_ARG(x) (void)x
/*define I2C pin*/
#define SCL_PIN 5
#define SDA_PIN 4
#define I2C_BUS 0

// define short handdelay
#define vTaskDelayMs(ms) vTaskDelay((ms) / portTICK_PERIOD_MS)

int8_t relay1_gpio = 14;
int8_t relay2_gpio = 12;
int8_t relay3_gpio = 13;
int8_t relay4_gpio = 15;

const dht_sensor_type_t sensor_type = DHT_TYPE_DHT11;

uint8_t tempValue, humValue, LightValue;
char *dateValue;

QueueHandle_t publish_queue;
SemaphoreHandle_t wifi_alive;
SemaphoreHandle_t mqtt_alive;
SemaphoreHandle_t collectData_done;
#define PUB_MSG_LEN 150

void sntp_tsk(void *pvParameters)
{
    const char *servers[] = {SNTP_SERVERS};
    UNUSED_ARG(pvParameters);

    /* Wait until we have joined AP and are assigned an IP */
    while (sdk_wifi_station_get_connect_status() != STATION_GOT_IP)
    {
        vTaskDelayMs(1000);
    }

    /* Start SNTP */
    printf("Starting SNTP... ");
    /* SNTP will request an update each 5 minutes */
    sntp_set_update_delay(5 * 60000);
    /* Set GMT+1 zone, daylight savings off */
    const struct timezone tz = {7 * 60, 0};
    /* SNTP initialization */
    sntp_initialize(&tz);
    /* Servers must be configured right after initialization */
    sntp_set_servers(servers, sizeof(servers) / sizeof(char *));
    printf("DONE!\n");
    vTaskDelayMs(5000);

    /* Print date and time each 1 seconds */
    while (1)
    {
        xSemaphoreGive(collectData_done);
        char *ctime_no_newline;
        time_t ts = time(NULL);
        ctime_no_newline = strtok(ctime(&ts), "\n");
        // printf("TIME: %s", ctime(&ts));
        dateValue = ctime_no_newline;
        vTaskDelayMs(1000);
    }
}

void dhtMeasurementTask(void *pvParameters)
{
    int16_t temperature = 0;
    int16_t humidity = 0;

    // DHT sensors that come mounted on a PCB generally have
    // pull-up resistors on the data pin.  It is recommended
    // to provide an external pull-up resistor otherwise...
    gpio_set_pullup(dht_gpio, false, false);

    while (1)
    {
        if (dht_read_data(sensor_type, dht_gpio, &humidity, &temperature))
        {
            // printf("Humidity: %d%%  Temp: %d C\n",
            //        humidity / 10,
            //        temperature / 10);
            tempValue = temperature / 10;
            humValue = humidity / 10;
        }
        else
        {
            printf("Could not read data from sensor\n");
        }

        // Three second delay...
        vTaskDelayMs(1000);
    }
}

static void bh1750MeasureTask(void *pvParameters)
{
    i2c_dev_t dev = {
        .addr = BH1750_ADDR_LO,
        .bus = I2C_BUS,
    };
    bh1750_configure(&dev, BH1750_CONTINUOUS_MODE | BH1750_HIGH_RES_MODE);

    while (1)
    {
        // printf("Lux: %d\n", bh1750_read(&dev));
        LightValue = bh1750_read(&dev);
        vTaskDelayMs(1000);
    }
}

void jsonCreateTask(void *pvParameter)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    char msg[PUB_MSG_LEN];
    while (1)
    {
        xSemaphoreTake(wifi_alive, portMAX_DELAY);
        xSemaphoreTake(mqtt_alive, portMAX_DELAY);
        xSemaphoreTake(collectData_done, portMAX_DELAY);
        vTaskDelayUntil(&xLastWakeTime, 1000 / portTICK_PERIOD_MS);
        cJSON *root;
        char *rendered;
        root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "d", cJSON_CreateString(dateValue));
        cJSON_AddNumberToObject(root, "t", tempValue);
        cJSON_AddNumberToObject(root, "h", humValue);
        cJSON_AddNumberToObject(root, "l", LightValue);
        cJSON_AddItemToObject(root, "r1", gpio_read(relay1_gpio) == 1 ? cJSON_CreateString("on") : cJSON_CreateString("off"));
        cJSON_AddItemToObject(root, "r2", gpio_read(relay2_gpio) == 1 ? cJSON_CreateString("on") : cJSON_CreateString("off"));
        cJSON_AddItemToObject(root, "r3", gpio_read(relay3_gpio) == 1 ? cJSON_CreateString("on") : cJSON_CreateString("off"));
        cJSON_AddItemToObject(root, "r4", gpio_read(relay4_gpio) == 1 ? cJSON_CreateString("on") : cJSON_CreateString("off"));
        rendered = cJSON_Print(root);
        free(dateValue);
        snprintf(msg, PUB_MSG_LEN, "%s\n", rendered);
        printf("json created: %s \n", rendered);
        cJSON_free(rendered);
        cJSON_Delete(root);
        if (xQueueSend(publish_queue, (void *)msg, 0) == pdFALSE)
        {
            printf("Publish queue overflow.\r\n");
        }
    }
}

static void wifi_task(void *pvParameters)
{
    uint8_t status = 0;
    uint8_t retries = 30;
    struct sdk_station_config config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS,
    };

    printf("WiFi: connecting to WiFi\n\r");
    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);

    while (1)
    {
        while ((status != STATION_GOT_IP) && (retries))
        {
            status = sdk_wifi_station_get_connect_status();
            printf("%s: status = %d\n\r", __func__, status);
            if (status == STATION_WRONG_PASSWORD)
            {
                printf("WiFi: wrong password\n\r");
                break;
            }
            else if (status == STATION_NO_AP_FOUND)
            {
                printf("WiFi: AP not found\n\r");
                break;
            }
            else if (status == STATION_CONNECT_FAIL)
            {
                printf("WiFi: connection failed\r\n");
                break;
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            --retries;
        }
        if (status == STATION_GOT_IP)
        {
            printf("WiFi: Connected\n\r");
            xSemaphoreGive(wifi_alive);
            taskYIELD();
        }

        while ((status = sdk_wifi_station_get_connect_status()) == STATION_GOT_IP)
        {
            xSemaphoreGive(wifi_alive);
            taskYIELD();
        }
        printf("WiFi: disconnected\n\r");
        sdk_wifi_station_disconnect();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

static void control_relay(char topic[10], char message[10])
{
    int state = strcmp(message, "on");

    if (strcmp(topic, topicRelay1) == 0)
        gpio_write(relay1_gpio, (state == 0) ? 1 : 0);

    if (strcmp(topic, topicRelay2) == 0)
        gpio_write(relay2_gpio, (state == 0) ? 1 : 0);

    if (strcmp(topic, topicRelay3) == 0)
        gpio_write(relay3_gpio, (state == 0) ? 1 : 0);

    if (strcmp(topic, topicRelay4) == 0)
        gpio_write(relay4_gpio, (state == 0) ? 1 : 0);
};

static void topic_received(mqtt_message_data_t *md)
{
    int i;
    mqtt_message_t *message = md->message;
    char topic[10];
    char msg[10];
    printf("Received: ");
    for (i = 0; i < md->topic->lenstring.len; ++i)
    {
        // printf("%c", md->topic->lenstring.data[i]);
        topic[i] = md->topic->lenstring.data[i];
    }
    topic[md->topic->lenstring.len] = '\0';
    printf(topic);
    printf(" = ");
    for (i = 0; i < (int)message->payloadlen; ++i)
    {
        // printf("%c", ((char *)(message->payload))[i]);
        msg[i] = ((char *)(message->payload))[i];
    }
    msg[(int)message->payloadlen] = '\0';
    printf(msg);
    printf("\r\n");
    control_relay(topic, msg);
}

static void mqtt_task(void *pvParameters)
{
    int ret = 0;
    struct mqtt_network network;
    mqtt_client_t client = mqtt_client_default;
    char mqtt_client_id[20];
    uint8_t mqtt_buf[200];
    uint8_t mqtt_readbuf[200];
    mqtt_packet_connect_data_t data = mqtt_packet_connect_data_initializer;

    mqtt_network_new(&network);
    memset(mqtt_client_id, 0, sizeof(mqtt_client_id));
    strcpy(mqtt_client_id, "ESP-teu");
    // strcat(mqtt_client_id, get_my_id());

    while (1)
    {
        xSemaphoreTake(wifi_alive, portMAX_DELAY);
        printf("%s: started\n\r", __func__);
        printf("%s: (Re)connecting to MQTT server %s ... ", __func__,
               MQTT_HOST);
        ret = mqtt_network_connect(&network, MQTT_HOST, MQTT_PORT);
        if (ret)
        {
            printf("error: %d\n\r", ret);
            taskYIELD();
            continue;
        }
        printf("done\n\r");
        mqtt_client_new(&client, &network, 5000, mqtt_buf, 200,
                        mqtt_readbuf, 200);

        data.willFlag = 0;
        data.MQTTVersion = 3;
        data.clientID.cstring = mqtt_client_id;
        data.username.cstring = MQTT_USER;
        data.password.cstring = MQTT_PASS;
        data.keepAliveInterval = 10;
        data.cleansession = 0;
        printf("Send MQTT connect ... ");
        ret = mqtt_connect(&client, &data);
        if (ret)
        {
            printf("error: %d\n\r", ret);
            mqtt_network_disconnect(&network);
            taskYIELD();
            continue;
        }
        printf("done\r\n");

        mqtt_subscribe(&client, topicRelay1, MQTT_QOS1, topic_received);
        mqtt_subscribe(&client, topicRelay2, MQTT_QOS1, topic_received);
        mqtt_subscribe(&client, topicRelay3, MQTT_QOS1, topic_received);
        mqtt_subscribe(&client, topicRelay4, MQTT_QOS1, topic_received);
        xQueueReset(publish_queue);
        while (1)
        {
            xSemaphoreGive(mqtt_alive);
            char msg[PUB_MSG_LEN - 1];
            while (xQueueReceive(publish_queue, (void *)msg, 0) ==
                   pdTRUE)
            {
                // printf("got message to publish\r\n");
                mqtt_message_t message;
                message.payloadlen = strlen(msg);
                message.payload = msg;
                message.dup = 0;
                message.qos = MQTT_QOS1;
                message.retained = 0;
                ret = mqtt_publish(&client, topicSensor, &message);
                if (ret != MQTT_SUCCESS)
                {
                    printf("error while publishing message: %d\n", ret);
                    break;
                }
            }

            ret = mqtt_yield(&client, 1000);
            if (ret == MQTT_DISCONNECTED)
                break;
        }
        printf("Connection dropped, request restart\n\r");
        mqtt_network_disconnect(&network);
        taskYIELD();
    }
}

void user_init(void)
{
    gpio_enable(relay1_gpio, GPIO_OUTPUT);
    gpio_enable(relay2_gpio, GPIO_OUTPUT);
    gpio_enable(relay3_gpio, GPIO_OUTPUT);
    gpio_enable(relay4_gpio, GPIO_OUTPUT);

    uart_set_baud(0, 115200);
    vSemaphoreCreateBinary(collectData_done);
    vSemaphoreCreateBinary(wifi_alive);
    vSemaphoreCreateBinary(mqtt_alive);
    // Just some information
    printf("\n");
    printf("SDK version : %s\n", sdk_system_get_sdk_version());
    printf("GIT version : %s\n", GITSHORTREV);

    i2c_init(I2C_BUS, SCL_PIN, SDA_PIN, I2C_FREQ_100K);
    publish_queue = xQueueCreate(3, PUB_MSG_LEN);
    xTaskCreate(&wifi_task, "wifi_task", 256, NULL, 2, NULL);
    xTaskCreate(&mqtt_task, "mqtt_task", 1024, NULL, 3, NULL);
    xTaskCreate(sntp_tsk, "getDateTask", 1024, NULL, 4, NULL);
    xTaskCreate(bh1750MeasureTask, "bh1750MeasureTask", 256, NULL, 4, NULL);
    xTaskCreate(dhtMeasurementTask, "dhtMeasurementTask", 256, NULL, 4, NULL);
    xTaskCreate(jsonCreateTask, "jsonCreateTask", 1024, NULL, 5, NULL);
}
