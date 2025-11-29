#include "../include/common.h"
#include "../include/ipc_utils.h"

/* Estructuras de simulación */
typedef struct {
    SystemParams params;
    int num_boxes;              /* Número de cajas a procesar */
    int num_robots;             /* Número de robots a usar */
    int num_backup;             /* Número de robots de respaldo */
    double failure_prob;        /* Probabilidad de falla */
    bool verbose;               /* Modo verbose */
} SimulationConfig;

typedef struct {
    int total_mangos;
    int labeled_mangos;
    int missed_mangos;
    int robot_failures;
    int backup_activations;
    double total_time;
    double efficiency;          /* Porcentaje de éxito */
    int labels_per_robot[MAX_ROBOTS];
} SimulationResult;

/* Variables globales */
static volatile sig_atomic_t g_running = 1;

static Box g_boxes[MAX_BOXES_QUEUE];
static int g_box_count = 0;
static int g_current_box_idx = 0;

static Robot g_robots[MAX_ROBOTS];
static int g_num_robots = 0;
static SimulationConfig g_config;
static SimulationResult g_result;

/* Prototipos de funciones */
static void signal_handler(int sig);
static void init_simulation(const SimulationConfig *config);
static SimulationResult run_single_simulation(const SimulationConfig *config);
static void run_batch_simulation(const SimulationConfig *base_config, 
                                  int n_min, int n_max, int step,
                                  const char *output_file);
static void run_failure_analysis(const SimulationConfig *base_config,
                                  double b_min, double b_max, double b_step,
                                  const char *output_file);
static void print_result(const SimulationResult *result);

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Generación de cajas */

/**
 * @brief Genera todas las cajas para la simulación
 */
static void generate_all_boxes(const SimulationConfig *config) {
    double half_size = config->params.Z / 2.0;
    double margin = config->params.Z / 10.0;
    double min_distance = config->params.Z / 15.0;
    
    for (int b = 0; b < config->num_boxes; b++) {
        g_boxes[b].id = b;
        g_boxes[b].entry_time = 0;
        g_boxes[b].position = 0;
        g_boxes[b].completed = false;
        g_boxes[b].labeled_count = 0;
        
        /* Número de mangos entre N_min y N_max */
        g_boxes[b].num_mangos = random_int(config->params.N_min, config->params.N_max);
        
        /* Generar posiciones de mangos */
        for (int i = 0; i < g_boxes[b].num_mangos; i++) {
            g_boxes[b].mangos[i].id = i;
            g_boxes[b].mangos[i].state = MANGO_UNLABELED;
            g_boxes[b].mangos[i].labeled_by_robot = -1;
            g_boxes[b].mangos[i].label_time = 0;
            
            bool valid = false;
            int attempts = 0;
            
            while (!valid && attempts < 100) {
                g_boxes[b].mangos[i].x = random_range(-half_size + margin, half_size - margin);
                g_boxes[b].mangos[i].y = random_range(-half_size + margin, half_size - margin);
                
                valid = true;
                for (int j = 0; j < i && valid; j++) {
                    double dist = distance(g_boxes[b].mangos[i].x, g_boxes[b].mangos[i].y,
                                          g_boxes[b].mangos[j].x, g_boxes[b].mangos[j].y);
                    if (dist < min_distance) valid = false;
                }
                attempts++;
            }
        }
    }
    
    g_box_count = config->num_boxes;
}

/* Inicialización */

static void init_simulation(const SimulationConfig *config) {
    memset(&g_result, 0, sizeof(SimulationResult));
    g_current_box_idx = 0;
    /* g_box_available = false; */  /* No usado en modelo secuencial */
    /* g_simulation_done = false; */  /* No usado */
    g_running = 1;
    
    /* Inicializar robots */
    g_num_robots = config->num_robots + config->num_backup;
    double robot_spacing = config->params.W / config->num_robots;
    
    for (int i = 0; i < g_num_robots; i++) {
        g_robots[i].id = i;
        g_robots[i].labels_placed = 0;
        g_robots[i].current_mango = -1;
        g_robots[i].last_action_time = 0;
        g_robots[i].failure_probability = config->failure_prob;
        g_robots[i].has_failed = false;
        g_robots[i].replacing_robot = -1;
        
        if (i < config->num_robots) {
            g_robots[i].axis_position = (i + 0.5) * robot_spacing;
            g_robots[i].state = ROBOT_STATE_IDLE;
            g_robots[i].is_backup = false;
        } else {
            g_robots[i].axis_position = 0;
            g_robots[i].state = ROBOT_STATE_DISABLED;
            g_robots[i].is_backup = true;
        }
    }
    
    /* Generar cajas */
    generate_all_boxes(config);
    
    /* Copiar configuración */
    memcpy(&g_config, config, sizeof(SimulationConfig));
}

