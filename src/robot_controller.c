#include "../include/common.h"
#include "../include/ipc_utils.h"

/* VARIABLES GLOBALES */

static volatile sig_atomic_t g_running = 1;     /* Flag de ejecución */
static int g_msgid = -1;                         /* ID de cola de mensajes */
static int g_shmid = -1;                         /* ID de memoria compartida */
static int g_semid = -1;                         /* ID de semáforos */
static SharedMemory *g_shm = NULL;               /* Puntero a memoria compartida */
static pthread_t g_robot_threads[MAX_ROBOTS];    /* Hilos de robots */
static Robot g_robots[MAX_ROBOTS];               /* Estado local de robots */
static pthread_mutex_t g_mango_mutex = PTHREAD_MUTEX_INITIALIZER; /* Mutex para mangos */
static pthread_cond_t g_box_available = PTHREAD_COND_INITIALIZER; /* Condición de caja disponible */
static Box g_current_box;                        /* Caja actual en procesamiento */
static bool g_box_ready = false;                 /* Flag de caja lista */
static int g_num_robots = 0;                     /* Número de robots activos */
static SystemParams g_params;                    /* Parámetros del sistema */

/* PROTOTIPOS DE FUNCIONES */

static void signal_handler(int sig);
static int init_ipc_resources(void);
static void cleanup_resources(void);
static void* robot_thread_function(void *arg);
static int try_claim_mango(int robot_id, int mango_id);
static void release_mango_claim(int mango_id);
static double calculate_effective_time(int robot_id);
static bool can_robot_reach_mango(int robot_id, int mango_id, double current_time);
static int label_mango(Robot *robot, Mango *mango);
static void* box_receiver_thread(void *arg);
static int calculate_required_robots(const SystemParams *params, int num_mangos);
static void activate_robots(int count);
static void deactivate_robots(int count);
static int check_robot_failure(Robot *robot);
static void activate_backup_robot(int failed_robot_id);

/* MANEJADOR DE SEÑALES */

static void signal_handler(int sig) {
    (void)sig;
    LOG_INFO("Señal recibida, terminando controlador de robots...");
    g_running = 0;
    
    /* Despertar hilos bloqueados */
    pthread_cond_broadcast(&g_box_available);
}

/* FUNCIONES DE INICIALIZACIÓN Y LIMPIEZA */

/**
 * @brief Inicializa los recursos IPC (abre los existentes creados por vision_system)
 */
static int init_ipc_resources(void) {
    /* Abrir cola de mensajes existente */
    g_msgid = ipc_create_message_queue(MSG_QUEUE_KEY, false);
    if (g_msgid == -1) {
        LOG_ERROR("No se pudo abrir la cola de mensajes. ¿Está corriendo vision_system?");
        return -1;
    }
    
    /* Abrir memoria compartida existente */
    g_shmid = ipc_create_shared_memory(SHM_KEY, sizeof(SharedMemory), false);
    if (g_shmid == -1) {
        LOG_ERROR("No se pudo abrir la memoria compartida");
        return -1;
    }
    
    /* Adjuntar memoria compartida */
    g_shm = (SharedMemory*)ipc_attach_shared_memory(g_shmid);
    if (g_shm == NULL) {
        LOG_ERROR("No se pudo adjuntar la memoria compartida");
        return -1;
    }
    
    /* Crear semáforos para sincronización de mangos */
    g_semid = ipc_create_semaphores(SEM_KEY, MAX_MANGOS_PER_BOX + 1, true);
    if (g_semid == -1) {
        LOG_ERROR("No se pudo crear los semáforos");
        return -1;
    }
    
    /* Inicializar semáforos (todos disponibles) */
    for (int i = 0; i <= MAX_MANGOS_PER_BOX; i++) {
        ipc_init_semaphore(g_semid, i, 1);
    }
    
    /* Copiar parámetros de memoria compartida */
    pthread_mutex_lock(&g_shm->mutex);
    memcpy(&g_params, &g_shm->params, sizeof(SystemParams));
    pthread_mutex_unlock(&g_shm->mutex);
    
    LOG_INFO("Recursos IPC abiertos correctamente");
    return 0;
}

/**
 * @brief Libera todos los recursos
 */
