#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include "freertos/task.h"
#include "esp_http_client.h"
#include <ctype.h>
#include <freertos/event_groups.h>


#define WIFI_SSID "teste"
#define WIFI_PASS "12345678"

unsigned char api_key[] = "1094825";
unsigned char whatsapp_num[] = "******";

#define LED_PIN GPIO_NUM_23      // Pino do LED VERDE
#define LED_PIN2 GPIO_NUM_1      // Pino do LED VERDE
#define BUTTON_PIN GPIO_NUM_22   // Pino do botão
#define BUZZER_PIN GPIO_NUM_19   // Pino do buzzer

static const char *TAG = "WiFi_NTP";
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

// Estrutura para armazenar eventos programados
typedef struct {
    int hour;
    int minute;
    int second;
    const char *message;
} ScheduledEvent;

// Horários e mensagens programados
ScheduledEvent events[] = {
    {11, 00, 0, "Mensagem 1: 10:30:00"},
    {11, 22, 0, "Mensagem 2: 15:28:00"},
    {11, 23, 0, "Mensagem 3: 15:23:00"}
};
const int num_events = sizeof(events) / sizeof(events[0]);

// Variável global para indicar se o LED deve estar aceso
volatile bool led_active = false;
volatile uint32_t last_button_press_time = 0; // Para debouncing do botão

// Função de interrupção para o botão
static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t current_time = xTaskGetTickCountFromISR();
    if (current_time - last_button_press_time > pdMS_TO_TICKS(200)) { // Debounce de 200 ms
        last_button_press_time = current_time;
        led_active = false; // Atualiza o estado global
        gpio_set_level(LED_PIN, 0); // Apaga o LED imediatamente
        gpio_set_level(LED_PIN2, 0); // Apaga o LED imediatamente
        gpio_set_level(BUZZER_PIN, 1); // Silencia o buzzer imediatamente
    }
}

void configure_button() {
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE // Interrupção na borda de descida
    };
    gpio_config(&button_config);
    gpio_install_isr_service(0); // Instala o serviço de interrupção
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);
}



static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Tentando reconectar ao Wi-Fi...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Conectado ao Wi-Fi.");
    }
}

void wifi_init() {
    wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Inicializando Wi-Fi...");
}

char *url_encode(const unsigned char *str)
{
    static const char *hex = "0123456789abcdef";
    static char encoded[1024];
    char *p = encoded;
    while (*str)
    {
        if (isalnum(*str) || *str == '-' || *str == '_' || *str == '.' || *str == '~')
        {
            *p++ = *str;
        }
        else
        {
            *p++ = '%';
            *p++ = hex[*str >> 4];
            *p++ = hex[*str & 15];
        }
        str++;
    }
    *p = '\0';
    return encoded;
}

static void send_whatsapp_message(unsigned char *message)
{
    char callmebot_url[] = "https://api.callmebot.com/whatsapp.php?phone=%s&text=%s&apikey=%s";
    char URL[strlen(callmebot_url) + 30];
    sprintf(URL, callmebot_url, whatsapp_num, url_encode(message), api_key);
    ESP_LOGI(TAG, "URL = %s", URL);


    esp_http_client_config_t config = {
        .url = URL,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            ESP_LOGI(TAG, "Message sent Successfully");
        } else {
            ESP_LOGI(TAG, "Message sending Failed");
        }
    } else {
        ESP_LOGI(TAG, "Message nao entrou sending Failed");
    }
    esp_http_client_cleanup(client);
}


void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Hora sincronizada");
}

void initialize_sntp() {
    ESP_LOGI(TAG, "Inicializando SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "time.google.com");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

void check_and_send_message(struct tm *timeinfo) {
    // Verificar os eventos e acionar o LED se o horário corresponder
    for (int i = 0; i < num_events; i++) {
        if (timeinfo->tm_hour == events[i].hour && timeinfo->tm_min == events[i].minute && timeinfo->tm_sec == events[i].second) {
            ESP_LOGI(TAG, "%s", events[i].message);
            led_active = true;
            gpio_set_level(LED_PIN, 1); // Acender o LED
            gpio_set_level(BUZZER_PIN, 0); // ON Buzzer
            unsigned char test_message[] = "Hora de tomar seu remédio!!!";
            send_whatsapp_message(test_message);
        }
    }
}

void monitor_led_task(void *arg) {
    while (1) {
        if (led_active) {
            vTaskDelay(60000 / portTICK_PERIOD_MS); // Aguarda 60 segundos
            if (led_active) { // Verifica novamente antes de desativar
                led_active = false;
                gpio_set_level(LED_PIN, 0); // Desativa o LED
                gpio_set_level(BUZZER_PIN, 1); // Desativa o buzzer
            }
        } else {
            gpio_set_level(LED_PIN, 0); // Certifica que o LED está apagado
            gpio_set_level(BUZZER_PIN, 1); // Certifica que o buzzer está desligado
        }
        vTaskDelay(100 / portTICK_PERIOD_MS); // Evita alta utilização da CPU
    }
}

void repor_task(void *pvParameters) {
    // Configuração do GPIO para o LED_PIN2
    esp_rom_gpio_pad_select_gpio(LED_PIN2);
    gpio_set_direction(LED_PIN2, GPIO_MODE_OUTPUT);

    while (1) {
        ESP_LOGI(TAG, "LED_PIN2 acendendo por 10 segundos.");
        gpio_set_level(LED_PIN2, 1); // Acende o LED
        gpio_set_level(BUZZER_PIN, 0); // ativa o buzzer
        vTaskDelay(pdMS_TO_TICKS(10000)); // Espera 10 segundos


        ESP_LOGI(TAG, "LED_PIN2 apagando.");
        gpio_set_level(LED_PIN2, 0); // Apaga o LED
        gpio_set_level(BUZZER_PIN, 1); // Desativa o buzzer
        vTaskDelay(pdMS_TO_TICKS(604740000)); // Espera 7 dias 
    }
}

void app_main() {
    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Configurar GPIO
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);
/*
    gpio_config_t button_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE // Detectar borda de descida
    };
    gpio_config(&button_conf);

    // Configurar interrupção do botão
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);
*/

    configure_button();
    // Configurar o pino do buzzer como saída
    esp_rom_gpio_pad_select_gpio(BUZZER_PIN);
    gpio_set_direction(BUZZER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(BUZZER_PIN, 1);

    // Inicializar Wi-Fi
    wifi_init();

    // Esperar pela conexão Wi-Fi
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    // Configurar NTP
    initialize_sntp();

    // Esperar até o horário ser sincronizado
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;

    while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Aguardando sincronização do horário (%d/%d)...", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGE(TAG, "Falha ao sincronizar o horário.");
    } else {
        ESP_LOGI(TAG, "Horário sincronizado com sucesso.");
    }

    // Criar a tarefa para monitorar o LED
    xTaskCreatePinnedToCore(monitor_led_task, "monitor_led_task", 4096, NULL, 5, NULL,0);
    xTaskCreatePinnedToCore(repor_task, "repor_task", 4096, NULL, 5, NULL,1);

    // Exibir o horário atual
    char strftime_buf[64];
    setenv("TZ", "<-03>3", 1); // Ajustar fuso horário para Brasília (GMT-3)
    tzset();

    while (1) {
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Hora local: %s", strftime_buf);

        // Verificar os horários programados
        check_and_send_message(&timeinfo);

        // Aguardar 1 segundo antes de verificar novamente
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}