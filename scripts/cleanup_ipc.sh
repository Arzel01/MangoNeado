#!/bin/bash
# Limpieza de recursos IPC

echo "Limpiando recursos IPC..."

# Claves IPC: 0x4D414E48 (MSG), 0x4D414E49 (SHM), 0x4D414E4A (SEM)

ipcrm -Q 0x4D414E48 2>/dev/null && echo "Cola de mensajes eliminada" || echo "No existe"
ipcrm -M 0x4D414E49 2>/dev/null && echo "Memoria compartida eliminada" || echo "No existe"
ipcrm -S 0x4D414E4A 2>/dev/null && echo "Semáforos eliminados" || echo "No existen"
echo ""
echo "Estado IPC actual:"
ipcs -q -m -s 2>/dev/null
echo ""
echo "Limpieza completada."