/* Ejecución de simulación */

static SimulationResult run_single_simulation(const SimulationConfig *config) {
    SimulationResult result;
    memset(&result, 0, sizeof(SimulationResult));
    
    double start_time = GET_TIME_S();
    
    /* Inicializar */
    init_simulation(config);
    
    /* Verificar fallas al inicio */
    if (config->failure_prob > 0) {
        for (int i = 0; i < config->num_robots; i++) {
            Robot *robot = &g_robots[i];
            if (robot->is_backup) continue;
            
            double r = (double)rand() / RAND_MAX;
            if (r < config->failure_prob) {
                robot->has_failed = true;
                robot->state = ROBOT_STATE_FAILED;
                result.robot_failures++;
                
                /* Activar backup */
                for (int j = config->num_robots; j < g_num_robots; j++) {
                    if (g_robots[j].is_backup && g_robots[j].state == ROBOT_STATE_DISABLED) {
                        g_robots[j].state = ROBOT_STATE_BACKUP;
                        g_robots[j].replacing_robot = robot->id;
                        g_robots[j].axis_position = robot->axis_position;
                        result.backup_activations++;
                        break;
                    }
                }
            }
        }
    }
    
    /* Modelo físico: caja pasa secuencialmente por cada robot
     * Tiempo disponible por robot = (W / num_robots) / X */
    double robot_spacing = config->params.W / config->num_robots;
    double effective_time = robot_spacing / config->params.X;
    
    /* Procesar cada caja */
    for (int box_idx = 0; box_idx < g_box_count; box_idx++) {
        Box *box = &g_boxes[box_idx];
        result.total_mangos += box->num_mangos;
        
        /* Calcular mangos que puede etiquetar cada robot */
        double time_per_station = effective_time;
        /* Tiempo estimado por mango basado en velocidad robot y distancia promedio */
        const double avg_time_per_mango = config->params.Z / config->params.robot_speed / 1.5;
        int mangos_per_station = (int)(time_per_station / avg_time_per_mango);
        if (mangos_per_station < 1) mangos_per_station = 1;
        
        /* Caja pasa secuencialmente por cada robot */
        for (int robot_idx = 0; robot_idx < g_num_robots; robot_idx++) {
            Robot *robot = &g_robots[robot_idx];
            
            /* Verificar si robot está activo */
            if (robot->state == ROBOT_STATE_DISABLED || robot->has_failed) {
                continue;
            }
            
            /* Este robot puede etiquetar hasta 'mangos_per_station' mangos */
            int labeled_by_this_robot = 0;
            
            for (int i = 0; i < box->num_mangos && labeled_by_this_robot < mangos_per_station; i++) {
                if (box->mangos[i].state == MANGO_UNLABELED) {
                    /* Etiquetar este mango */
                    box->mangos[i].state = MANGO_LABELED;
                    box->mangos[i].labeled_by_robot = robot->id;
                    box->labeled_count++;
                    robot->labels_placed++;
                    labeled_by_this_robot++;
                }
            }
            
            /* Si ya se etiquetaron todos, no seguir */
            if (box->labeled_count >= box->num_mangos) {
                break;
            }
        }
        
        result.labeled_mangos += box->labeled_count;
        result.missed_mangos += (box->num_mangos - box->labeled_count);
        
        if (config->verbose) {
            printf("Caja %d: %d/%d etiquetados\n", box_idx, 
                   box->labeled_count, box->num_mangos);
        }
    }
    
    double end_time = GET_TIME_S();
    
    result.total_time = end_time - start_time;
    result.efficiency = (result.total_mangos > 0) ? 
                        (100.0 * result.labeled_mangos / result.total_mangos) : 0;
    
    for (int i = 0; i < g_num_robots; i++) {
        result.labels_per_robot[i] = g_robots[i].labels_placed;
    }
    
    return result;
}

