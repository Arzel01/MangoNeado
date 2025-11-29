#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdarg.h>

/* Constantes del sistema */
#define IPC_KEY_BASE        0x4D414E47
#define MSG_QUEUE_KEY       (IPC_KEY_BASE + 1)
#define SHM_KEY             (IPC_KEY_BASE + 2)
#define SEM_KEY             (IPC_KEY_BASE + 3)

/* Límites del sistema */
#define MAX_ROBOTS          32          /* Número máximo de robots soportados */
#define MAX_MANGOS_PER_BOX  100         /* Máximo de mangos por caja */
#define MAX_BOXES_QUEUE     50          /* Máximo de cajas en cola de espera */
#define MAX_MSG_SIZE        4096        /* Tamaño máximo de mensaje IPC */

/* Tipos de mensajes para la cola de mensajes */
#define MSG_TYPE_BOX_DATA       1L      /* Datos de caja detectada por visión */
#define MSG_TYPE_ROBOT_STATUS   2L      /* Estado de robot */
#define MSG_TYPE_CONTROL        3L      /* Comandos de control */
#define MSG_TYPE_ACK            4L      /* Confirmación */
#define MSG_TYPE_SHUTDOWN       99L     /* Señal de apagado */

/* Estados del robot */
typedef enum {
    ROBOT_STATE_IDLE,           /* Robot en posición inicial, esperando */
    ROBOT_STATE_ACTIVE,         /* Robot activo, procesando mangos */
    ROBOT_STATE_LABELING,       /* Robot etiquetando un mango específico */
    ROBOT_STATE_RETURNING,      /* Robot regresando a posición inicial */
    ROBOT_STATE_DISABLED,       /* Robot deshabilitado por baja carga */
    ROBOT_STATE_FAILED,         /* Robot con falla */
    ROBOT_STATE_BACKUP          /* Robot de respaldo activado */
} RobotState;

/* Estados del mango */
typedef enum {
    MANGO_UNLABELED,            /* Mango sin etiquetar */
    MANGO_BEING_LABELED,        /* Mango siendo etiquetado */
    MANGO_LABELED               /* Mango ya etiquetado */
} MangoState;

/* Estructuras de datos */

/**
 * @struct Mango
 * @brief Representa un mango dentro de una caja
 * 
 * Las coordenadas son relativas al centroide de la caja.
 * x: desplazamiento horizontal (-Z/2 a Z/2)
 * y: desplazamiento vertical (-Z/2 a Z/2)
 */
typedef struct {
    int id;                     /* Identificador único del mango en la caja */
    double x;                   /* Coordenada X relativa al centroide (cm) */
    double y;                   /* Coordenada Y relativa al centroide (cm) */
    MangoState state;           /* Estado actual del mango */
    int labeled_by_robot;       /* ID del robot que lo etiquetó (-1 si no etiquetado) */
    double label_time;          /* Tiempo cuando fue etiquetado */
} Mango;

/**
 * @struct Box
 * @brief Representa una caja en la banda transportadora
 */
typedef struct {
    int id;                     /* Identificador único de la caja */
    int num_mangos;             /* Número de mangos en la caja */
    Mango mangos[MAX_MANGOS_PER_BOX]; /* Array de mangos */
    double position;            /* Posición actual en la banda (cm desde inicio) */
    double entry_time;          /* Tiempo de entrada al sistema */
    int labeled_count;          /* Número de mangos ya etiquetados */
    bool completed;             /* True si todos los mangos fueron etiquetados */
} Box;

/**
 * @struct Robot
 * @brief Representa un brazo robot
 */
