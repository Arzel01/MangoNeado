#include "../include/common.h"
#include "../include/ipc_utils.h"

/* ESTRUCTURAS PARA ANÁLISIS */

typedef struct {
    int num_robots;
    double avg_efficiency;
    double min_efficiency;
    double max_efficiency;
    double avg_missed_per_box;
    int optimal;
} RobotAnalysisResult;

typedef struct {
    double failure_prob;
    int robots_no_backup;
    double efficiency_no_backup;
    int robots_with_backup;
    int backup_count;
    double efficiency_with_backup;
} FailureAnalysisResult;

/* VARIABLES GLOBALES */

static volatile sig_atomic_t g_running = 1;
static pthread_mutex_t g_sim_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_box_ready_cond = PTHREAD_COND_INITIALIZER;
static pthread_barrier_t g_start_barrier;

static Box g_boxes[MAX_BOXES_QUEUE];
static int g_box_count = 0;
static int g_current_box_idx = 0;
static bool g_box_available = false;
static bool g_simulation_done = false;

static Robot g_robots[MAX_ROBOTS];
static pthread_t g_robot_threads[MAX_ROBOTS];
static int g_num_robots = 0;
static SystemParams g_params;

/* Resultados de simulación */
static int g_total_mangos = 0;
static int g_labeled_mangos = 0;
static int g_missed_mangos = 0;

/* GENERACIÓN DE CAJAS */