/* ANÁLISIS DE NÚMERO DE ROBOTS */

/**
 * @brief Ejecuta simulaciones variando el número de robots
 * 
 * Genera datos para la curva de costo-efectividad
 */
static void run_batch_simulation(const SimulationConfig *base_config,
                                  int n_min, int n_max, int step,
                                  const char *output_file) {
    (void)n_min; (void)n_max; (void)step; /* Parámetros para versión futura */
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        LOG_ERROR("No se pudo crear archivo de salida: %s", output_file);
        return;
    }
    
    fprintf(fp, "# Análisis de número de robots vs eficiencia\n");
    fprintf(fp, "# X=%.2f cm/s, Z=%.2f cm, W=%.2f cm, N=%d-%d\n",
            base_config->params.X, base_config->params.Z, 
            base_config->params.W, base_config->params.N_min, base_config->params.N_max);
    fprintf(fp, "# Columnas: num_robots avg_efficiency min_efficiency max_efficiency avg_missed\n");
    
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     ANÁLISIS DE NÚMERO ÓPTIMO DE ROBOTS                      ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Robots │ Eficiencia │ Mínima │ Máxima │ Perdidos/caja       ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    for (int num_robots = 1; num_robots <= MAX_ROBOTS && num_robots * 
         (base_config->params.W / num_robots) >= base_config->params.Z; num_robots++) {
        
        SimulationConfig config = *base_config;
        config.num_robots = num_robots;
        
        /* Múltiples simulaciones para promediar */
        const int NUM_RUNS = 5;
        double total_eff = 0, min_eff = 100, max_eff = 0, total_missed = 0;
        
        for (int run = 0; run < NUM_RUNS; run++) {
            SimulationResult result = run_single_simulation(&config);
            total_eff += result.efficiency;
            total_missed += result.missed_mangos;
            if (result.efficiency < min_eff) min_eff = result.efficiency;
            if (result.efficiency > max_eff) max_eff = result.efficiency;
        }
        
        double avg_eff = total_eff / NUM_RUNS;
        double avg_missed = total_missed / (NUM_RUNS * config.num_boxes);
        
        fprintf(fp, "%d %.2f %.2f %.2f %.2f\n", 
                num_robots, avg_eff, min_eff, max_eff, avg_missed);
        
        printf("║   %2d   │   %5.1f%%   │ %5.1f%% │ %5.1f%% │     %.1f            ║\n",
               num_robots, avg_eff, min_eff, max_eff, avg_missed);
        
        /* Parar si ya alcanzamos 100% de eficiencia */
        if (avg_eff >= 99.9) {
            /* Calcular totales de mangos para mostrar */
            int total_mangos_all_boxes = 0;
            for (int i = 0; i < g_box_count; i++) {
                total_mangos_all_boxes += g_boxes[i].num_mangos;
            }
            
            printf("╠══════════════════════════════════════════════════════════════╣\n");
            printf("║ ✓ ÓPTIMO ENCONTRADO                                          ║\n");
            printf("╠══════════════════════════════════════════════════════════════╣\n");
            printf("║ Se necesitan %2d robots para etiquetar %d cajas             ║\n", 
                   num_robots, config.num_boxes);
            printf("║ Total de mangos procesados: %d mangos                       ║\n", 
                   total_mangos_all_boxes);
            printf("║ Eficiencia alcanzada: 100%% (todos etiquetados)              ║\n");
            break;
        }
    }
    
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    fclose(fp);
    printf("Datos guardados en: %s\n", output_file);
}

/* ANÁLISIS DE REDUNDANCIA */

/**
 * @brief Ejecuta simulaciones variando la probabilidad de falla
 */
