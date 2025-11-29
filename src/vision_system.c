#include "../include/common.h"
#include "../include/ipc_utils.h"

/* VARIABLES GLOBALES */

static volatile sig_atomic_t g_running = 1;     /* Flag de ejecución */
static int g_msgid = -1;                         /* ID de cola de mensajes */
static int g_shmid = -1;                         /* ID de memoria compartida */
static SharedMemory *g_shm = NULL;               /* Puntero a memoria compartida */

/* PROTOTIPOS DE FUNCIONES */

static void signal_handler(int sig);
static int init_ipc_resources(bool create);
static void cleanup_resources(void);
static Box generate_box(int box_id, const SystemParams *params);
static void print_box_info(const Box *box);
static int run_vision_loop(const SystemParams *params);

/* MANEJADOR DE SEÑALES */

/**
 * @brief Manejador de señales para terminación limpia
 */
static void signal_handler(int sig) {
    (void)sig;
    LOG_INFO("Señal recibida, terminando sistema de visión...");
    g_running = 0;
}

/* FUNCIONES DE INICIALIZACIÓN Y LIMPIEZA */

/**
 * @brief Inicializa los recursos IPC
 * @param create Si es true, crea los recursos; si no, solo los abre
 * @return 0 en éxito, -1 en error
 */
static int init_ipc_resources(bool create) {
    /* Crear/abrir cola de mensajes */
    g_msgid = ipc_create_message_queue(MSG_QUEUE_KEY, create);
    if (g_msgid == -1) {
        LOG_ERROR("No se pudo crear/abrir la cola de mensajes");
        return -1;
    }
    
    /* Crear/abrir memoria compartida */
    g_shmid = ipc_create_shared_memory(SHM_KEY, sizeof(SharedMemory), create);
    if (g_shmid == -1) {
        LOG_ERROR("No se pudo crear/abrir la memoria compartida");
        return -1;
    }
    
    /* Adjuntar memoria compartida */
    g_shm = (SharedMemory*)ipc_attach_shared_memory(g_shmid);
    if (g_shm == NULL) {
        LOG_ERROR("No se pudo adjuntar la memoria compartida");
        return -1;
    }
    
    /* Si creamos la memoria, inicializarla */
    if (create) {
        memset(g_shm, 0, sizeof(SharedMemory));
        g_shm->system_running = true;
        
        /* Inicializar mutex */
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&g_shm->mutex, &attr);
        pthread_mutexattr_destroy(&attr);
        
        /* Inicializar locks de mangos */
        for (int i = 0; i < MAX_MANGOS_PER_BOX; i++) {
            g_shm->mango_lock[i] = -1;
        }
    }
    
    LOG_INFO("Recursos IPC inicializados correctamente");
    return 0;
}

/**
 * @brief Libera todos los recursos utilizados
 */
static void cleanup_resources(void) {
    if (g_shm != NULL) {
        g_shm->system_running = false;
        ipc_detach_shared_memory(g_shm);
        g_shm = NULL;
    }
    
    /* Solo eliminamos recursos si somos el creador */
    /* En producción, esto se manejaría de manera más sofisticada */
    LOG_INFO("Recursos de visión liberados");
}

/* GENERACIÓN DE DATOS DE CAJAS */

/**
 * @brief Genera una caja con mangos en posiciones aleatorias
 * 
 * Simula el algoritmo de visión artificial que detecta mangos.
 * Las coordenadas son relativas al centroide de la caja.
 * 
 * @param box_id Identificador de la caja
 * @param params Parámetros del sistema
 * @return Estructura Box con datos generados
 */