static void generate_boxes(int num_boxes, int n_min, int n_max, double z) {
    double half_size = z / 2.0;
    double margin = z / 10.0;
    double min_distance = z / 15.0;
    
    for (int b = 0; b < num_boxes; b++) {
        g_boxes[b].id = b;
        g_boxes[b].entry_time = 0;
        g_boxes[b].position = 0;
        g_boxes[b].completed = false;
        g_boxes[b].labeled_count = 0;
        g_boxes[b].num_mangos = random_int(n_min, n_max);
        
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
    g_box_count = num_boxes;
}

/* FUNCIONES DE SIMULACIÓN */

static void init_robots(int num_robots, int num_backup, double failure_prob, double w) {
    g_num_robots = num_robots + num_backup;
    double robot_spacing = w / num_robots;
    
    for (int i = 0; i < g_num_robots; i++) {
        g_robots[i].id = i;
        g_robots[i].labels_placed = 0;
        g_robots[i].current_mango = -1;
        g_robots[i].last_action_time = 0;
        g_robots[i].failure_probability = failure_prob;
        g_robots[i].has_failed = false;
        g_robots[i].replacing_robot = -1;
        
        if (i < num_robots) {
            g_robots[i].axis_position = (i + 0.5) * robot_spacing;
            g_robots[i].state = ROBOT_STATE_IDLE;
            g_robots[i].is_backup = false;
        } else {
            g_robots[i].axis_position = 0;
            g_robots[i].state = ROBOT_STATE_DISABLED;
            g_robots[i].is_backup = true;
        }
    }
}

static void* robot_thread(void *arg) {
    Robot *robot = (Robot*)arg;
    double robot_spacing = g_params.W / (g_num_robots - 
                          (g_robots[g_num_robots-1].is_backup ? 1 : 0));
    
    pthread_barrier_wait(&g_start_barrier);
    
    while (!g_simulation_done) {
        if (robot->state == ROBOT_STATE_DISABLED || robot->has_failed) {
            pthread_mutex_lock(&g_sim_mutex);
            while (!g_box_available && !g_simulation_done) {
                pthread_cond_wait(&g_box_ready_cond, &g_sim_mutex);
            }
            pthread_mutex_unlock(&g_sim_mutex);
            if (g_simulation_done) break;
            continue;
        }
        
        pthread_mutex_lock(&g_sim_mutex);
        while (!g_box_available && !g_simulation_done) {
            pthread_cond_wait(&g_box_ready_cond, &g_sim_mutex);
        }
        if (g_simulation_done) {
            pthread_mutex_unlock(&g_sim_mutex);
            break;
        }
        Box *current_box = &g_boxes[g_current_box_idx];
        pthread_mutex_unlock(&g_sim_mutex);
        
        /* Simulación de falla */
        if (g_params.B > 0 && !robot->is_backup && !robot->has_failed) {
            double r = (double)rand() / RAND_MAX;
            if (r < g_params.B) {
                robot->has_failed = true;
                robot->state = ROBOT_STATE_FAILED;
                continue;
            }
        }
        
        robot->state = ROBOT_STATE_ACTIVE;
        
        double avg_distance = g_params.Z / 3.0;
        double time_per_label = (avg_distance / g_params.robot_speed) * 2;
        double effective_time = robot_spacing / g_params.X;
        int max_labels = (int)(effective_time / time_per_label);
        if (max_labels < 1) max_labels = 1;
        
        int labels_this_box = 0;
        
        while (labels_this_box < max_labels) {
            int target = -1;
            double min_dist = INFINITY;
            
            pthread_mutex_lock(&g_sim_mutex);
            for (int i = 0; i < current_box->num_mangos; i++) {
                if (current_box->mangos[i].state == MANGO_UNLABELED) {
                    double dist = distance(0, 0, current_box->mangos[i].x, 
                                          current_box->mangos[i].y);
                    if (dist < min_dist) {
                        min_dist = dist;
                        target = i;
                    }
                }
            }
            
            if (target >= 0) {
                current_box->mangos[target].state = MANGO_BEING_LABELED;
            }
            pthread_mutex_unlock(&g_sim_mutex);
            
            if (target < 0) break;
            
            double reach_time = calc_robot_reach_time(
                current_box->mangos[target].x,
                current_box->mangos[target].y,
                g_params.robot_speed
            );
            usleep((useconds_t)(reach_time * 1000));
            
            pthread_mutex_lock(&g_sim_mutex);
            current_box->mangos[target].state = MANGO_LABELED;
            current_box->mangos[target].labeled_by_robot = robot->id;
            current_box->labeled_count++;
            pthread_mutex_unlock(&g_sim_mutex);
            
            robot->labels_placed++;
            labels_this_box++;
        }
        
        robot->state = ROBOT_STATE_IDLE;
    }
    
    return NULL;
}

static void* conveyor_thread(void *arg) {
    (void)arg;
    pthread_barrier_wait(&g_start_barrier);
    
    double box_interval = g_params.box_spacing / g_params.X;
    double transit_time = g_params.W / g_params.X;
    
    for (int i = 0; i < g_box_count; i++) {
        pthread_mutex_lock(&g_sim_mutex);
        g_current_box_idx = i;
        g_box_available = true;
        g_total_mangos += g_boxes[i].num_mangos;
        pthread_cond_broadcast(&g_box_ready_cond);
        pthread_mutex_unlock(&g_sim_mutex);
        
        usleep((useconds_t)(transit_time * 10000));
        
        pthread_mutex_lock(&g_sim_mutex);
        g_box_available = false;
        g_labeled_mangos += g_boxes[i].labeled_count;
        g_missed_mangos += (g_boxes[i].num_mangos - g_boxes[i].labeled_count);
        pthread_mutex_unlock(&g_sim_mutex);
        
        usleep((useconds_t)(box_interval * 5000));
    }
    
    pthread_mutex_lock(&g_sim_mutex);
    g_simulation_done = true;
    pthread_cond_broadcast(&g_box_ready_cond);
    pthread_mutex_unlock(&g_sim_mutex);
    
    return NULL;
}

static double run_single_simulation(int num_robots, int num_backup, 
                                     int num_boxes, double failure_prob) {
    g_current_box_idx = 0;
    g_box_available = false;
    g_simulation_done = false;
    g_total_mangos = 0;
    g_labeled_mangos = 0;
    g_missed_mangos = 0;
    
    init_robots(num_robots, num_backup, failure_prob, g_params.W);
    generate_boxes(num_boxes, g_params.N_min, g_params.N_max, g_params.Z);
    
    pthread_barrier_init(&g_start_barrier, NULL, g_num_robots + 2);
    
    for (int i = 0; i < g_num_robots; i++) {
        pthread_create(&g_robot_threads[i], NULL, robot_thread, &g_robots[i]);
    }
    
    pthread_t conveyor;
    pthread_create(&conveyor, NULL, conveyor_thread, NULL);
    
    pthread_barrier_wait(&g_start_barrier);
    
    pthread_join(conveyor, NULL);
    for (int i = 0; i < g_num_robots; i++) {
        pthread_join(g_robot_threads[i], NULL);
    }
    
    pthread_barrier_destroy(&g_start_barrier);
    
    double efficiency = (g_total_mangos > 0) ? 
                        (100.0 * g_labeled_mangos / g_total_mangos) : 0;
    return efficiency;
}

/* ANÁLISIS DE ROBOTS */

static void analyze_robots(const SystemParams *params, int num_boxes, 
                           const char *output_file) {
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        LOG_ERROR("No se pudo crear archivo: %s", output_file);
        return;
    }
    
    fprintf(fp, "# Análisis de Número Óptimo de Robots\n");
    fprintf(fp, "# X=%.2f cm/s, Z=%.2f cm, W=%.2f cm, N=%d-%d\n",
            params->X, params->Z, params->W, params->N_min, params->N_max);
    fprintf(fp, "# Columnas: num_robots avg_eff min_eff max_eff avg_missed\n");
    
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  ANÁLISIS: NÚMERO ÓPTIMO DE ROBOTS                          ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Robots │ Eficiencia │ Mínima │ Máxima │ Perdidos/caja     ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    int optimal_robots = -1;
    const int NUM_RUNS = 5;
    
    for (int r = 1; r <= 15; r++) {
        double robot_spacing = params->W / r;
        if (robot_spacing < params->Z) break;
        
        double total_eff = 0, min_eff = 100, max_eff = 0;
        double total_missed = 0;
        
        for (int run = 0; run < NUM_RUNS; run++) {
            double eff = run_single_simulation(r, 0, num_boxes, 0.0);
            total_eff += eff;
            if (eff < min_eff) min_eff = eff;
            if (eff > max_eff) max_eff = eff;
            total_missed += g_missed_mangos;
        }
        
        double avg_eff = total_eff / NUM_RUNS;
        double avg_missed = total_missed / (NUM_RUNS * num_boxes);
        
        fprintf(fp, "%d %.2f %.2f %.2f %.2f\n", 
                r, avg_eff, min_eff, max_eff, avg_missed);
        
        char status = (avg_eff >= 99.9) ? '*' : ' ';
        printf("║ %c %2d   │   %5.1f%%   │ %5.1f%% │ %5.1f%% │     %.1f           ║\n",
               status, r, avg_eff, min_eff, max_eff, avg_missed);
        
        if (avg_eff >= 99.9 && optimal_robots == -1) {
            optimal_robots = r;
        }
        
        if (avg_eff >= 99.9) break;
    }
    
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    if (optimal_robots > 0) {
        printf("║ ★ NÚMERO ÓPTIMO: %d robots (eficiencia ~100%%)               ║\n", 
               optimal_robots);
    }
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    fclose(fp);
    printf("Datos guardados en: %s\n\n", output_file);
}

