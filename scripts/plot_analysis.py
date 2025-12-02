"""
Análisis de Mangosa S.A.
Gráficas de eficiencia vs número de robots para determinar
el punto de operación costo-efectivo.
"""

import os
import sys
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch
import matplotlib.patches as mpatches

# Configuración de estilo
plt.style.use('seaborn-v0_8-whitegrid')
plt.rcParams['font.family'] = 'DejaVu Sans'
plt.rcParams['font.size'] = 11
plt.rcParams['axes.labelsize'] = 12
plt.rcParams['axes.titlesize'] = 14
plt.rcParams['legend.fontsize'] = 10

def load_robot_analysis(filename):
    """Carga datos del archivo de análisis de robots."""
    robots = []
    avg_eff = []
    min_eff = []
    max_eff = []
    avg_missed = []
    params = {}
    
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if line.startswith('#'):
                # Extraer parámetros del header
                if 'X=' in line and 'Z=' in line:
                    import re
                    # Buscar X=valor, Z=valor, W=valor, N=min-max, Cajas=valor
                    x_match = re.search(r'X=(\d+\.?\d*)', line)
                    z_match = re.search(r'Z=(\d+\.?\d*)', line)
                    w_match = re.search(r'W=(\d+\.?\d*)', line)
                    n_match = re.search(r'N=(\d+)-(\d+)', line)
                    cajas_match = re.search(r'Cajas=(\d+)', line)
                    
                    if x_match:
                        params['X'] = float(x_match.group(1))
                    if z_match:
                        params['Z'] = float(z_match.group(1))
                    if w_match:
                        params['W'] = float(w_match.group(1))
                    if n_match:
                        params['N_min'] = int(n_match.group(1))
                        params['N_max'] = int(n_match.group(2))
                    if cajas_match:
                        params['cajas'] = int(cajas_match.group(1))
                continue
            if not line:
                continue
            parts = line.split()
            if len(parts) >= 5:
                robots.append(int(parts[0]))
                avg_eff.append(float(parts[1]))
                min_eff.append(float(parts[2]))
                max_eff.append(float(parts[3]))
                avg_missed.append(float(parts[4]))
    
    return {
        'robots': np.array(robots),
        'avg_eff': np.array(avg_eff),
        'min_eff': np.array(min_eff),
        'max_eff': np.array(max_eff),
        'avg_missed': np.array(avg_missed),
        'params': params
    }

def load_failure_analysis(filename):
    """Carga datos del archivo de análisis de fallas."""
    data = {
        'prob_falla': [],
        'robots_sin_backup': [],
        'eff_sin_backup': [],
        'robots_con_backup': [],
        'num_backup': [],
        'eff_con_backup': []
    }
    
    try:
        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                if line.startswith('#') or not line:
                    continue
                parts = line.split()
                if len(parts) >= 6:
                    data['prob_falla'].append(float(parts[0]))
                    data['robots_sin_backup'].append(int(parts[1]))
                    data['eff_sin_backup'].append(float(parts[2]))
                    data['robots_con_backup'].append(int(parts[3]))
                    data['num_backup'].append(int(parts[4]))
                    data['eff_con_backup'].append(float(parts[5]))
    except FileNotFoundError:
        return None
    
    for key in data:
        data[key] = np.array(data[key])
    
    return data

def find_optimal_point(robots, efficiency, threshold=95.0):
    """
    Encuentra el punto óptimo costo-efectivo.
    Busca el menor número de robots que alcanza el umbral de eficiencia.
    """
    for i, (r, eff) in enumerate(zip(robots, efficiency)):
        if eff >= threshold:
            return i, r, eff
    return len(robots)-1, robots[-1], efficiency[-1]

def calculate_cost_effectiveness(robots, efficiency):
    """
    Calcula el índice de costo-efectividad.
    CE = Eficiencia / Número de robots (normalizado)
    """
    # Evitar división por cero y normalizar
    ce = efficiency / robots
    return ce / ce.max() * 100  # Normalizar a porcentaje

