#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "lib/aht20.h"
#include "lib/bmp280.h"
#include "lib/ssd1306.h"
#include "lib/font.h"

//CONFIGURAÇÕES 

//I2C dos sensores
#define I2C_PORT i2c0
#define I2C_SDA 0
#define I2C_SCL 1

//I2C do Display
#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define DISPLAY_ADDR 0x3C

//GPIOs
#define BOTAO_A 5
#define BUZZER_PIN 21
#define RED_PIN 13
#define BLUE_PIN 12
#define GREEN_PIN 11

//Constantes
#define SEA_LEVEL_PRESSURE 101325.0
#define UPDATE_INTERVAL_MS 1000
#define MAX_DATA_POINTS 50
#define DEBOUNCE_DELAY_MS 200

//ESTRUTURAS DE DADOS

//Estrutura para transferência via FIFO entre cores
typedef struct {
    float temperature;
    float humidity;
    float pressure;
    float altitude;
    uint32_t timestamp;
} SensorData;

typedef struct {
    float temp_min;
    float temp_max;
    float humid_min;
    float humid_max;
    float press_min;
    float press_max;
    float temp_offset;
    float humid_offset;
    float press_offset;
} Config;

typedef struct {
    float temperature[MAX_DATA_POINTS];
    float humidity[MAX_DATA_POINTS];
    float pressure[MAX_DATA_POINTS];
    int index;
    int count;
} HistoricalData;

//VARIÁVEIS GLOBAIS

Config config = {
    .temp_min = 10.0, .temp_max = 35.0,
    .humid_min = 20.0, .humid_max = 80.0,
    .press_min = 900.0, .press_max = 1100.0,
    .temp_offset = 0.0, .humid_offset = 0.0, .press_offset = 0.0
};

//Variáveis do Core 1
HistoricalData history = {0};
int current_page = 0;
bool alarm_active = false;
ssd1306_t ssd;

//Variáveis para controle do botão (Core 1)
volatile bool button_a_pressed = false;
volatile uint32_t last_button_time = 0;

//Último dado recebido (Core 1)
SensorData current_sensor_data = {0};

//PROTÓTIPOS DE FUNÇÕES

//Core 0 - Sensores
void core0_sensor_task(void);
float calculate_altitude(float pressure);

//Core 1 - Interface
void core1_interface_task(void);
void init_gpio_core1(void);
void init_i2c_display(void);
void update_display(void);
void handle_buttons(void);
void check_alarms(void);
void add_to_history(float temp, float humid, float press);
void gpio_callback(uint gpio, uint32_t events);

//Funções de controle de hardware
void set_rgb_led(uint8_t r, uint8_t g, uint8_t b);
void buzzer_beep(int duration_ms);

//FUNÇÕES DE DIAGNÓSTICO I2C

void i2c_scan(i2c_inst_t *i2c) {
    printf("\n[CORE0] Escaneando barramento I2C...\n");
    printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    for (int addr = 0; addr < (1 << 7); ++addr) {
        if (addr % 16 == 0) {
            printf("%02x ", addr);
        }

        int ret;
        uint8_t rxdata;
        ret = i2c_read_blocking(i2c, addr, &rxdata, 1, false);

        printf(ret < 0 ? "." : "@");
        printf(addr % 16 == 15 ? "\n" : "   ");
    }
    printf("\n");
    printf("[CORE0] Legenda: @ = dispositivo encontrado, . = sem resposta\n");
    printf("[CORE0] Endereços esperados: AHT20=0x38, BMP280=0x76 ou 0x77\n\n");
}

//CORE 0: AQUISIÇÃO DE DADOS