typedef struct {
    int id;                     /* Identificador del robot (0 a MAX_ROBOTS-1) */
    double axis_position;       /* Posición del eje de rotación en la banda (cm) */
    RobotState state;           /* Estado actual */
    int current_mango;          /* ID del mango que está etiquetando (-1 si ninguno) */
    int labels_placed;          /* Contador de etiquetas colocadas */
    double last_action_time;    /* Timestamp de última acción */
    bool is_backup;             /* True si es robot de respaldo */
    int replacing_robot;        /* ID del robot que reemplaza (-1 si no aplica) */
    double failure_probability; /* Probabilidad de falla (0.0 a 1.0) */
    bool has_failed;            /* True si el robot ha fallado */
} Robot;

/**
 * @struct SystemParams
 * @brief Parámetros operativos del sistema
 */
typedef struct {
    double X;                   /* Velocidad de la banda (cm/s) */
    double Z;                   /* Tamaño de la caja (cm) */
    double W;                   /* Longitud total de la banda de trabajo (cm) */
    int num_robots;             /* Número de robots instalados */
    int N_min;                  /* Número mínimo de mangos por caja */
    int N_max;                  /* Número máximo de mangos por caja (1.2N) */
    double B;                   /* Probabilidad de falla de robot */
    double robot_speed;         /* Velocidad del robot Z/10 (cm/s) */
    double robot_spacing;       /* Distancia entre ejes de robots */
    double box_spacing;         /* Distancia entre cajas */
    int num_backup_robots;      /* Número de robots de respaldo */
} SystemParams;

/**
 * @struct SimulationStats
 * @brief Estadísticas de la simulación
 */
typedef struct {
    int total_boxes;            /* Total de cajas procesadas */
    int total_mangos;           /* Total de mangos procesados */
    int mangos_labeled;         /* Total de mangos etiquetados */
    int mangos_missed;          /* Mangos que no pudieron ser etiquetados */
    int robot_failures;         /* Número de fallas de robot */
    int backup_activations;     /* Veces que se activaron robots de respaldo */
    double avg_labels_per_robot[MAX_ROBOTS]; /* Etiquetas promedio por robot */
    double simulation_time;     /* Tiempo total de simulación */
    double throughput;          /* Cajas por segundo */
} SimulationStats;

/* ESTRUCTURAS IPC */

/**
 * @struct BoxMessage
 * @brief Mensaje para transmitir datos de caja por cola de mensajes
 */
typedef struct {
    long mtype;                 /* Tipo de mensaje (MSG_TYPE_BOX_DATA) */
    Box box;                    /* Datos de la caja */
} BoxMessage;

/**
 * @struct RobotStatusMessage
 * @brief Mensaje de estado del robot
 */
typedef struct {
    long mtype;                 /* Tipo de mensaje (MSG_TYPE_ROBOT_STATUS) */
    int robot_id;               /* ID del robot */
    RobotState state;           /* Estado actual */
    int labels_placed;          /* Etiquetas colocadas */
} RobotStatusMessage;

/**
 * @struct ControlMessage
 * @brief Mensaje de control del sistema
 */
typedef struct {
    long mtype;                 /* Tipo de mensaje */
    int command;                /* Comando a ejecutar */
    int target_robot;           /* Robot objetivo (-1 para todos) */
    int value;                  /* Valor asociado al comando */
} ControlMessage;

/**
 * @struct SharedMemory
 * @brief Estructura de memoria compartida entre procesos
 */
typedef struct {
    SystemParams params;                    /* Parámetros del sistema */
    Robot robots[MAX_ROBOTS];               /* Estado de todos los robots */
    Box current_box;                        /* Caja actual en procesamiento */
    int active_robots;                      /* Número de robots activos */
    bool system_running;                    /* Flag de sistema activo */
    pthread_mutex_t mutex;                  /* Mutex para sincronización */
    int mango_lock[MAX_MANGOS_PER_BOX];    /* Lock por mango (ID del robot o -1) */
    SimulationStats stats;                  /* Estadísticas */
} SharedMemory;

/* MACROS DE UTILIDAD */

/* Logging con niveles */
#define LOG_LEVEL_ERROR   0
#define LOG_LEVEL_WARN    1
#define LOG_LEVEL_INFO    2
#define LOG_LEVEL_DEBUG   3

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