static void run_failure_analysis(const SimulationConfig *base_config,
                                  double b_min, double b_max, double b_step,
                                  const char *output_file) {
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        LOG_ERROR("No se pudo crear archivo de salida: %s", output_file);
        return;
    }
    
    fprintf(fp, "# Análisis de fallas y redundancia\n");
    fprintf(fp, "# Columnas: prob_falla robots_sin_backup eff_sin_backup robots_con_backup eff_con_backup\n");
    
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     ANÁLISIS DE REDUNDANCIA Y FALLAS                         ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ P(falla) │ Sin backup │ Eff  │ Con backup │ Eff             ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    for (double b = b_min; b <= b_max; b += b_step) {
        /* Encontrar número óptimo sin backup */
        SimulationConfig config = *base_config;
        config.failure_prob = b;
        config.num_backup = 0;
        
        int opt_no_backup = 1;
        double eff_no_backup = 0;
        
        for (int r = 1; r <= MAX_ROBOTS/2; r++) {
            config.num_robots = r;
            SimulationResult result = run_single_simulation(&config);
            if (result.efficiency > eff_no_backup) {
                eff_no_backup = result.efficiency;
                opt_no_backup = r;
            }
            if (result.efficiency >= 99.9) break;
        }
        
        /* Encontrar número óptimo con backup */
        config.num_backup = (int)(base_config->num_robots * b) + 1;
        if (config.num_backup < 1) config.num_backup = 1;
        
        int opt_with_backup = 1;
        double eff_with_backup = 0;
        
        for (int r = 1; r <= MAX_ROBOTS/2; r++) {
            config.num_robots = r;
            SimulationResult result = run_single_simulation(&config);
            if (result.efficiency > eff_with_backup) {
                eff_with_backup = result.efficiency;
                opt_with_backup = r;
            }
            if (result.efficiency >= 99.9) break;
        }
        
        fprintf(fp, "%.2f %d %.2f %d %.2f\n", 
                b, opt_no_backup, eff_no_backup, opt_with_backup, eff_with_backup);
        
        printf("║  %.2f    │     %2d     │%5.1f%%│     %2d     │%5.1f%%          ║\n",
               b, opt_no_backup, eff_no_backup, opt_with_backup, eff_with_backup);
    }
    
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    fclose(fp);
    printf("Datos guardados en: %s\n", output_file);
}

/* IMPRESIÓN DE RESULTADOS */

static void print_result(const SimulationResult *result) {
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     RESULTADO DE SIMULACIÓN                                  ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Total de mangos:       %-6d                                ║\n", result->total_mangos);
    printf("║ Mangos etiquetados:    %-6d                                ║\n", result->labeled_mangos);
    printf("║ Mangos perdidos:       %-6d                                ║\n", result->missed_mangos);
    printf("║ Eficiencia:            %5.1f%%                               ║\n", result->efficiency);
    printf("║ Fallas de robot:       %-6d                                ║\n", result->robot_failures);
    printf("║ Backups activados:     %-6d                                ║\n", result->backup_activations);
    printf("║ Tiempo de simulación:  %.2f s                              ║\n", result->total_time);
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
}

/* FUNCIÓN PRINCIPAL */

static void print_usage(const char *prog_name) {
    printf("\nUso: %s [opciones]\n\n", prog_name);
    printf("Simulador de Etiquetado - Mangosa S.A.\n\n");
    printf("Opciones:\n");
    printf("  -x <valor>    Velocidad de la banda (cm/s) [default: 10]\n");
    printf("  -z <valor>    Tamaño de la caja (cm) [default: 50]\n");
    printf("  -w <valor>    Longitud de la banda de trabajo (cm) [default: 300]\n");
    printf("  -n <valor>    Número mínimo de mangos por caja [default: 10]\n");
    printf("  -r <valor>    Número de robots [default: 4]\n");
    printf("  -b <valor>    Número de robots de respaldo [default: 1]\n");
    printf("  -c <valor>    Número de cajas a simular [default: 20]\n");
    printf("  -B <valor>    Probabilidad de falla (0-1) [default: 0]\n");
    printf("  -a            Ejecutar análisis completo\n");
    printf("  -f            Ejecutar análisis de fallas\n");
    printf("  -v            Modo verbose\n");
    printf("  -h            Mostrar esta ayuda\n\n");
    printf("Ejemplos:\n");
    printf("  %s -x 15 -z 40 -n 12 -r 5 -c 50\n", prog_name);
    printf("  %s -a  # Análisis de número óptimo de robots\n", prog_name);
    printf("  %s -f  # Análisis de redundancia\n\n", prog_name);
}