static Box generate_box(int box_id, const SystemParams *params) {
    Box box;
    memset(&box, 0, sizeof(Box));
    
    box.id = box_id;
    box.entry_time = GET_TIME_S();
    box.position = 0.0; /* Inicio de la banda */
    box.completed = false;
    box.labeled_count = 0;
    
    /* Generar número aleatorio de mangos entre N_min y N_max */
    box.num_mangos = random_int(params->N_min, params->N_max);
    
    /* Límites de la caja (coordenadas relativas al centroide) */
    double half_size = params->Z / 2.0;
    double margin = params->Z / 10.0; /* Margen del borde */
    
    /* Generar posiciones para cada mango */
    for (int i = 0; i < box.num_mangos; i++) {
        box.mangos[i].id = i;
        box.mangos[i].state = MANGO_UNLABELED;
        box.mangos[i].labeled_by_robot = -1;
        box.mangos[i].label_time = 0.0;
        
        /* Generar posición aleatoria dentro de la caja */
        /* Evitamos que los mangos se superpongan (simplificación) */
        bool valid_position = false;
        int attempts = 0;
        const int max_attempts = 100;
        const double min_distance = params->Z / 15.0; /* Distancia mínima entre mangos */
        
        while (!valid_position && attempts < max_attempts) {
            box.mangos[i].x = random_range(-half_size + margin, half_size - margin);
            box.mangos[i].y = random_range(-half_size + margin, half_size - margin);
            
            valid_position = true;
            
            /* Verificar distancia con mangos anteriores */
            for (int j = 0; j < i && valid_position; j++) {
                double dist = distance(box.mangos[i].x, box.mangos[i].y,
                                      box.mangos[j].x, box.mangos[j].y);
                if (dist < min_distance) {
                    valid_position = false;
                }
            }
            attempts++;
        }
        
        /* Si no encontramos posición válida, usar la última generada */
        if (!valid_position) {
            LOG_WARN("No se encontró posición óptima para mango %d en caja %d", i, box_id);
        }
    }
    
    return box;
}

/**
 * @brief Imprime información de una caja para depuración
 */
