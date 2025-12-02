# Sistema de Etiquetado Automático de Mangos - Mangosa S.A.

## Ejecución Rápida (Un Solo Comando)

Para compilar, ejecutar una simulación visual, análisis completo y generar todas las gráficas:

```bash
make demo
```

**Esto automáticamente:**
1. Compila todo el proyecto
2. Ejecuta una simulación visual con robots etiquetando mangos
3. Ejecuta el análisis de eficiencia (1 a N robots)
4. Genera 4 gráficas PNG en la carpeta `scripts/`

**Gráficas generadas:**
- `scripts/cost_effectiveness_analysis.png` - Análisis principal costo-efectivo
- `scripts/efficiency_curve.png` - Curva de eficiencia vs robots
- `scripts/missed_mangos.png` - Mangos perdidos por configuración  
- `scripts/combined_analysis.png` - Análisis combinado (4 gráficas)

### Otros comandos útiles:
```bash
make              # Solo compilar
make analysis     # Solo análisis + gráficas (sin simulación visual)
make run          # Solo ejecutar simulación
make help         # Ver todos los comandos disponibles
```

### Personalizar parámetros:
```bash
make demo X=15 N=25 BOXES=30
```

| Parámetro | Descripción | Default |
|-----------|-------------|---------|
| `X` | Velocidad banda (cm/s) | 10 |
| `Z` | Tamaño caja (cm) | 50 |
| `W` | Longitud banda (cm) | 300 |
| `N` | Mangos mínimos/caja | 10 |
| `ROBOTS` | Número de robots | 4 |
| `BOXES` | Cajas a simular | 20 |

### Ejemplos de uso con parámetros personalizados:

```bash
# Escenario 1: Banda más rápida con más mangos por caja
make demo X=15 N=25

# Escenario 2: Cajas más grandes con más mangos
make demo Z=80 N=30

# Escenario 3: Banda más larga, simulación extendida
make demo W=500 BOXES=50

# Escenario 4: Configuración completa personalizada
make demo X=12 Z=60 W=400 N=20 ROBOTS=6 BOXES=40
```

---

## Descripción

Sistema de simulación para automatización de etiquetado de mangos en línea de producción industrial.

**Conceptos de Sistemas Operativos implementados:**
- **Procesos**: Sistema de visión y controlador de robots como procesos independientes
- **Hilos (pthreads)**: Múltiples robots trabajando concurrentemente
- **IPC**: Colas de mensajes y memoria compartida para comunicación entre procesos
- **Sincronización**: Mutex y variables de condición para evitar race conditions
- **Manejo de señales**: Control de terminación segura del sistema

**Modelo físico:**
- Caja viaja por banda transportadora pasando por robots distribuidos homogéneamente
- Cada robot etiqueta mangos durante ventana de tiempo limitada
- Más robots = mayor eficiencia (hasta punto óptimo)
- Sistema de respaldo ante fallas de robots

## Compilación

```bash
make
```

Genera ejecutables en `bin/`:
- `simulator` - Simulador integrado (principal)
- `vision_system` - Proceso de visión artificial
- `robot_controller` - Controlador de robots
- `analysis` - Análisis y optimización

## Parámetros

| Opción | Descripción | Default |
|--------|-------------|---------|
| `-x` | Velocidad banda (cm/s) | 10 |
| `-z` | Tamaño caja (cm) | 50 |
| `-w` | Longitud banda (cm) | 300 |
| `-n` | Mangos mínimos/caja | 10 |
| `-r` | Número de robots | 4 |
| `-b` | Robots de respaldo | 1 |
| `-B` | Probabilidad falla (0-1) | 0 |
| `-c` | Cajas a simular | 20 |
| `-v` | Modo verbose | - |

## Casos de Prueba

### 1. Compilar el proyecto
```bash
make
```

### 2. Eficiencia Baja (~35%)
**Escenario**: 1 robot procesando cajas
```bash
./bin/simulator -x 10 -z 50 -w 300 -n 10 -r 1 -c 10 -v
```
**Resultado esperado**: ~35% eficiencia (3 mangos/caja de ~11 total)

### 3. Eficiencia Media (~51%)
**Escenario**: 6 robots distribuidos
```bash
./bin/simulator -x 10 -z 50 -w 300 -n 10 -r 6 -c 10 -v
```
**Resultado esperado**: ~50% eficiencia (6 mangos/caja de ~11 total)

### 4. Eficiencia Alta (~89%)
**Escenario**: 10 robots para mayor cobertura
```bash
./bin/simulator -x 10 -z 50 -w 300 -n 10 -r 10 -c 10 -v
```
**Resultado esperado**: ~89% eficiencia (10 mangos/caja de ~11 total)

### 5. Eficiencia Óptima (100%)
**Escenario**: 15 robots (saturación)
```bash
./bin/simulator -x 10 -z 50 -w 300 -n 10 -r 15 -c 10 -v
```
**Resultado esperado**: 100% eficiencia (todos los mangos etiquetados)

### 6. Prueba con Fallas y Respaldo
**Escenario**: 10 robots con 15% probabilidad de falla y 5 backups
```bash
./bin/simulator -x 10 -z 50 -w 300 -n 10 -r 10 -b 5 -B 0.15 -c 10 -v
```
**Resultado esperado**: 
- Fallas: 1-2 robots
- Backups activados: 1-2
- Eficiencia: ~85-89% (ligeramente menor por robots fallados)

### 7. Análisis de Eficiencia Completo
**Prueba automatizada** que genera curva de eficiencia vs robots:
```bash
./bin/simulator -x 10 -z 50 -w 300 -n 10 -c 20 -a
```

### 8. Generar Análisis con Gráficas
**Ejecuta análisis completo y genera gráficas** en un solo comando:
```bash
make analysis
```
También puedes personalizar los parámetros:
```bash
make analysis X=50 Z=50 W=300 N=20
```
**Imágenes generadas en `scripts/`:**
- `cost_effectiveness_analysis.png` - Análisis principal costo-efectivo
- `efficiency_curve.png` - Curva de eficiencia vs robots
- `missed_mangos.png` - Mangos perdidos por configuración
- `combined_analysis.png` - Análisis combinado

## Limpieza

```bash
make clean
```