void core0_sensor_task(void) {
    //Inicializa I2C dos sensores
    printf("[CORE0] Inicializando I2C0 a 400kHz...\n");
    uint actual_baudrate = i2c_init(I2C_PORT, 400 * 1000);
    printf("[CORE0] I2C baudrate configurado: %u Hz\n", actual_baudrate);
    
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    
    printf("[CORE0] GPIOs I2C configurados: SDA=%d, SCL=%d\n", I2C_SDA, I2C_SCL);
    
    //Aguarda estabilização
    sleep_ms(100);
    
    //Escaneia barramento I2C
    i2c_scan(I2C_PORT);
    
    //Inicializa BMP280
    printf("[CORE0] Inicializando BMP280...\n");
    bmp280_init(I2C_PORT);
    sleep_ms(50);
    printf("[CORE0] BMP280 inicializado\n");
    
    //Inicializa AHT20
    printf("[CORE0] Resetando AHT20...\n");
    aht20_reset(I2C_PORT);
    sleep_ms(50);
    
    printf("[CORE0] Inicializando AHT20...\n");
    aht20_init(I2C_PORT);
    sleep_ms(50);
    printf("[CORE0] AHT20 inicializado\n");
    
    //Calibração do BMP280
    struct bmp280_calib_param bmp_params;
    bmp280_get_calib_params(I2C_PORT, &bmp_params);
    
    printf("[CORE0] Sensores inicializados - Iniciando leitura contínua\n");
    
    AHT20_Data aht_data;
    int32_t raw_temp_bmp, raw_pressure;
    float bmp_temperature, bmp_pressure, altitude;
    uint32_t last_update = 0;
    
    while (1) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        
        //Atualiza leituras a cada UPDATE_INTERVAL_MS
        if (now - last_update >= UPDATE_INTERVAL_MS) {
            last_update = now;
            
            //Lê AHT20 (Temperatura e Umidade)
            bool aht_ok = aht20_read(I2C_PORT, &aht_data);
            
            if (!aht_ok) {
                printf("[CORE0] ERRO: Falha na leitura do AHT20!\n");
                continue;
            }
            
            printf("[CORE0] AHT20 OK - T=%.2f°C U=%.2f%%\n", 
                   aht_data.temperature, aht_data.humidity);
            
            //Lê BMP280 (Pressão)
            bmp280_read_raw(I2C_PORT, &raw_temp_bmp, &raw_pressure);
            bmp_temperature = bmp280_convert_temp(raw_temp_bmp, &bmp_params) / 100.0;
            bmp_pressure = bmp280_convert_pressure(raw_pressure, raw_temp_bmp, &bmp_params) / 100.0;
            
            printf("[CORE0] BMP280 OK - T=%.2f°C P=%.2fhPa (raw_t=%d raw_p=%d)\n", 
                   bmp_temperature, bmp_pressure, raw_temp_bmp, raw_pressure);
            
            //Verifica se os valores são válidos
            if (bmp_pressure < 300.0 || bmp_pressure > 1200.0) {
                printf("[CORE0] AVISO: Pressão fora da faixa válida!\n");
            }
            
            //Calcula altitude
            altitude = calculate_altitude(bmp_pressure * 100);
            
            //Prepara pacote de dados
            SensorData data = {
                .temperature = aht_data.temperature,
                .humidity    = aht_data.humidity,
                .pressure    = bmp_pressure,
                .altitude    = altitude,
                .timestamp   = now
            };

            //Envia dados para Core 1 via FIFO
            uint32_t num_words = (sizeof(SensorData) + 3) / 4; 

            //Serializa com memcpy
            uint32_t words[num_words];
            memset(words, 0, sizeof(words));            
            memcpy(words, &data, sizeof(SensorData));   
            multicore_fifo_push_blocking(0xAAAAAAAA);
            for (uint32_t i = 0; i < num_words; i++) {
                multicore_fifo_push_blocking(words[i]);
            }
        }
        
        sleep_ms(10);
    }
}

float calculate_altitude(float pressure) {
    return 44330.0 * (1.0 - pow(pressure / SEA_LEVEL_PRESSURE, 0.1903));
}

//CORE 1: INTERFACE DO USUÁRIO