def plot_cost_effectiveness_analysis(data, output_dir):
    """Genera la gráfica principal de análisis costo-efectivo."""
    
    fig, ax1 = plt.subplots(figsize=(12, 8))
    
    robots = data['robots']
    avg_eff = data['avg_eff']
    min_eff = data['min_eff']
    max_eff = data['max_eff']
    
    # Calcular costo-efectividad
    cost_eff = calculate_cost_effectiveness(robots, avg_eff)
    
    # Encontrar punto óptimo (95% eficiencia)
    idx_95, robot_95, eff_95 = find_optimal_point(robots, avg_eff, 95.0)
    # Encontrar punto de 100% eficiencia
    idx_100, robot_100, eff_100 = find_optimal_point(robots, avg_eff, 99.9)
    # Encontrar máximo costo-efectividad
    idx_ce_max = np.argmax(cost_eff)
    
    # Color palette
    color_eff = '#2E86AB'       # Azul para eficiencia
    color_ce = '#A23B72'        # Magenta para costo-efectividad
    color_fill = '#E8F4F8'      # Relleno suave
    color_optimal = '#28A745'   # Verde para punto óptimo
    color_100 = '#FFC107'       # Amarillo para 100%
    
    # Gráfica de eficiencia con área sombreada (min-max)
    ax1.fill_between(robots, min_eff, max_eff, alpha=0.3, color=color_eff, 
                     label='Rango (min-max)')
    line1, = ax1.plot(robots, avg_eff, 'o-', color=color_eff, linewidth=2.5, 
                      markersize=10, label='Eficiencia promedio (%)', zorder=5)
    
    ax1.set_xlabel('Número de Robots', fontsize=13, fontweight='bold')
    ax1.set_ylabel('Eficiencia (%)', color=color_eff, fontsize=13, fontweight='bold')
    ax1.tick_params(axis='y', labelcolor=color_eff)
    ax1.set_ylim(0, 105)
    ax1.set_xlim(robots.min() - 0.5, robots.max() + 0.5)
    
    # Líneas de referencia
    ax1.axhline(y=95, color='gray', linestyle='--', alpha=0.5, linewidth=1)
    ax1.axhline(y=100, color='gray', linestyle='--', alpha=0.5, linewidth=1)
    ax1.text(robots.max() + 0.3, 95, '95%', va='center', ha='left', 
             color='gray', fontsize=9)
    ax1.text(robots.max() + 0.3, 100, '100%', va='center', ha='left', 
             color='gray', fontsize=9)
    
    # Segundo eje Y para costo-efectividad
    ax2 = ax1.twinx()
    line2, = ax2.plot(robots, cost_eff, 's--', color=color_ce, linewidth=2, 
                      markersize=8, label='Índice Costo-Efectividad', alpha=0.8, zorder=4)
    ax2.set_ylabel('Índice Costo-Efectividad (%)', color=color_ce, 
                   fontsize=13, fontweight='bold')
    ax2.tick_params(axis='y', labelcolor=color_ce)
    ax2.set_ylim(0, 110)
    
    # Marcar puntos importantes
    # Punto óptimo costo-efectivo (máximo CE)
    ax1.scatter([robots[idx_ce_max]], [avg_eff[idx_ce_max]], s=200, c=color_optimal, 
                marker='*', zorder=10, edgecolors='white', linewidths=2)
    ax1.annotate(f'ÓPTIMO COSTO-EFECTIVO\n{robots[idx_ce_max]} robots, {avg_eff[idx_ce_max]:.1f}%',
                xy=(robots[idx_ce_max], avg_eff[idx_ce_max]),
                xytext=(robots[idx_ce_max] + 1, avg_eff[idx_ce_max] - 15),
                fontsize=10, fontweight='bold', color=color_optimal,
                arrowprops=dict(arrowstyle='->', color=color_optimal, lw=1.5),
                bbox=dict(boxstyle='round,pad=0.3', facecolor='white', 
                         edgecolor=color_optimal, alpha=0.9))
    
    # Punto de 100% eficiencia
    if eff_100 >= 99.9:
        ax1.scatter([robot_100], [eff_100], s=150, c=color_100, marker='D', 
                    zorder=10, edgecolors='black', linewidths=1)
        ax1.annotate(f'100% Eficiencia\n{robot_100} robots',
                    xy=(robot_100, eff_100),
                    xytext=(robot_100 - 2, eff_100 - 10),
                    fontsize=9, color='#856404',
                    arrowprops=dict(arrowstyle='->', color='#856404', lw=1),
                    bbox=dict(boxstyle='round,pad=0.2', facecolor='#FFF3CD', 
                             edgecolor='#856404', alpha=0.9))
    
    # Zona de operación recomendada
    if idx_ce_max < len(robots) - 1:
        ax1.axvspan(robots[idx_ce_max] - 0.3, robots[min(idx_ce_max + 2, len(robots)-1)] + 0.3, 
                   alpha=0.15, color=color_optimal, label='Zona recomendada')
    
    # Título
    plt.title('Análisis Costo-Efectividad: Número Óptimo de Robots\nMangosa S.A. - Sistema de Etiquetado',
              fontsize=15, fontweight='bold', pad=20)
    
    # Leyenda combinada
    lines = [line1, line2]
    labels = [l.get_label() for l in lines]
    
    # Añadir elementos adicionales a la leyenda
    star_patch = plt.Line2D([0], [0], marker='*', color='w', markerfacecolor=color_optimal,
                            markersize=15, label='Punto óptimo costo-efectivo')
    diamond_patch = plt.Line2D([0], [0], marker='D', color='w', markerfacecolor=color_100,
                               markersize=10, label='100% eficiencia')
    range_patch = mpatches.Patch(color=color_eff, alpha=0.3, label='Rango eficiencia')
    
    ax1.legend(handles=[line1, range_patch, line2, star_patch, diamond_patch],
               loc='lower right', framealpha=0.95, fontsize=10)
    
    # Cuadro de información con parámetros del sistema
    params = data.get('params', {})
    params_text = "Parámetros del análisis:\n"
    if params:
        if 'N_min' in params and 'N_max' in params:
            params_text += f"• Mangos/caja: {params['N_min']}-{params['N_max']}\n"
        if 'cajas' in params:
            params_text += f"• Cajas simuladas: {params['cajas']}\n"
        if 'X' in params:
            params_text += f"• Vel. banda: {params['X']:.0f} cm/s\n"
        if 'Z' in params:
            params_text += f"• Tamaño caja: {params['Z']:.0f} cm\n"
        if 'W' in params:
            params_text += f"• Long. banda: {params['W']:.0f} cm"
    
    info_text = (f"Resultados:\n"
                f"• Punto óptimo: {robots[idx_ce_max]} robots\n"
                f"• Eficiencia en óptimo: {avg_eff[idx_ce_max]:.1f}%\n"
                f"• Para 100%: {robot_100} robots")
    
    props = dict(boxstyle='round,pad=0.5', facecolor='wheat', alpha=0.9)
    ax1.text(0.02, 0.98, params_text, transform=ax1.transAxes, fontsize=9,
             verticalalignment='top', bbox=props)
    
    props2 = dict(boxstyle='round,pad=0.5', facecolor='lightgreen', alpha=0.9)
    ax1.text(0.02, 0.65, info_text, transform=ax1.transAxes, fontsize=9,
             verticalalignment='top', bbox=props2)
    
    plt.tight_layout()
    
    # Guardar figura
    output_path = os.path.join(output_dir, 'cost_effectiveness_analysis.png')
    plt.savefig(output_path, dpi=150, bbox_inches='tight', 
                facecolor='white', edgecolor='none')
    print(f"Gráfica guardada en: {output_path}")
    
    return fig