int main(int argc, char *argv[]) {
    SimulationConfig config;
    bool run_analysis = false;
    bool run_failure_test = false;
    int opt;
    
    /* Valores por defecto */
    config.params.X = 10.0;
    config.params.Z = 50.0;
    config.params.W = 300.0;
    config.params.N_min = 10;
    config.params.N_max = 12;
    config.params.B = 0.0;
    config.params.robot_speed = config.params.Z / 10.0;
    config.params.box_spacing = config.params.Z * 1.5;
    config.params.robot_spacing = config.params.W / 4;
    config.num_robots = 4;
    config.num_backup = 1;
    config.num_boxes = 20;
    config.failure_prob = 0.0;
    config.verbose = false;
    
    /* Parsear argumentos */
    while ((opt = getopt(argc, argv, "x:z:w:n:r:b:c:B:afvh")) != -1) {
        switch (opt) {
            case 'x':
                config.params.X = atof(optarg);
                break;
            case 'z':
                config.params.Z = atof(optarg);
                config.params.robot_speed = config.params.Z / 10.0;
                config.params.box_spacing = config.params.Z * 1.5;
                break;
            case 'w':
                config.params.W = atof(optarg);
                break;
            case 'n':
                config.params.N_min = atoi(optarg);
                config.params.N_max = (int)(config.params.N_min * 1.2);
                break;
            case 'r':
                config.num_robots = atoi(optarg);
                break;
            case 'b':
                config.num_backup = atoi(optarg);
                break;
            case 'c':
                config.num_boxes = atoi(optarg);
                break;
            case 'B':
                config.failure_prob = atof(optarg);
                config.params.B = config.failure_prob;
                break;
            case 'a':
                run_analysis = true;
                break;
            case 'f':
                run_failure_test = true;
                break;
            case 'v':
                config.verbose = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    /* Validar parámetros */
    if (config.params.X <= 0 || config.params.Z <= 0 || config.params.W <= 0) {
        LOG_ERROR("Los parámetros X, Z y W deben ser positivos");
        return EXIT_FAILURE;
    }
    
    if (config.num_robots <= 0 || config.num_robots > MAX_ROBOTS) {
        LOG_ERROR("Número de robots inválido (1-%d)", MAX_ROBOTS);
        return EXIT_FAILURE;
    }
    
    /* Inicializar generador aleatorio */
    srand((unsigned int)time(NULL));
    
    /* Configurar señales */
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     SIMULADOR DE ETIQUETADO - MANGOSA S.A.                   ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Parámetros:                                                  ║\n");
    printf("║   Velocidad banda (X): %.2f cm/s                            ║\n", config.params.X);
    printf("║   Tamaño caja (Z): %.2f cm                                  ║\n", config.params.Z);
    printf("║   Longitud banda (W): %.2f cm                               ║\n", config.params.W);
    printf("║   Mangos por caja: %d - %d                                   ║\n", 
           config.params.N_min, config.params.N_max);
    printf("║   Velocidad robot: %.2f cm/s (Z/10)                         ║\n", config.params.robot_speed);
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    if (run_analysis) {
        run_batch_simulation(&config, config.params.N_min, config.params.N_max, 1,
                            "robot_analysis.dat");
    } else if (run_failure_test) {
        run_failure_analysis(&config, 0.0, 0.3, 0.05, "failure_analysis.dat");
    } else {
        /* Simulación simple */
        printf("Ejecutando simulación con %d robots, %d cajas...\n\n", 
               config.num_robots, config.num_boxes);
        
        SimulationResult result = run_single_simulation(&config);
        print_result(&result);
        
        /* Mostrar etiquetas por robot */
        printf("Etiquetas por robot:\n");
        for (int i = 0; i < config.num_robots + config.num_backup; i++) {
            if (result.labels_per_robot[i] > 0) {
                printf("  Robot %d: %d etiquetas%s\n", i, result.labels_per_robot[i],
                       i >= config.num_robots ? " (backup)" : "");
            }
        }
    }
    
    return EXIT_SUCCESS;
}