void core1_interface_task(void) {
    //Inicializa periféricos de interface
    init_gpio_core1();
    init_i2c_display();
    
    printf("[CORE1] Inicializado - Interface ativa\n");
    
    //Mensagem de inicialização no display
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "ESTACAO", 30, 20);
    ssd1306_draw_string(&ssd, "Aguardando", 20, 35);
    ssd1306_draw_string(&ssd, "sensores...", 20, 45);
    ssd1306_send_data(&ssd);
    
    uint32_t last_display_update = 0;
    bool data_received = false;
    
    while (1) {
        //Verifica se há dados disponíveis na FIFO
        if (multicore_fifo_rvalid()) {
            
            //Lê o marcador
            uint32_t marker = multicore_fifo_pop_blocking();
            
            //Verificar se ta certo
            if (marker == 0xAAAAAAAA) {
                uint32_t num_words = (sizeof(SensorData) + 3) / 4;
                uint32_t data_buffer[num_words];

                for (uint32_t i = 0; i < num_words; i++) {
                    data_buffer[i] = multicore_fifo_pop_blocking();
                }

                memset(&current_sensor_data, 0, sizeof(SensorData));
                memcpy(&current_sensor_data, data_buffer, sizeof(SensorData));
                
                //Aplica offsets de configuração
                current_sensor_data.temperature += config.temp_offset;
                current_sensor_data.humidity += config.humid_offset;
                current_sensor_data.pressure += config.press_offset;
                
                //Addiciona ao histórico
                add_to_history(
                    current_sensor_data.temperature,
                    current_sensor_data.humidity,
                    current_sensor_data.pressure
                );
                
                data_received = true;
                
                printf("[CORE1] Dados recebidos - T=%.1f°C U=%.1f%% P=%.1fhPa\n",
                    current_sensor_data.temperature,
                    current_sensor_data.humidity,
                    current_sensor_data.pressure);

            } else {
                //Error de sincronização
                printf("[CORE1] ERRO FIFO: Marcador esperado 0xAAAAAAAA, recebido 0x%08X\n", marker);
                //Tentativa de Sincronizar
                while(multicore_fifo_rvalid()) {
                    multicore_fifo_pop_blocking();
                }
            }
        }
        
        //Processa botão
        handle_buttons();
        
        //Verifica alarmes e atualiza LEDs
        if (data_received) {
            check_alarms();
        }
        
        //Atualiza display periodicamente
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_display_update >= 500 && data_received) {
            last_display_update = now;
            update_display();
        }
        
        sleep_ms(10);
    }
}

void init_gpio_core1(void) {
    //Inicializa botão A
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, gpio_callback);
    
    //Inicializa buzzer
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    
    //Inicializa LED RGB
    gpio_init(RED_PIN);
    gpio_set_dir(RED_PIN, GPIO_OUT);
    gpio_init(GREEN_PIN);
    gpio_set_dir(GREEN_PIN, GPIO_OUT);
    gpio_init(BLUE_PIN);
    gpio_set_dir(BLUE_PIN, GPIO_OUT);
    
    //Feedback de inicialização
    set_rgb_led(0, 0, 1);   // LED azul
    buzzer_beep(100);
}

void init_i2c_display(void) {
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);
    
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, DISPLAY_ADDR, I2C_PORT_DISP);
    ssd1306_config(&ssd);
}

void update_display(void) {
    char str[32];
    
    ssd1306_fill(&ssd, false);
    
    switch (current_page) {
        case 0:   //Página principal
            ssd1306_draw_string(&ssd, "ESTACAO", 30, 0);
            ssd1306_line(&ssd, 0, 10, 127, 10, true);
            
            sprintf(str, "T: %.1fC", current_sensor_data.temperature);
            ssd1306_draw_string(&ssd, str, 0, 15);
            
            sprintf(str, "U: %.1f%%", current_sensor_data.humidity);
            ssd1306_draw_string(&ssd, str, 0, 27);
            
            sprintf(str, "P: %.0fhPa", current_sensor_data.pressure);
            ssd1306_draw_string(&ssd, str, 0, 39);
            
            sprintf(str, "Alt: %.0fm", current_sensor_data.altitude);
            ssd1306_draw_string(&ssd, str, 0, 51);
            break;
            
        case 1:   //Página de limites (thresholds)
            ssd1306_draw_string(&ssd, "LIMITES", 35, 0);
            ssd1306_line(&ssd, 0, 10, 127, 10, true);
            
            sprintf(str, "T:%.0f-%.0fC", config.temp_min, config.temp_max);
            ssd1306_draw_string(&ssd, str, 0, 15);
            
            sprintf(str, "U:%.0f-%.0f%%", config.humid_min, config.humid_max);
            ssd1306_draw_string(&ssd, str, 0, 27);
            
            sprintf(str, "P:%.0f-%.0f", config.press_min, config.press_max);
            ssd1306_draw_string(&ssd, str, 0, 39);
            
            ssd1306_draw_string(&ssd, "Btn A: Menu", 0, 55);
            break;
            
        case 2:   //Página de estatísticas
            ssd1306_draw_string(&ssd, "HISTORICO", 25, 0);
            ssd1306_line(&ssd, 0, 10, 127, 10, true);
            
            sprintf(str, "Pontos: %d/%d", history.count, MAX_DATA_POINTS);
            ssd1306_draw_string(&ssd, str, 0, 15);
            
            if (history.count > 0) {
                float avg_temp = 0, avg_humid = 0;
                for (int i = 0; i < history.count; i++) {
                    avg_temp += history.temperature[i];
                    avg_humid += history.humidity[i];
                }
                avg_temp /= history.count;
                avg_humid /= history.count;
                
                sprintf(str, "T med: %.1fC", avg_temp);
                ssd1306_draw_string(&ssd, str, 0, 30);
                
                sprintf(str, "U med: %.1f%%", avg_humid);
                ssd1306_draw_string(&ssd, str, 0, 45);
            }
            break;
    }
    
    ssd1306_send_data(&ssd);
}

