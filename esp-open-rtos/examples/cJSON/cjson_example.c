/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include <stdio.h>
#include <string.h>

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

#define vTaskDelayMs(ms) vTaskDelay((ms) / portTICK_PERIOD_MS)
void jsonCreateTask(void *pvParameter)
{
    printf("this is task thread\n");
    while (1)
    {
        vTaskDelayMs(1000);
        cJSON *root, *format;
        char *rendered;

        root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "name", cJSON_CreateString("teu"));
        cJSON_AddItemToObject(root, "tuoi", cJSON_CreateString("lon"));
        cJSON_AddItemToObject(root, "ly lich", format = cJSON_CreateObject());
        cJSON_AddNumberToObject(format, "ngay", 123);
        cJSON_AddNumberToObject(format, "nam", 789);
        rendered = cJSON_Print(root);
        printf("json created: %s \n", rendered);
        cJSON_Delete(root);
        vTaskDelayMs(5000);
    }
}
void user_init(void)
{
    uart_set_baud(0, 115200);
    xTaskCreate(jsonCreateTask, "json", 2048, NULL, 5, NULL);
}