#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// ----- Definición de estados -----
#define ESTADO_INICIAL     0
#define ESTADO_ERROR       1
#define ESTADO_ABRIENDO    2
#define ESTADO_ABIERTO     3
#define ESTADO_CERRANDO    4
#define ESTADO_CERRADO     5
#define ESTADO_DETENIDO    6
#define ESTADO_DESCONOCIDO 7

// ----- Definición de señales de E/S -----
#define LM_ACTIVO    1
#define LM_NOACTIVO  0
#define MOTOR_OFF    0
#define MOTOR_ON     1
#define LAMP_OFF     0
#define LAMP_ON      1

// ----- Pines GPIO (ajusta a tu esquema) -----
#define PIN_LSC        GPIO_NUM_12   // limit switch puerta cerrada
#define PIN_LSA        GPIO_NUM_13   // limit switch puerta abierta
#define PIN_MOTOR_A    GPIO_NUM_14   // motor de apertura
#define PIN_MOTOR_C    GPIO_NUM_27   // motor de cierre
#define PIN_LAMP       GPIO_NUM_26   // lámpara

// ----- Variables de estado -----
static int ESTADO_SIGUIENTE = ESTADO_INICIAL;
static int ESTADO_ANTERIOR  = ESTADO_INICIAL;
static int ESTADO_ACTUAL    = ESTADO_INICIAL;

// ----- Prototipos -----
int Func_ESTADO_INICIAL(void);
int Func_ESTADO_ERROR(void);
int Func_ESTADO_ABRIENDO(void);
int Func_ESTADO_ABIERTO(void);
int Func_ESTADO_CERRANDO(void);
int Func_ESTADO_CERRADO(void);
int Func_ESTADO_DETENIDO(void);
int Func_ESTADO_DESCONOCIDO(void);

// ----- Función de lectura de entradas -----
static inline int leer_switch(gpio_num_t pin) {
    return gpio_get_level(pin) ? LM_ACTIVO : LM_NOACTIVO;
}

// ----- Funciones de control de salidas -----
static inline void controlar_salida(gpio_num_t pin, int valor) {
    gpio_set_level(pin, valor);
}