def plot_efficiency_curve(data, output_dir):
    """Genera una gráfica simple de la curva de eficiencia."""
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    robots = data['robots']
    avg_eff = data['avg_eff']
    avg_missed = data['avg_missed']
    params = data.get('params', {})
    
    # Colores
    color1 = '#1f77b4'
    color2 = '#d62728'
    
    # Gráfica principal - Eficiencia
    ax.plot(robots, avg_eff, 'o-', color=color1, linewidth=2.5, 
            markersize=10, label='Eficiencia (%)')
    
    # Rellenar área bajo la curva
    ax.fill_between(robots, 0, avg_eff, alpha=0.2, color=color1)
    
    # Encontrar punto óptimo
    cost_eff = avg_eff / robots
    idx_opt = np.argmax(cost_eff)
    
    # Marcar punto óptimo
    ax.scatter([robots[idx_opt]], [avg_eff[idx_opt]], s=200, c='#28A745', 
               marker='*', zorder=10, label=f'Óptimo: {robots[idx_opt]} robots')
    
    # Construir título con parámetros
    title = 'Curva de Eficiencia vs Número de Robots\n'
    if params:
        subtitle_parts = []
        if 'N_min' in params and 'N_max' in params:
            subtitle_parts.append(f"Mangos/caja: {params['N_min']}-{params['N_max']}")
        if 'cajas' in params:
            subtitle_parts.append(f"Cajas: {params['cajas']}")
        if 'X' in params:
            subtitle_parts.append(f"Vel: {params['X']:.0f} cm/s")
        if subtitle_parts:
            title += ' | '.join(subtitle_parts)
    else:
        title += 'Sistema de Etiquetado - Mangosa S.A.'
    
    # Configuración
    ax.set_xlabel('Número de Robots', fontsize=12, fontweight='bold')
    ax.set_ylabel('Eficiencia (%)', fontsize=12, fontweight='bold')
    ax.set_title(title, fontsize=12, fontweight='bold')
    
    ax.set_ylim(0, 105)
    ax.set_xlim(0, robots.max() + 1)
    ax.grid(True, alpha=0.3)
    ax.legend(loc='lower right', fontsize=11)
    
    # Línea de 95% y 100%
    ax.axhline(y=95, color='orange', linestyle='--', alpha=0.7, label='95%')
    ax.axhline(y=100, color='green', linestyle='--', alpha=0.7, label='100%')
    
    # Anotaciones
    for i, (r, e) in enumerate(zip(robots, avg_eff)):
        ax.annotate(f'{e:.0f}%', (r, e), textcoords="offset points", 
                   xytext=(0, 10), ha='center', fontsize=9)
    
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'efficiency_curve.png')
    plt.savefig(output_path, dpi=150, bbox_inches='tight', 
                facecolor='white', edgecolor='none')
    print(f"✓ Gráfica guardada en: {output_path}")
    
    return fig