static void cleanup_resources(void) {
    /* Esperar a que terminen los hilos de robots */
    for (int i = 0; i < g_num_robots; i++) {
        pthread_join(g_robot_threads[i], NULL);
        LOG_DEBUG("Robot %d terminado", i);
    }
    
    if (g_shm != NULL) {
        ipc_detach_shared_memory(g_shm);
        g_shm = NULL;
    }
    
    if (g_semid != -1) {
        ipc_remove_semaphores(g_semid);
        g_semid = -1;
    }
    
    pthread_mutex_destroy(&g_mango_mutex);
    pthread_cond_destroy(&g_box_available);
    
    LOG_INFO("Recursos del controlador liberados");
}

/* FUNCIONES DE CÁLCULO Y LÓGICA DE ROBOTS */

/**
 * @brief Calcula el número de robots necesarios según los mangos
 * 
 * Fórmula basada en:
 * - Tiempo disponible por robot: distancia_entre_ejes / velocidad_banda
 * - Tiempo por etiquetado: distancia_promedio / velocidad_robot
 * - Capacidad por robot: tiempo_disponible / tiempo_por_etiquetado
 */
static int calculate_required_robots(const SystemParams *params, int num_mangos) {
    /* Tiempo que tiene un robot para trabajar (caja frente a él) */
    double time_per_robot = params->robot_spacing / params->X;
    
    /* Tiempo promedio para etiquetar un mango (movimiento diagonal promedio) */
    double avg_distance = params->Z / 3.0; /* Distancia promedio al centro */
    double time_per_label = avg_distance / params->robot_speed;
    
    /* Número de mangos que puede etiquetar un robot */
    int mangos_per_robot = (int)(time_per_robot / time_per_label);
    if (mangos_per_robot < 1) mangos_per_robot = 1;
    
    /* Robots necesarios */
    int required = (num_mangos + mangos_per_robot - 1) / mangos_per_robot;
    
    LOG_DEBUG("Cálculo: tiempo/robot=%.2fs, tiempo/etiqueta=%.2fs, mangos/robot=%d, requeridos=%d",
              time_per_robot, time_per_label, mangos_per_robot, required);
    
    return required;
}

/**
 * @brief Calcula el tiempo efectivo de operación para un robot
 */
static double calculate_effective_time(int robot_id) {
    double robot_axis = g_robots[robot_id].axis_position;
    double next_axis = (robot_id + 1 < g_num_robots) ? 
                       g_robots[robot_id + 1].axis_position : 
                       g_params.W;
    
    return (next_axis - robot_axis) / g_params.X;
}

/**
 * @brief Verifica si un robot puede alcanzar un mango dado el tiempo restante
 */
__attribute__((unused))
static bool can_robot_reach_mango(int robot_id, int mango_id, double current_time) {
    (void)current_time; /* Parámetro para uso futuro */
    double box_pos = g_current_box.position;
    
    /* Posición del mango en coordenadas absolutas */
    double mango_x = box_pos + g_current_box.mangos[mango_id].x;
    
    /* Tiempo para que el mango llegue al eje del siguiente robot */
    double next_axis = (robot_id + 1 < g_num_robots) ?
                       g_robots[robot_id + 1].axis_position :
                       g_params.W;
    double time_remaining = (next_axis - mango_x) / g_params.X;
    
    /* Tiempo que tarda el robot en alcanzar el mango */
    double reach_time = calc_robot_reach_time(
        g_current_box.mangos[mango_id].x,
        g_current_box.mangos[mango_id].y,
        g_params.robot_speed
    );
    
    return reach_time < time_remaining;
}

/**
 * @brief Intenta reclamar un mango para etiquetado (exclusión mutua)
 * @return 0 si se obtuvo el mango, -1 si ya está tomado
 */
static int try_claim_mango(int robot_id, int mango_id) {
    if (mango_id < 0 || mango_id >= MAX_MANGOS_PER_BOX) {
        return -1;
    }
    
    pthread_mutex_lock(&g_mango_mutex);
    
    /* Verificar si el mango ya está tomado o etiquetado */
    if (g_current_box.mangos[mango_id].state != MANGO_UNLABELED) {
        pthread_mutex_unlock(&g_mango_mutex);
        return -1;
    }
    
    /* Reclamar el mango */
    g_current_box.mangos[mango_id].state = MANGO_BEING_LABELED;
    g_shm->mango_lock[mango_id] = robot_id;
    
    pthread_mutex_unlock(&g_mango_mutex);
    
    LOG_DEBUG("Robot %d reclamó mango %d", robot_id, mango_id);
    return 0;
}

