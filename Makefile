# ============================================================================
# Makefile para el Sistema de Etiquetado Automático - Mangosa S.A.
# ============================================================================
# 
# Uso:
#   make          - Compila todos los programas
#   make demo     - EJECUTA TODO (compilar + simulación + análisis + gráficas)
#   make clean    - Elimina archivos compilados
#   make run      - Ejecuta el simulador con parámetros por defecto
#   make analysis - Ejecuta el análisis completo
#   make test     - Ejecuta pruebas básicas
#   make help     - Muestra ayuda
#
# ============================================================================

# Compilador y flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c11
CFLAGS += -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
CFLAGS += -O2
LDFLAGS = -pthread -lm -lrt

# Flags de depuración (descomentar para debug)
# CFLAGS += -g -O0 -DLOG_LEVEL=LOG_LEVEL_DEBUG
# CFLAGS += -fsanitize=address -fsanitize=undefined

# Directorios
SRC_DIR = src
INC_DIR = include
BIN_DIR = bin
SCRIPTS_DIR = scripts

# Archivos fuente
VISION_SRC = $(SRC_DIR)/vision_system.c
ROBOT_SRC = $(SRC_DIR)/robot_controller.c
SIMULATOR_SRC = $(SRC_DIR)/simulator.c

# Ejecutables
VISION_BIN = $(BIN_DIR)/vision_system
ROBOT_BIN = $(BIN_DIR)/robot_controller
SIMULATOR_BIN = $(BIN_DIR)/simulator
ANALYSIS_BIN = $(BIN_DIR)/analysis

# Headers
HEADERS = $(INC_DIR)/common.h $(INC_DIR)/ipc_utils.h

# Parámetros por defecto para ejecución
X ?= 10
Z ?= 50
W ?= 300
N ?= 10
ROBOTS ?= 4
BACKUP ?= 1
BOXES ?= 20

# ============================================================================
# Reglas principales
# ============================================================================

.PHONY: all clean run analysis test help dirs ipc-clean

all: dirs $(VISION_BIN) $(ROBOT_BIN) $(SIMULATOR_BIN) $(ANALYSIS_BIN)
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║  Compilación exitosa - Sistema de Etiquetado Mangosa S.A.    ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Ejecutables generados:                                      ║"
	@echo "║    - $(VISION_BIN)                                           ║"
	@echo "║    - $(ROBOT_BIN)                                            ║"
	@echo "║    - $(SIMULATOR_BIN)                                        ║"
	@echo "║    - $(ANALYSIS_BIN)                                         ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""

dirs:
	@mkdir -p $(BIN_DIR)

# ============================================================================
# Compilación de ejecutables
# ============================================================================

$(VISION_BIN): $(VISION_SRC) $(HEADERS)
	@echo "Compilando sistema de visión..."
	$(CC) $(CFLAGS) -I$(INC_DIR) -o $@ $< $(LDFLAGS)

$(ROBOT_BIN): $(ROBOT_SRC) $(HEADERS)
	@echo "Compilando controlador de robots..."
	$(CC) $(CFLAGS) -I$(INC_DIR) -o $@ $< $(LDFLAGS)

$(SIMULATOR_BIN): $(SIMULATOR_SRC) $(HEADERS)
	@echo "Compilando simulador..."
	$(CC) $(CFLAGS) -I$(INC_DIR) -o $@ $< $(LDFLAGS)

$(BIN_DIR)/analysis: $(SRC_DIR)/analysis.c $(HEADERS)
	@echo "Compilando programa de análisis..."
	$(CC) $(CFLAGS) -I$(INC_DIR) -o $@ $< $(LDFLAGS)

# ============================================================================
# Ejecución
# ============================================================================