// ----- Tarea de la máquina de estados -----
void Task_Maquina_Estados(void *pvParameters) {
    while (1) {
        switch (ESTADO_SIGUIENTE) {
            case ESTADO_INICIAL:
                ESTADO_SIGUIENTE = Func_ESTADO_INICIAL();
                break;
            case ESTADO_ERROR:
                ESTADO_SIGUIENTE = Func_ESTADO_ERROR();
                break;
            case ESTADO_ABRIENDO:
                ESTADO_SIGUIENTE = Func_ESTADO_ABRIENDO();
                break;
            case ESTADO_ABIERTO:
                ESTADO_SIGUIENTE = Func_ESTADO_ABIERTO();
                break;
            case ESTADO_CERRANDO:
                ESTADO_SIGUIENTE = Func_ESTADO_CERRANDO();
                break;
            case ESTADO_CERRADO:
                ESTADO_SIGUIENTE = Func_ESTADO_CERRADO();
                break;
            case ESTADO_DETENIDO:
                ESTADO_SIGUIENTE = Func_ESTADO_DETENIDO();
                break;
            case ESTADO_DESCONOCIDO:
                ESTADO_SIGUIENTE = Func_ESTADO_DESCONOCIDO();
                break;
            default:
                ESTADO_SIGUIENTE = ESTADO_ERROR;
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // ciclo cada 100 ms
    }
}

// ----- Implementación de funciones de estado -----
int Func_ESTADO_INICIAL(void) {
    ESTADO_ANTERIOR = ESTADO_ACTUAL;
    ESTADO_ACTUAL   = ESTADO_INICIAL;

    controlar_salida(PIN_MOTOR_A, MOTOR_OFF);
    controlar_salida(PIN_MOTOR_C, MOTOR_OFF);
    controlar_salida(PIN_LAMP,   LAMP_OFF);

    printf("[FSM] Estado Inicial\n");

    int lsa = leer_switch(PIN_LSA);
    int lsc = leer_switch(PIN_LSC);

    if (lsa == LM_ACTIVO && lsc == LM_NOACTIVO) return ESTADO_ABIERTO;
    if (lsa == LM_ACTIVO && lsc == LM_ACTIVO)   return ESTADO_ERROR;
    if (lsa == LM_NOACTIVO && lsc == LM_NOACTIVO) return ESTADO_DESCONOCIDO;
    return ESTADO_ERROR;
}

int Func_ESTADO_ERROR(void) {
    ESTADO_ANTERIOR = ESTADO_ACTUAL;
    ESTADO_ACTUAL   = ESTADO_ERROR;
    printf("[FSM] Estado Error!\n");
    // Podrías parpadear la lámpara o similar
    controlar_salida(PIN_LAMP, LAMP_ON);
    vTaskDelay(pdMS_TO_TICKS(500));
    controlar_salida(PIN_LAMP, LAMP_OFF);
    return ESTADO_INICIAL;
}

int Func_ESTADO_ABRIENDO(void) {
    ESTADO_ANTERIOR = ESTADO_ACTUAL;
    ESTADO_ACTUAL   = ESTADO_ABRIENDO;
    printf("[FSM] Estado Abriendo\n");
    controlar_salida(PIN_MOTOR_A, MOTOR_ON);
    vTaskDelay(pdMS_TO_TICKS(1000));  // simula tiempo de apertura
    controlar_salida(PIN_MOTOR_A, MOTOR_OFF);
    return ESTADO_ABIERTO;
}

int Func_ESTADO_ABIERTO(void) {
    ESTADO_ANTERIOR = ESTADO_ACTUAL;
    ESTADO_ACTUAL   = ESTADO_ABIERTO;
    printf("[FSM] Estado Abierto\n");
    vTaskDelay(pdMS_TO_TICKS(2000));  // espera antes de cerrar
    return ESTADO_CERRANDO;
}

int Func_ESTADO_CERRANDO(void) {
    ESTADO_ANTERIOR = ESTADO_ACTUAL;
    ESTADO_ACTUAL   = ESTADO_CERRANDO;
    printf("[FSM] Estado Cerrando\n");
    controlar_salida(PIN_MOTOR_C, MOTOR_ON);
    vTaskDelay(pdMS_TO_TICKS(1000));  // simula tiempo de cierre
    controlar_salida(PIN_MOTOR_C, MOTOR_OFF);
    return ESTADO_CERRADO;
}

int Func_ESTADO_CERRADO(void) {
    ESTADO_ANTERIOR = ESTADO_ACTUAL;
    ESTADO_ACTUAL   = ESTADO_CERRADO;
    printf("[FSM] Estado Cerrado\n");
    vTaskDelay(pdMS_TO_TICKS(2000));
    return ESTADO_DETENIDO;
}

int Func_ESTADO_DETENIDO(void) {
    ESTADO_ANTERIOR = ESTADO_ACTUAL;
    ESTADO_ACTUAL   = ESTADO_DETENIDO;
    printf("[FSM] Estado Detenido\n");
    return ESTADO_INICIAL;
}

int Func_ESTADO_DESCONOCIDO(void) {
    ESTADO_ANTERIOR = ESTADO_ACTUAL;
    ESTADO_ACTUAL   = ESTADO_DESCONOCIDO;
    printf("[FSM] Estado Desconocido\n");
    return ESTADO_ABRIENDO;
}

// ----- Punto de entrada de ESP-IDF -----
void app_main(void)
{
    // Configurar pines como entradas o salidas
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL<<PIN_LSC)|(1ULL<<PIN_LSA),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL<<PIN_MOTOR_A)|(1ULL<<PIN_MOTOR_C)|(1ULL<<PIN_LAMP);
    io_conf.mode         = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    // Crear la tarea de la FSM
    xTaskCreate(
        Task_Maquina_Estados,
        "Maquina_Estados",
        2048,      // tamaño de stack
        NULL,
        5,         // prioridad
        NULL
    );

    // `app_main` termina aquí; el scheduler de FreeRTOS sigue corriendo
}