/**
 * @brief Libera el reclamo de un mango (para uso en recuperación de errores)
 */
__attribute__((unused))
static void release_mango_claim(int mango_id) {
    pthread_mutex_lock(&g_mango_mutex);
    g_shm->mango_lock[mango_id] = -1;
    pthread_mutex_unlock(&g_mango_mutex);
}

/**
 * @brief Simula el proceso de etiquetado de un mango
 */
static int label_mango(Robot *robot, Mango *mango) {
    /* Calcular tiempo de movimiento al mango */
    double reach_time = calc_robot_reach_time(mango->x, mango->y, g_params.robot_speed);
    
    /* Simular tiempo de movimiento y etiquetado */
    usleep((useconds_t)(reach_time * 1000000));
    
    /* Marcar mango como etiquetado */
    pthread_mutex_lock(&g_mango_mutex);
    mango->state = MANGO_LABELED;
    mango->labeled_by_robot = robot->id;
    mango->label_time = GET_TIME_S();
    g_current_box.labeled_count++;
    pthread_mutex_unlock(&g_mango_mutex);
    
    robot->labels_placed++;
    
    /* Simular tiempo de regreso al centro (mitad del tiempo) */
    usleep((useconds_t)(reach_time * 500000));
    
    LOG_DEBUG("Robot %d etiquetó mango %d (%.2f, %.2f)", 
              robot->id, mango->id, mango->x, mango->y);
    
    return 0;
}

/**
 * @brief Verifica si un robot sufre una falla (basado en probabilidad B)
 */
static int check_robot_failure(Robot *robot) {
    if (robot->has_failed) {
        return 1;
    }
    
    if (g_params.B > 0 && !robot->is_backup) {
        double r = (double)rand() / RAND_MAX;
        if (r < g_params.B / 1000.0) { /* Probabilidad por ciclo */
            robot->has_failed = true;
            robot->state = ROBOT_STATE_FAILED;
            LOG_WARN("¡Robot %d ha fallado!", robot->id);
            
            pthread_mutex_lock(&g_shm->mutex);
            g_shm->stats.robot_failures++;
            pthread_mutex_unlock(&g_shm->mutex);
            
            /* Activar robot de respaldo */
            activate_backup_robot(robot->id);
            return 1;
        }
    }
    
    return 0;
}

/**
 * @brief Activa un robot de respaldo para reemplazar uno fallido
 */
static void activate_backup_robot(int failed_robot_id) {
    /* Buscar robot de respaldo disponible */
    for (int i = g_params.num_robots; i < g_num_robots; i++) {
        if (g_robots[i].is_backup && g_robots[i].state == ROBOT_STATE_DISABLED) {
            g_robots[i].state = ROBOT_STATE_BACKUP;
            g_robots[i].replacing_robot = failed_robot_id;
            g_robots[i].axis_position = g_robots[failed_robot_id].axis_position;
            
            pthread_mutex_lock(&g_shm->mutex);
            g_shm->stats.backup_activations++;
            g_shm->robots[i] = g_robots[i];
            pthread_mutex_unlock(&g_shm->mutex);
            
            LOG_INFO("Robot de respaldo %d activado reemplazando robot %d", i, failed_robot_id);
            return;
        }
    }
    
    LOG_WARN("No hay robots de respaldo disponibles para reemplazar robot %d", failed_robot_id);
}

/* FUNCIÓN DEL HILO DE ROBOT */

/**
 * @brief Función principal de cada hilo de robot
 * 
 * Cada robot es un hilo independiente que:
 * 1. Espera a que haya una caja disponible
 * 2. Busca mangos sin etiquetar en su zona de alcance
 * 3. Reclama y etiqueta mangos de forma exclusiva
 * 4. Regresa a posición inicial cuando termina
 */