void add_to_history(float temp, float humid, float press) {
    history.temperature[history.index] = temp;
    history.humidity[history.index] = humid;
    history.pressure[history.index] = press;
    
    history.index = (history.index + 1) % MAX_DATA_POINTS;
    if (history.count < MAX_DATA_POINTS) {
        history.count++;
    }
}

void check_alarms(void) {
    bool temp_alarm = (current_sensor_data.temperature < config.temp_min || 
                       current_sensor_data.temperature > config.temp_max);
    bool humid_alarm = (current_sensor_data.humidity < config.humid_min || 
                        current_sensor_data.humidity > config.humid_max);
    bool press_alarm = (current_sensor_data.pressure < config.press_min || 
                        current_sensor_data.pressure > config.press_max);
    
    alarm_active = temp_alarm || humid_alarm || press_alarm;
    
    static bool led_state = false;
    static uint32_t last_toggle = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if (alarm_active) {
        //Pisca vermelho a cada 500ms
        if (now - last_toggle >= 500) {
            last_toggle = now;
            led_state = !led_state;
            
            if (led_state) {
                set_rgb_led(1, 0, 0);
                buzzer_beep(100);
            } else {
                set_rgb_led(0, 0, 0);
            }
        }
    } else {
        set_rgb_led(0, 1, 0);
    }
}

void gpio_callback(uint gpio, uint32_t events) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if (now - last_button_time < DEBOUNCE_DELAY_MS) {
        return;
    }
    last_button_time = now;

    if (gpio == BOTAO_A) {
        button_a_pressed = true;
    }
}

void handle_buttons(void) {
    if (button_a_pressed) {
        button_a_pressed = false;
        
        current_page = (current_page + 1) % 3;
        buzzer_beep(50);
        update_display();
        
        printf("[CORE1] Página alterada para: %d\n", current_page);
    }
}

//FUNÇÕES DE CONTROLE DE HARDWARE

void set_rgb_led(uint8_t r, uint8_t g, uint8_t b) {
    gpio_put(RED_PIN, r);
    gpio_put(GREEN_PIN, g);
    gpio_put(BLUE_PIN, b);
}

void buzzer_beep(int duration_ms) {
    if (duration_ms > 0 && duration_ms < 1000) {
        gpio_put(BUZZER_PIN, 1);
        sleep_ms(duration_ms);
        gpio_put(BUZZER_PIN, 0);
    }
}

//MAIN

int main() {
    stdio_init_all();
    sleep_ms(2000);
    printf("  ESTACAO METEOROLOGICA MULTICORE RP2040\n");

    //Inicia Core 1 com a tarefa de interface
    multicore_launch_core1(core1_interface_task);
    
    printf("[MAIN] Core 1 iniciado\n");
    sleep_ms(500);
    
    //Core 0 executa a tarefa de sensores
    printf("[MAIN] Iniciando Core 0...\n");
    core0_sensor_task();
    
    return 0;
}