def plot_missed_mangos(data, output_dir):
    """Genera gráfica de mangos perdidos vs robots."""
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    robots = data['robots']
    avg_missed = data['avg_missed']
    avg_eff = data['avg_eff']
    params = data.get('params', {})
    
    # Gráfica de barras para mangos perdidos
    bars = ax.bar(robots, avg_missed, color='#e74c3c', alpha=0.7, 
                  edgecolor='darkred', linewidth=1.5)
    
    # Colorear diferente la barra óptima
    cost_eff = avg_eff / robots
    idx_opt = np.argmax(cost_eff)
    bars[idx_opt].set_color('#27ae60')
    bars[idx_opt].set_edgecolor('darkgreen')
    
    ax.set_xlabel('Número de Robots', fontsize=12, fontweight='bold')
    ax.set_ylabel('Mangos Perdidos Promedio por Caja', fontsize=12, fontweight='bold')
    
    # Construir título con parámetros
    title = 'Mangos No Etiquetados vs Número de Robots\n'
    if params:
        subtitle_parts = []
        if 'N_min' in params and 'N_max' in params:
            subtitle_parts.append(f"Mangos/caja: {params['N_min']}-{params['N_max']}")
        if 'cajas' in params:
            subtitle_parts.append(f"Cajas: {params['cajas']}")
        if 'X' in params:
            subtitle_parts.append(f"Vel: {params['X']:.0f} cm/s")
        if subtitle_parts:
            title += ' | '.join(subtitle_parts)
    
    ax.set_title(title, fontsize=12, fontweight='bold')
    
    ax.set_xticks(robots)
    ax.grid(True, axis='y', alpha=0.3)
    
    # Valores sobre barras
    for bar, val in zip(bars, avg_missed):
        height = bar.get_height()
        ax.annotate(f'{val:.1f}',
                   xy=(bar.get_x() + bar.get_width() / 2, height),
                   xytext=(0, 3), textcoords="offset points",
                   ha='center', va='bottom', fontsize=10, fontweight='bold')
    
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'missed_mangos.png')
    plt.savefig(output_path, dpi=150, bbox_inches='tight', 
                facecolor='white', edgecolor='none')
    print(f"✓ Gráfica guardada en: {output_path}")
    
    return fig