/* ANÁLISIS DE REDUNDANCIA */

static void analyze_failure(const SystemParams *params, int num_boxes,
                            const char *output_file) {
    (void)params; /* Parámetros disponibles para uso futuro */
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        LOG_ERROR("No se pudo crear archivo: %s", output_file);
        return;
    }
    
    fprintf(fp, "# Análisis de Redundancia y Tolerancia a Fallas\n");
    fprintf(fp, "# Columnas: prob_falla robots_sin_backup eff_sin_backup "
                "robots_con_backup num_backup eff_con_backup\n");
    
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  ANÁLISIS: REDUNDANCIA Y FALLAS                              ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ P(falla) │ Sin backup │ Eff  │ Con backup │ Backups │ Eff  ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    
    double failure_probs[] = {0.0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3};
    int num_probs = sizeof(failure_probs) / sizeof(failure_probs[0]);
    
    for (int p = 0; p < num_probs; p++) {
        double prob = failure_probs[p];
        
        /* Encontrar óptimo sin backup */
        int opt_no_backup = 1;
        double eff_no_backup = 0;
        
        for (int r = 1; r <= 12; r++) {
            double eff = run_single_simulation(r, 0, num_boxes, prob);
            if (eff > eff_no_backup) {
                eff_no_backup = eff;
                opt_no_backup = r;
            }
            if (eff >= 99.5) break;
        }
        
        /* Encontrar óptimo con backup */
        int backup_count = (int)(opt_no_backup * prob) + 1;
        if (backup_count < 1) backup_count = 1;
        
        int opt_with_backup = 1;
        double eff_with_backup = 0;
        
        for (int r = 1; r <= 12; r++) {
            double eff = run_single_simulation(r, backup_count, num_boxes, prob);
            if (eff > eff_with_backup) {
                eff_with_backup = eff;
                opt_with_backup = r;
            }
            if (eff >= 99.5) break;
        }
        
        fprintf(fp, "%.2f %d %.2f %d %d %.2f\n",
                prob, opt_no_backup, eff_no_backup, 
                opt_with_backup, backup_count, eff_with_backup);
        
        printf("║  %.2f    │     %2d     │%5.1f%%│     %2d     │   %2d    │%5.1f%%║\n",
               prob, opt_no_backup, eff_no_backup, 
               opt_with_backup, backup_count, eff_with_backup);
    }
    
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    fclose(fp);
    printf("Datos guardados en: %s\n\n", output_file);
}