run: $(SIMULATOR_BIN)
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║  Ejecutando simulación con parámetros:                       ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Velocidad banda (X): $(X) cm/s                              ║"
	@echo "║  Tamaño caja (Z):     $(Z) cm                                ║"
	@echo "║  Longitud banda (W):  $(W) cm                                ║"
	@echo "║  Mangos mínimos (N):  $(N)                                   ║"
	@echo "║  Robots activos:      $(ROBOTS)                              ║"
	@echo "║  Robots respaldo:     $(BACKUP)                              ║"
	@echo "║  Cajas a simular:     $(BOXES)                               ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""
	@$(SIMULATOR_BIN) -x $(X) -z $(Z) -w $(W) -n $(N) -r $(ROBOTS) -b $(BACKUP) -c $(BOXES) -v

run-analysis: $(SIMULATOR_BIN)
	@echo "Ejecutando análisis de número óptimo de robots..."
	@$(SIMULATOR_BIN) -x $(X) -z $(Z) -w $(W) -n $(N) -a

run-failure: $(SIMULATOR_BIN)
	@echo "Ejecutando análisis de fallas..."
	@$(SIMULATOR_BIN) -x $(X) -z $(Z) -w $(W) -n $(N) -f

# Ejecutar sistema completo (visión + robots en paralelo)
run-full: $(VISION_BIN) $(ROBOT_BIN)
	@echo "Iniciando sistema completo..."
	@echo "Presione Ctrl+C para terminar"
	@echo ""
	@# Limpiar recursos IPC previos
	@-ipcrm -Q 0x4D414E48 2>/dev/null || true
	@-ipcrm -M 0x4D414E49 2>/dev/null || true
	@-ipcrm -S 0x4D414E4A 2>/dev/null || true
	@# Iniciar sistema de visión en background
	@$(VISION_BIN) -x $(X) -z $(Z) -w $(W) -n $(N) -c &
	@sleep 1
	@# Iniciar controlador de robots
	@$(ROBOT_BIN) -r $(ROBOTS) -b $(BACKUP)

# ============================================================================
# Análisis
# ============================================================================

analysis: $(SIMULATOR_BIN)
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║  Ejecutando análisis completo + generación de gráficas       ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""
	@$(SIMULATOR_BIN) -x $(X) -z $(Z) -w $(W) -n $(N) -a
	@echo ""
	@echo "Generando gráficas..."
	@python3 $(SCRIPTS_DIR)/plot_analysis.py
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║  Gráficas generadas en: scripts/                             ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"

# ============================================================================
# Demo completa (compilar + simulación visual + análisis + gráficas)
# ============================================================================

demo: all
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║           DEMO COMPLETA - Sistema Mangosa S.A.               ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo "  PARTE 1: Simulación en tiempo real ($(BOXES) cajas)"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo ""
	@echo "Parámetros de simulación:"
	@echo "  • Velocidad banda:    $(X) cm/s"
	@echo "  • Tamaño caja:        $(Z) cm"
	@echo "  • Longitud banda:     $(W) cm"
	@echo "  • Mangos mínimos:     $(N) por caja"
	@echo "  • Robots activos:     $(ROBOTS)"
	@echo "  • Robots de respaldo: $(BACKUP)"
	@echo ""
	@$(SIMULATOR_BIN) -x $(X) -z $(Z) -w $(W) -n $(N) -r $(ROBOTS) -b $(BACKUP) -c $(BOXES) -v
	@echo ""
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo "  PARTE 2: Análisis de número óptimo de robots"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo ""
	@$(SIMULATOR_BIN) -x $(X) -z $(Z) -w $(W) -n $(N) -a
	@echo ""
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo "  PARTE 3: Generación de gráficas"
	@echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
	@echo ""
	@python3 $(SCRIPTS_DIR)/plot_analysis.py
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║                    DEMO COMPLETADA                           ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Resultados:                                                 ║"
	@echo "║    • Simulación ejecutada exitosamente                       ║"
	@echo "║    • Análisis de robots completado                           ║"
	@echo "║    • Gráficas generadas en: scripts/                         ║"
	@echo "║                                                              ║"
	@echo "║  Archivos PNG generados:                                     ║"
	@echo "║    - scripts/cost_effectiveness_analysis.png                 ║"
	@echo "║    - scripts/efficiency_curve.png                            ║"
	@echo "║    - scripts/missed_mangos.png                               ║"
	@echo "║    - scripts/combined_analysis.png                           ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"