def plot_combined_analysis(data, output_dir):
    """Genera una figura con múltiples subgráficas."""
    
    fig = plt.figure(figsize=(14, 10))
    
    robots = data['robots']
    avg_eff = data['avg_eff']
    min_eff = data['min_eff']
    max_eff = data['max_eff']
    avg_missed = data['avg_missed']
    params = data.get('params', {})
    
    cost_eff = calculate_cost_effectiveness(robots, avg_eff)
    idx_opt = np.argmax(cost_eff)
    
    # Subplot 1: Eficiencia con rango
    ax1 = fig.add_subplot(2, 2, 1)
    ax1.fill_between(robots, min_eff, max_eff, alpha=0.3, color='#3498db')
    ax1.plot(robots, avg_eff, 'o-', color='#2980b9', linewidth=2, markersize=8)
    ax1.scatter([robots[idx_opt]], [avg_eff[idx_opt]], s=150, c='#e74c3c', 
                marker='*', zorder=10)
    ax1.axhline(y=95, color='orange', linestyle='--', alpha=0.5)
    ax1.set_xlabel('Número de Robots')
    ax1.set_ylabel('Eficiencia (%)')
    ax1.set_title('Eficiencia vs Robots')
    ax1.set_ylim(0, 105)
    ax1.grid(True, alpha=0.3)
    
    # Subplot 2: Costo-Efectividad
    ax2 = fig.add_subplot(2, 2, 2)
    colors = ['#27ae60' if i == idx_opt else '#9b59b6' for i in range(len(robots))]
    ax2.bar(robots, cost_eff, color=colors, alpha=0.7, edgecolor='black')
    ax2.set_xlabel('Número de Robots')
    ax2.set_ylabel('Índice Costo-Efectividad (%)')
    ax2.set_title('Costo-Efectividad por Robot')
    ax2.set_xticks(robots)
    ax2.grid(True, axis='y', alpha=0.3)
    
    # Subplot 3: Mangos perdidos
    ax3 = fig.add_subplot(2, 2, 3)
    ax3.plot(robots, avg_missed, 's-', color='#e74c3c', linewidth=2, markersize=8)
    ax3.fill_between(robots, 0, avg_missed, alpha=0.2, color='#e74c3c')
    ax3.scatter([robots[idx_opt]], [avg_missed[idx_opt]], s=150, c='#27ae60', 
                marker='*', zorder=10)
    ax3.set_xlabel('Número de Robots')
    ax3.set_ylabel('Mangos Perdidos/Caja')
    ax3.set_title('Mangos No Etiquetados')
    ax3.grid(True, alpha=0.3)
    
    # Subplot 4: Eficiencia incremental
    ax4 = fig.add_subplot(2, 2, 4)
    incremental = np.diff(avg_eff, prepend=0)
    colors = ['#27ae60' if inc > 5 else '#f39c12' if inc > 2 else '#e74c3c' 
              for inc in incremental]
    ax4.bar(robots, incremental, color=colors, alpha=0.7, edgecolor='black')
    ax4.axhline(y=0, color='black', linewidth=0.5)
    ax4.set_xlabel('Número de Robots')
    ax4.set_ylabel('Ganancia Incremental (%)')
    ax4.set_title('Ganancia por Robot Adicional')
    ax4.set_xticks(robots)
    ax4.grid(True, axis='y', alpha=0.3)
    
    # Título general con parámetros
    title = 'Análisis Completo: Sistema de Etiquetado - Mangosa S.A.\n'
    if params:
        subtitle_parts = []
        if 'N_min' in params and 'N_max' in params:
            subtitle_parts.append(f"Mangos/caja: {params['N_min']}-{params['N_max']}")
        if 'cajas' in params:
            subtitle_parts.append(f"Cajas: {params['cajas']}")
        if 'X' in params:
            subtitle_parts.append(f"Vel: {params['X']:.0f} cm/s")
        if 'Z' in params:
            subtitle_parts.append(f"Caja: {params['Z']:.0f} cm")
        if 'W' in params:
            subtitle_parts.append(f"Banda: {params['W']:.0f} cm")
        if subtitle_parts:
            title += ' | '.join(subtitle_parts)
    
    fig.suptitle(title, fontsize=14, fontweight='bold', y=1.02)
    
    plt.tight_layout()
    
    output_path = os.path.join(output_dir, 'combined_analysis.png')
    plt.savefig(output_path, dpi=150, bbox_inches='tight', 
                facecolor='white', edgecolor='none')
    print(f"✓ Gráfica guardada en: {output_path}")
    
    return fig