static void* robot_thread_function(void *arg) {
    Robot *robot = (Robot*)arg;
    
    LOG_INFO("Robot %d iniciado (eje en %.2f cm)", robot->id, robot->axis_position);
    
    while (g_running && !robot->has_failed) {
        /* Verificar si este robot está activo */
        if (robot->state == ROBOT_STATE_DISABLED) {
            usleep(100000); /* 100ms */
            continue;
        }
        
        /* Esperar a que haya una caja disponible */
        pthread_mutex_lock(&g_mango_mutex);
        while (!g_box_ready && g_running) {
            pthread_cond_wait(&g_box_available, &g_mango_mutex);
        }
        pthread_mutex_unlock(&g_mango_mutex);
        
        if (!g_running) break;
        
        /* Verificar falla del robot */
        if (check_robot_failure(robot)) {
            break;
        }
        
        robot->state = ROBOT_STATE_ACTIVE;
        
        /* Procesar mangos mientras la caja esté en nuestra zona */
        double start_time = GET_TIME_S();
        double effective_time = calculate_effective_time(robot->id);
        
        while (g_running && !robot->has_failed) {
            double elapsed = GET_TIME_S() - start_time;
            if (elapsed >= effective_time) {
                break; /* Caja salió de nuestra zona */
            }
            
            /* Buscar mango sin etiquetar */
            int target_mango = -1;
            double min_distance = INFINITY;
            
            pthread_mutex_lock(&g_mango_mutex);
            for (int i = 0; i < g_current_box.num_mangos; i++) {
                if (g_current_box.mangos[i].state == MANGO_UNLABELED) {
                    /* Priorizar mangos más cercanos */
                    double dist = distance(0, 0, 
                                          g_current_box.mangos[i].x,
                                          g_current_box.mangos[i].y);
                    if (dist < min_distance) {
                        min_distance = dist;
                        target_mango = i;
                    }
                }
            }
            pthread_mutex_unlock(&g_mango_mutex);
            
            if (target_mango == -1) {
                /* No hay más mangos sin etiquetar */
                break;
            }
            
            /* Intentar reclamar el mango */
            if (try_claim_mango(robot->id, target_mango) == 0) {
                robot->state = ROBOT_STATE_LABELING;
                robot->current_mango = target_mango;
                
                /* Etiquetar el mango */
                label_mango(robot, &g_current_box.mangos[target_mango]);
                
                robot->current_mango = -1;
                robot->state = ROBOT_STATE_ACTIVE;
            }
        }
        
        /* Regresar a posición inicial */
        robot->state = ROBOT_STATE_RETURNING;
        usleep(50000); /* Simular regreso */
        robot->state = ROBOT_STATE_IDLE;
        
        /* Actualizar memoria compartida */
        pthread_mutex_lock(&g_shm->mutex);
        g_shm->robots[robot->id] = *robot;
        pthread_mutex_unlock(&g_shm->mutex);
    }
    
    LOG_INFO("Robot %d terminado (etiquetas colocadas: %d)", robot->id, robot->labels_placed);
    return NULL;
}

/* HILO RECEPTOR DE CAJAS */

/**
 * @brief Hilo que recibe cajas del sistema de visión
 */
static void* box_receiver_thread(void *arg) {
    (void)arg;
    
    LOG_INFO("Receptor de cajas iniciado");
    
    while (g_running) {
        Box new_box;
        
        /* Recibir nueva caja de la cola de mensajes */
        int result = ipc_receive_box_data(g_msgid, &new_box, true);
        
        if (result == -1) {
            if (!g_running) break;
            continue;
        }
        
        LOG_INFO("Caja %d recibida para procesamiento (%d mangos)", 
                 new_box.id, new_box.num_mangos);
        
        /* Calcular y ajustar robots activos según número de mangos */
        int required = calculate_required_robots(&g_params, new_box.num_mangos);
        if (required < g_params.num_robots) {
            deactivate_robots(g_params.num_robots - required);
        } else {
            activate_robots(required);
        }
        
        /* Actualizar caja actual y notificar a robots */
        pthread_mutex_lock(&g_mango_mutex);
        memcpy(&g_current_box, &new_box, sizeof(Box));
        
        /* Resetear locks de mangos */
        for (int i = 0; i < MAX_MANGOS_PER_BOX; i++) {
            g_shm->mango_lock[i] = -1;
        }
        
        g_box_ready = true;
        pthread_cond_broadcast(&g_box_available);
        pthread_mutex_unlock(&g_mango_mutex);
        
        /* Esperar mientras la caja atraviesa la banda */
        double transit_time = g_params.W / g_params.X;
        usleep((useconds_t)(transit_time * 1000000));
        
        /* Marcar caja como procesada */
        pthread_mutex_lock(&g_mango_mutex);
        g_box_ready = false;
        
        /* Actualizar estadísticas */
        pthread_mutex_lock(&g_shm->mutex);
        g_shm->stats.mangos_labeled += g_current_box.labeled_count;
        g_shm->stats.mangos_missed += (g_current_box.num_mangos - g_current_box.labeled_count);
        pthread_mutex_unlock(&g_shm->mutex);
        
        pthread_mutex_unlock(&g_mango_mutex);
        
        /* Reportar resultado */
        printf("\n╔══════════════════════════════════════════════════════════════╗\n");
        printf("║ RESULTADO CAJA #%d                                            ║\n", g_current_box.id);
        printf("╠══════════════════════════════════════════════════════════════╣\n");
        printf("║ Mangos etiquetados: %d / %d                                   ║\n", 
               g_current_box.labeled_count, g_current_box.num_mangos);
        if (g_current_box.labeled_count < g_current_box.num_mangos) {
            printf("║ ⚠ ADVERTENCIA: %d mangos sin etiquetar                      ║\n",
                   g_current_box.num_mangos - g_current_box.labeled_count);
        } else {
            printf("║ ✓ Todos los mangos fueron etiquetados                        ║\n");
        }
        printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    }
    
    LOG_INFO("Receptor de cajas terminado");
    return NULL;
}