/* Función de logging para evitar problemas con macros variádicas */
static inline void log_message(int level, const char *prefix, FILE *stream, const char *fmt, ...) {
    if (LOG_LEVEL >= level) {
        va_list args;
        va_start(args, fmt);
        fprintf(stream, "%s", prefix);
        vfprintf(stream, fmt, args);
        fprintf(stream, "\n");
        va_end(args);
    }
}

#define LOG_ERROR(...) log_message(LOG_LEVEL_ERROR, "[ERROR] ", stderr, __VA_ARGS__)
#define LOG_WARN(...)  log_message(LOG_LEVEL_WARN, "[WARN] ", stderr, __VA_ARGS__)
#define LOG_INFO(...)  log_message(LOG_LEVEL_INFO, "[INFO] ", stdout, __VA_ARGS__)
#define LOG_DEBUG(...) log_message(LOG_LEVEL_DEBUG, "[DEBUG] ", stdout, __VA_ARGS__)

/* Validación de parámetros */
#define VALIDATE_POSITIVE(val, name) \
    do { \
        if ((val) <= 0) { \
            LOG_ERROR("Parámetro inválido: %s debe ser positivo (valor: %g)", name, (double)(val)); \
            return -1; \
        } \
    } while(0)

#define VALIDATE_RANGE(val, min, max, name) \
    do { \
        if ((val) < (min) || (val) > (max)) { \
            LOG_ERROR("Parámetro fuera de rango: %s debe estar entre %g y %g (valor: %g)", \
                      name, (double)(min), (double)(max), (double)(val)); \
            return -1; \
        } \
    } while(0)

/* Manejo seguro de recursos */
#define SAFE_FREE(ptr) \
    do { if ((ptr) != NULL) { free(ptr); (ptr) = NULL; } } while(0)

/* Cálculo de tiempo */
static inline double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static inline double get_time_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

#define GET_TIME_MS() get_time_ms()
#define GET_TIME_S() get_time_s()

/* FUNCIONES AUXILIARES INLINE */

/**
 * @brief Calcula la distancia entre dos puntos
 */
static inline double distance(double x1, double y1, double x2, double y2) {
    return sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}

/**
 * @brief Calcula el tiempo que tarda un robot en alcanzar un mango
 * @param robot_y Posición Y del robot (centro de la caja)
 * @param mango_x Coordenada X del mango relativa al centroide
 * @param mango_y Coordenada Y del mango relativa al centroide  
 * @param robot_speed Velocidad del brazo robot (Z/10 cm/s)
 * @return Tiempo en segundos
 */
static inline double calc_robot_reach_time(double mango_x, double mango_y, double robot_speed) {
    /* El robot parte del eje (posición 0,0 relativa) y se mueve al mango */
    double dist = distance(0, 0, mango_x, mango_y);
    return dist / robot_speed;
}

/**
 * @brief Verifica si un mango está en el rango de alcance de un robot
 * @param box_pos Posición actual de la caja
 * @param box_size Tamaño de la caja
 * @param robot_axis Posición del eje del robot
 * @param next_robot_axis Posición del eje del siguiente robot
 * @return true si el mango está en rango
 */
static inline bool mango_in_robot_range(double box_pos, double box_size,
                                        double robot_axis, double next_robot_axis) {
    double box_front = box_pos - box_size / 2;
    (void)box_size; /* Evitar warning */
    return (box_front >= robot_axis && box_front < next_robot_axis);
}

/**
 * @brief Genera un número aleatorio entre min y max
 */
static inline double random_range(double min, double max) {
    return min + (rand() / (double)RAND_MAX) * (max - min);
}

/**
 * @brief Genera un entero aleatorio entre min y max (inclusive)
 */
static inline int random_int(int min, int max) {
    return min + rand() % (max - min + 1);
}

#endif /* COMMON_H */