def main():
    """Función principal del script."""
    
    # Directorio base del proyecto
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    
    # Archivos de datos
    robot_file = os.path.join(project_dir, 'robot_analysis.dat')
    failure_file = os.path.join(project_dir, 'failure_analysis.dat')
    
    # También buscar archivos .csv
    if not os.path.exists(robot_file):
        robot_file = os.path.join(project_dir, 'robot_analysis.csv')
    if not os.path.exists(failure_file):
        failure_file = os.path.join(project_dir, 'failure_analysis.csv')
    
    # Directorio de salida
    output_dir = script_dir  # Las imágenes se guardan en scripts/
    
    print("\n" + "="*60)
    print("  GENERADOR DE GRÁFICAS - MANGOSA S.A.")
    print("="*60 + "\n")
    
    # Verificar archivo de datos
    if not os.path.exists(robot_file):
        print(f"Error: No se encontró el archivo de datos.")
        print(f"   Buscado en: {robot_file}")
        print("\n   Ejecute primero el programa de análisis:")
        print("   $ ./bin/analysis")
        sys.exit(1)
    
    print(f"Leyendo datos de: {robot_file}")
    
    # Cargar datos
    data = load_robot_analysis(robot_file)
    
    print(f"   → {len(data['robots'])} configuraciones de robots analizadas")
    print(f"   → Rango: {data['robots'].min()} a {data['robots'].max()} robots\n")
    
    # Generar gráficas
    print("Generando gráficas...\n")
    
    try:
        plot_cost_effectiveness_analysis(data, output_dir)
        plot_efficiency_curve(data, output_dir)
        plot_missed_mangos(data, output_dir)
        plot_combined_analysis(data, output_dir)
        
        print("\n" + "="*60)
        print("  GRÁFICAS GENERADAS EXITOSAMENTE")
        print("="*60)
        print(f"\nUbicación: {output_dir}/")
        print("\n   Archivos generados:")
        print("   • cost_effectiveness_analysis.png  (Análisis principal)")
        print("   • efficiency_curve.png             (Curva de eficiencia)")
        print("   • missed_mangos.png                (Mangos perdidos)")
        print("   • combined_analysis.png            (Análisis combinado)")
        
        # Mostrar resumen del punto óptimo
        cost_eff = calculate_cost_effectiveness(data['robots'], data['avg_eff'])
        idx_opt = np.argmax(cost_eff)
        
        print("\n" + "-"*60)
        print("  RESULTADO DEL ANÁLISIS")
        print("-"*60)
        print(f"\n  * PUNTO OPTIMO COSTO-EFECTIVO: {data['robots'][idx_opt]} robots")
        print(f"    • Eficiencia: {data['avg_eff'][idx_opt]:.1f}%")
        print(f"    • Mangos perdidos/caja: {data['avg_missed'][idx_opt]:.2f}")
        print(f"    • Índice costo-efectividad: {cost_eff[idx_opt]:.1f}%")
        
        # Encontrar 100%
        idx_100 = np.where(data['avg_eff'] >= 99.9)[0]
        if len(idx_100) > 0:
            r100 = data['robots'][idx_100[0]]
            print(f"\n  ◆ Para 100% eficiencia: {r100} robots")
            print(f"    • Robots adicionales necesarios: {r100 - data['robots'][idx_opt]}")
        
        print("\n")
        
    except Exception as e:
        print(f"\nError al generar gráficas: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()