/* FUNCIONES DE CONTROL DE ROBOTS */

/**
 * @brief Activa robots adicionales según la demanda
 */
static void activate_robots(int count) {
    int activated = 0;
    for (int i = 0; i < g_num_robots && activated < count; i++) {
        if (g_robots[i].state == ROBOT_STATE_DISABLED && !g_robots[i].has_failed) {
            g_robots[i].state = ROBOT_STATE_IDLE;
            activated++;
            LOG_DEBUG("Robot %d activado", i);
        }
    }
    
    pthread_mutex_lock(&g_shm->mutex);
    g_shm->active_robots += activated;
    pthread_mutex_unlock(&g_shm->mutex);
}

/**
 * @brief Desactiva robots innecesarios
 */
static void deactivate_robots(int count) {
    int deactivated = 0;
    /* Desactivar desde el final para mantener los primeros activos */
    for (int i = g_num_robots - 1; i >= 0 && deactivated < count; i--) {
        if (g_robots[i].state == ROBOT_STATE_IDLE && !g_robots[i].is_backup) {
            g_robots[i].state = ROBOT_STATE_DISABLED;
            deactivated++;
            LOG_DEBUG("Robot %d desactivado", i);
        }
    }
    
    pthread_mutex_lock(&g_shm->mutex);
    g_shm->active_robots -= deactivated;
    pthread_mutex_unlock(&g_shm->mutex);
}

/* FUNCIÓN DE INICIALIZACIÓN DE ROBOTS */

/**
 * @brief Inicializa los robots y sus posiciones
 */
static int init_robots(int num_robots, int num_backup) {
    int total_robots = num_robots + num_backup;
    if (total_robots > MAX_ROBOTS) {
        LOG_ERROR("Número de robots excede el máximo permitido");
        return -1;
    }
    
    g_num_robots = total_robots;
    g_params.num_robots = num_robots;
    g_params.num_backup_robots = num_backup;
    
    /* Calcular espaciado entre robots */
    g_params.robot_spacing = g_params.W / num_robots;
    
    /* Inicializar cada robot */
    for (int i = 0; i < total_robots; i++) {
        g_robots[i].id = i;
        g_robots[i].labels_placed = 0;
        g_robots[i].current_mango = -1;
        g_robots[i].last_action_time = 0;
        g_robots[i].failure_probability = g_params.B;
        g_robots[i].has_failed = false;
        g_robots[i].replacing_robot = -1;
        
        if (i < num_robots) {
            /* Robot principal */
            g_robots[i].axis_position = (i + 0.5) * g_params.robot_spacing;
            g_robots[i].state = ROBOT_STATE_IDLE;
            g_robots[i].is_backup = false;
        } else {
            /* Robot de respaldo */
            g_robots[i].axis_position = 0; /* Se asignará al activarse */
            g_robots[i].state = ROBOT_STATE_DISABLED;
            g_robots[i].is_backup = true;
        }
        
        /* Copiar a memoria compartida */
        pthread_mutex_lock(&g_shm->mutex);
        g_shm->robots[i] = g_robots[i];
        pthread_mutex_unlock(&g_shm->mutex);
    }
    
    pthread_mutex_lock(&g_shm->mutex);
    g_shm->active_robots = num_robots;
    pthread_mutex_unlock(&g_shm->mutex);
    
    LOG_INFO("Inicializados %d robots principales y %d de respaldo", num_robots, num_backup);
    LOG_INFO("Espaciado entre robots: %.2f cm", g_params.robot_spacing);
    
    return 0;
}