# ============================================================================
# Pruebas
# ============================================================================

test: $(SIMULATOR_BIN)
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║  Ejecutando pruebas del sistema                              ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "PRUEBA 1: Simulación básica con 4 robots y 10 cajas"
	@echo "Parámetros: X=10, Z=50, W=300, N=10, Robots=4"
	@echo "═══════════════════════════════════════════════════════════════"
	@$(SIMULATOR_BIN) -x 10 -z 50 -w 300 -n 10 -r 4 -c 10 -v
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "PRUEBA 2: Simulación con redundancia y fallas"
	@echo "Parámetros: X=10, Z=50, W=300, N=10, Robots=4, Backup=2, B=0.15"
	@echo "═══════════════════════════════════════════════════════════════"
	@$(SIMULATOR_BIN) -x 10 -z 50 -w 300 -n 10 -r 4 -b 2 -B 0.15 -c 10 -v
	@echo ""
	@echo "═══════════════════════════════════════════════════════════════"
	@echo "PRUEBA 3: Simulación con muchos mangos (alta carga)"
	@echo "Parámetros: X=10, Z=50, W=300, N=20, Robots=8"
	@echo "═══════════════════════════════════════════════════════════════"
	@$(SIMULATOR_BIN) -x 10 -z 50 -w 300 -n 20 -r 8 -c 10 -v
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║  Todas las pruebas completadas exitosamente                  ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"

# ============================================================================
# Limpieza
# ============================================================================

clean:
	@echo "Limpiando archivos compilados..."
	@rm -rf $(BIN_DIR)
	@rm -f *.dat *.csv
	@echo "Limpieza completada."

clean-all: clean
	@echo "Limpiando resultados..."


# Limpiar recursos IPC del sistema
ipc-clean:
	@echo "Limpiando recursos IPC..."
	@-ipcrm -Q 0x4D414E48 2>/dev/null || true
	@-ipcrm -M 0x4D414E49 2>/dev/null || true
	@-ipcrm -S 0x4D414E4A 2>/dev/null || true
	@echo "Recursos IPC limpiados."

# ============================================================================
# Ayuda
# ============================================================================

help:
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║  Sistema de Etiquetado Automático - Mangosa S.A.             ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Comandos disponibles:                                       ║"
	@echo "║                                                              ║"
	@echo "║  make          - Compila todos los programas                 ║"
	@echo "║  make demo     - ⭐ DEMO COMPLETA (compila+simula+gráficas)  ║"
	@echo "║  make clean    - Elimina archivos compilados                 ║"
	@echo "║  make run      - Ejecuta simulación con parámetros default   ║"
	@echo "║  make test     - Ejecuta pruebas básicas                     ║"
	@echo "║  make analysis - Ejecuta análisis completo con gráficas      ║"
	@echo "║  make ipc-clean- Limpia recursos IPC del sistema             ║"
	@echo "║  make help     - Muestra esta ayuda                          ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Parámetros configurables:                                   ║"
	@echo "║                                                              ║"
	@echo "║  X=<valor>     - Velocidad de banda (cm/s) [default: 10]     ║"
	@echo "║  Z=<valor>     - Tamaño de caja (cm) [default: 50]           ║"
	@echo "║  W=<valor>     - Longitud de banda (cm) [default: 300]       ║"
	@echo "║  N=<valor>     - Mangos mínimos por caja [default: 10]       ║"
	@echo "║  ROBOTS=<num>  - Número de robots [default: 4]               ║"
	@echo "║  BACKUP=<num>  - Robots de respaldo [default: 1]             ║"
	@echo "║  BOXES=<num>   - Cajas a simular [default: 20]               ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Ejemplos:                                                   ║"
	@echo "║                                                              ║"
	@echo "║  make demo                    <- Ejecutar TODO automático    ║"
	@echo "║  make run X=15 Z=40 N=12 ROBOTS=6                            ║"
	@echo "║  make analysis X=10 Z=50 N=15                                ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""