/* FUNCIÓN PRINCIPAL */

static void print_usage(const char *prog_name) {
    printf("\nUso: %s [opciones]\n\n", prog_name);
    printf("Programa de Análisis - Mangosa S.A.\n\n");
    printf("Opciones:\n");
    printf("  -x <valor>    Velocidad de la banda (cm/s) [default: 10]\n");
    printf("  -z <valor>    Tamaño de la caja (cm) [default: 50]\n");
    printf("  -w <valor>    Longitud de la banda (cm) [default: 300]\n");
    printf("  -n <valor>    Número mínimo de mangos [default: 10]\n");
    printf("  -c <valor>    Número de cajas por simulación [default: 30]\n");
    printf("  -r            Análisis de robots solamente\n");
    printf("  -f            Análisis de fallas solamente\n");
    printf("  -h            Mostrar ayuda\n\n");
}

int main(int argc, char *argv[]) {
    SystemParams params;
    int num_boxes = 30;
    bool robots_only = false;
    bool failure_only = false;
    int opt;
    
    /* Valores por defecto */
    params.X = 10.0;
    params.Z = 50.0;
    params.W = 300.0;
    params.N_min = 10;
    params.N_max = 12;
    params.robot_speed = params.Z / 10.0;
    params.box_spacing = params.Z * 1.5;
    
    while ((opt = getopt(argc, argv, "x:z:w:n:c:rfh")) != -1) {
        switch (opt) {
            case 'x':
                params.X = atof(optarg);
                break;
            case 'z':
                params.Z = atof(optarg);
                params.robot_speed = params.Z / 10.0;
                params.box_spacing = params.Z * 1.5;
                break;
            case 'w':
                params.W = atof(optarg);
                break;
            case 'n':
                params.N_min = atoi(optarg);
                params.N_max = (int)(params.N_min * 1.2);
                break;
            case 'c':
                num_boxes = atoi(optarg);
                break;
            case 'r':
                robots_only = true;
                break;
            case 'f':
                failure_only = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    srand((unsigned int)time(NULL));
    memcpy(&g_params, &params, sizeof(SystemParams));
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  PROGRAMA DE ANÁLISIS - MANGOSA S.A.                         ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Parámetros:                                                  ║\n");
    printf("║   X = %.2f cm/s                                             ║\n", params.X);
    printf("║   Z = %.2f cm                                               ║\n", params.Z);
    printf("║   W = %.2f cm                                               ║\n", params.W);
    printf("║   N = %d a %d mangos/caja                                    ║\n", 
           params.N_min, params.N_max);
    printf("║   Cajas por simulación: %d                                   ║\n", num_boxes);
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    if (!failure_only) {
        analyze_robots(&params, num_boxes, "robot_analysis.csv");
    }
    
    if (!robots_only) {
        analyze_failure(&params, num_boxes, "failure_analysis.csv");
    }
    
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  ANÁLISIS COMPLETADO                                         ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    
    return EXIT_SUCCESS;
}