/* FUNCIÓN PRINCIPAL */

static void print_usage(const char *prog_name) {
    printf("\nUso: %s [opciones]\n\n", prog_name);
    printf("Controlador de Robots - Mangosa S.A.\n\n");
    printf("Opciones:\n");
    printf("  -r <num>      Número de robots principales [default: 4]\n");
    printf("  -b <num>      Número de robots de respaldo [default: 1]\n");
    printf("  -B <prob>     Probabilidad de falla de robot (0.0-1.0) [default: 0.0]\n");
    printf("  -h            Mostrar esta ayuda\n\n");
    printf("Ejemplo:\n");
    printf("  %s -r 6 -b 2 -B 0.05\n\n", prog_name);
}

int main(int argc, char *argv[]) {
    int num_robots = 4;
    int num_backup = 1;
    double failure_prob = 0.0;
    int opt;
    
    /* Parsear argumentos */
    while ((opt = getopt(argc, argv, "r:b:B:h")) != -1) {
        switch (opt) {
            case 'r':
                num_robots = atoi(optarg);
                if (num_robots <= 0 || num_robots > MAX_ROBOTS) {
                    LOG_ERROR("Número de robots inválido (1-%d)", MAX_ROBOTS);
                    return EXIT_FAILURE;
                }
                break;
            case 'b':
                num_backup = atoi(optarg);
                if (num_backup < 0) {
                    LOG_ERROR("Número de robots de respaldo inválido");
                    return EXIT_FAILURE;
                }
                break;
            case 'B':
                failure_prob = atof(optarg);
                if (failure_prob < 0 || failure_prob > 1) {
                    LOG_ERROR("Probabilidad de falla debe estar entre 0 y 1");
                    return EXIT_FAILURE;
                }
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
    srand((unsigned int)time(NULL) + getpid());
    
    /* Configurar manejadores de señales */
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     CONTROLADOR DE ROBOTS - MANGOSA S.A.                     ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Configuración:                                               ║\n");
    printf("║   - Robots principales: %-3d                                  ║\n", num_robots);
    printf("║   - Robots de respaldo: %-3d                                  ║\n", num_backup);
    printf("║   - Probabilidad falla: %.2f%%                               ║\n", failure_prob * 100);
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    /* Inicializar recursos IPC */
    if (init_ipc_resources() != 0) {
        LOG_ERROR("Fallo en inicialización de IPC");
        cleanup_resources();
        return EXIT_FAILURE;
    }
    
    /* Configurar probabilidad de falla */
    g_params.B = failure_prob;
    
    /* Inicializar robots */
    if (init_robots(num_robots, num_backup) != 0) {
        LOG_ERROR("Fallo en inicialización de robots");
        cleanup_resources();
        return EXIT_FAILURE;
    }
    
    /* Crear hilos de robots */
    for (int i = 0; i < g_num_robots; i++) {
        if (pthread_create(&g_robot_threads[i], NULL, robot_thread_function, &g_robots[i]) != 0) {
            LOG_ERROR("Error al crear hilo de robot %d", i);
            g_running = 0;
            cleanup_resources();
            return EXIT_FAILURE;
        }
    }
    
    /* Crear hilo receptor de cajas */
    pthread_t receiver_thread;
    if (pthread_create(&receiver_thread, NULL, box_receiver_thread, NULL) != 0) {
        LOG_ERROR("Error al crear hilo receptor");
        g_running = 0;
        cleanup_resources();
        return EXIT_FAILURE;
    }
    
    /* Esperar a que termine el receptor */
    pthread_join(receiver_thread, NULL);
    
    /* Limpieza */
    cleanup_resources();
    
    /* Mostrar estadísticas finales */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     ESTADÍSTICAS FINALES                                     ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    for (int i = 0; i < num_robots; i++) {
        printf("║ Robot %2d: %4d etiquetas colocadas                          ║\n",
               i, g_robots[i].labels_placed);
    }
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    LOG_INFO("Controlador de robots terminado");
    return EXIT_SUCCESS;
}