static void print_box_info(const Box *box) {
    if (box == NULL) return;
    
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║ CAJA #%d - Detectada por Sistema de Visión                   ║\n", box->id);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Número de mangos: %-4d                                       ║\n", box->num_mangos);
    printf("║ Posición inicial: %.2f cm                                   ║\n", box->position);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Coordenadas de mangos (relativas al centroide):              ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    for (int i = 0; i < box->num_mangos; i++) {
        printf("║   Mango %2d: (%.2f, %.2f) cm                             ║\n",
               box->mangos[i].id, box->mangos[i].x, box->mangos[i].y);
    }
    
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

/* BUCLE PRINCIPAL DE VISIÓN */

/**
 * @brief Ejecuta el bucle principal del sistema de visión
 * 
 * Genera cajas con mangos y las envía al sistema de control de robots.
 * 
 * @param params Parámetros del sistema
 * @return 0 en éxito, -1 en error
 */
static int run_vision_loop(const SystemParams *params) {
    int box_id = 0;
    
    /* Calcular intervalo entre cajas basado en velocidad de banda y espaciado */
    double box_interval = params->box_spacing / params->X;
    
    LOG_INFO("Iniciando bucle de visión artificial");
    LOG_INFO("Intervalo entre cajas: %.2f segundos", box_interval);
    
    while (g_running) {
        /* Generar nueva caja */
        Box box = generate_box(box_id, params);
        
        /* Mostrar información de la caja */
        print_box_info(&box);
        
        /* Actualizar memoria compartida con la caja actual */
        pthread_mutex_lock(&g_shm->mutex);
        memcpy(&g_shm->current_box, &box, sizeof(Box));
        g_shm->stats.total_boxes++;
        g_shm->stats.total_mangos += box.num_mangos;
        pthread_mutex_unlock(&g_shm->mutex);
        
        /* Enviar datos de la caja por cola de mensajes */
        if (ipc_send_box_data(g_msgid, &box) == -1) {
            LOG_ERROR("Error al enviar datos de caja %d", box_id);
            /* Continuamos aunque falle un envío */
        } else {
            LOG_INFO("Caja %d enviada al sistema de control (%d mangos)", 
                     box_id, box.num_mangos);
        }
        
        box_id++;
        
        /* Esperar intervalo entre cajas */
        usleep((useconds_t)(box_interval * 1000000));
    }
    
    return 0;
}

/* FUNCIÓN PRINCIPAL */

/**
 * @brief Muestra ayuda de uso del programa
 */
static void print_usage(const char *prog_name) {
    printf("\nUso: %s [opciones]\n\n", prog_name);
    printf("Sistema de Visión Artificial - Mangosa S.A.\n\n");
    printf("Opciones:\n");
    printf("  -x <valor>    Velocidad de la banda (cm/s) [default: 10]\n");
    printf("  -z <valor>    Tamaño de la caja (cm) [default: 50]\n");
    printf("  -w <valor>    Longitud de la banda de trabajo (cm) [default: 300]\n");
    printf("  -n <valor>    Número mínimo de mangos por caja [default: 10]\n");
    printf("  -c            Modo crear (crear nuevos recursos IPC)\n");
    printf("  -h            Mostrar esta ayuda\n\n");
    printf("Ejemplo:\n");
    printf("  %s -x 15 -z 40 -n 8 -c\n\n", prog_name);
}

int main(int argc, char *argv[]) {
    SystemParams params;
    bool create_mode = false;
    int opt;
    
    /* Valores por defecto */
    params.X = 10.0;        /* cm/s */
    params.Z = 50.0;        /* cm */
    params.W = 300.0;       /* cm */
    params.N_min = 10;
    params.N_max = 12;      /* 1.2 * N_min */
    params.B = 0.0;         /* Sin fallas por defecto */
    params.robot_speed = params.Z / 10.0;
    params.box_spacing = params.Z * 1.5; /* Espaciado entre cajas */
    
    /* Parsear argumentos */
    while ((opt = getopt(argc, argv, "x:z:w:n:ch")) != -1) {
        switch (opt) {
            case 'x':
                params.X = atof(optarg);
                if (params.X <= 0) {
                    LOG_ERROR("Velocidad de banda debe ser positiva");
                    return EXIT_FAILURE;
                }
                break;
            case 'z':
                params.Z = atof(optarg);
                if (params.Z <= 0) {
                    LOG_ERROR("Tamaño de caja debe ser positivo");
                    return EXIT_FAILURE;
                }
                params.robot_speed = params.Z / 10.0;
                params.box_spacing = params.Z * 1.5;
                break;
            case 'w':
                params.W = atof(optarg);
                if (params.W <= 0) {
                    LOG_ERROR("Longitud de banda debe ser positiva");
                    return EXIT_FAILURE;
                }
                break;
            case 'n':
                params.N_min = atoi(optarg);
                if (params.N_min <= 0) {
                    LOG_ERROR("Número de mangos debe ser positivo");
                    return EXIT_FAILURE;
                }
                params.N_max = (int)(params.N_min * 1.2);
                break;
            case 'c':
                create_mode = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    /* Inicializar generador aleatorio */
    srand((unsigned int)time(NULL));
    
    /* Configurar manejadores de señales */
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     SISTEMA DE VISIÓN ARTIFICIAL - MANGOSA S.A.              ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Parámetros de operación:                                     ║\n");
    printf("║   - Velocidad de banda (X): %.2f cm/s                       ║\n", params.X);
    printf("║   - Tamaño de caja (Z): %.2f cm                             ║\n", params.Z);
    printf("║   - Longitud de banda (W): %.2f cm                          ║\n", params.W);
    printf("║   - Mangos por caja: %d a %d                                 ║\n", params.N_min, params.N_max);
    printf("║   - Velocidad del robot: %.2f cm/s (Z/10)                   ║\n", params.robot_speed);
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    /* Inicializar recursos IPC */
    if (init_ipc_resources(create_mode) != 0) {
        LOG_ERROR("Fallo en inicialización de IPC");
        cleanup_resources();
        return EXIT_FAILURE;
    }
    
    /* Guardar parámetros en memoria compartida */
    pthread_mutex_lock(&g_shm->mutex);
    memcpy(&g_shm->params, &params, sizeof(SystemParams));
    pthread_mutex_unlock(&g_shm->mutex);
    
    /* Ejecutar bucle principal */
    int result = run_vision_loop(&params);
    
    /* Limpieza */
    cleanup_resources();
    
    LOG_INFO("Sistema de visión terminado");
    return (result == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
