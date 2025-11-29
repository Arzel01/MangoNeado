#ifndef IPC_UTILS_H
#define IPC_UTILS_H

#include "common.h"

/* Prototipos de funciones - Cola de mensajes */

/**
Crea o abre una cola de mensajes
  * key Clave única para la cola
  * create Si es true, crea la cola; si es false, solo la abre
  * return: ID de la cola de mensajes, o -1 en caso de error
 */
int ipc_create_message_queue(key_t key, bool create);

/**
Elimina una cola de mensajes
  * msgid ID de la cola de mensajes
  * return: 0 en éxito, -1 en error
 */
int ipc_remove_message_queue(int msgid);

/**
Envía datos de una caja a través de la cola de mensajes
  * msgid ID de la cola
  * box Puntero a la estructura Box
  * return: 0 en éxito, -1 en error
 */
int ipc_send_box_data(int msgid, const Box *box);

/**
Recibe datos de una caja de la cola de mensajes
  * msgid ID de la cola
  * box Puntero donde se almacenarán los datos
  * blocking Si es true, espera hasta recibir; si es false, retorna inmediatamente
  * return: 0 en éxito, -1 en error, -2 si no hay mensajes (no bloqueante)
 */
int ipc_receive_box_data(int msgid, Box *box, bool blocking);

/**
Envía estado de un robot
  * msgid ID de la cola
  * robot Puntero a la estructura Robot
  * return: 0 en éxito, -1 en error
 */
int ipc_send_robot_status(int msgid, const Robot *robot);

/**
Envía mensaje de control
  * msgid ID de la cola
  * command Comando a enviar
  * target_robot Robot objetivo (-1 para todos)
  * value Valor asociado
  * return: 0 en éxito, -1 en error
 */
int ipc_send_control(int msgid, int command, int target_robot, int value);

/* Prototipos de funciones - Memoria compartida */

/**
Crea un segmento de memoria compartida
  * key Clave única
  * size Tamaño del segmento
  * create Si es true, crea el segmento
  * return: ID del segmento, o -1 en error
 */
int ipc_create_shared_memory(key_t key, size_t size, bool create);

/**
Adjunta memoria compartida al proceso
  * shmid ID del segmento
  * return: Puntero a la memoria, o NULL en error
 */
void* ipc_attach_shared_memory(int shmid);

/**
Desadjunta memoria compartida
  * ptr Puntero a la memoria compartida
  * return: 0 en éxito, -1 en error
 */
int ipc_detach_shared_memory(void *ptr);

/**
Elimina segmento de memoria compartida
  * shmid ID del segmento
  * return: 0 en éxito, -1 en error
 */
int ipc_remove_shared_memory(int shmid);

/* Prototipos de funciones - Semáforos */

/**
Crea un conjunto de semáforos
  * key Clave única
  * num_sems Número de semáforos
  * create Si es true, crea los semáforos
  * return: ID del conjunto, o -1 en error
 */
int ipc_create_semaphores(key_t key, int num_sems, bool create);

/**
Inicializa un semáforo
  * semid ID del conjunto
  * sem_num Número del semáforo
  * value Valor inicial
  * return: 0 en éxito, -1 en error
 */
int ipc_init_semaphore(int semid, int sem_num, int value);

/**
Operación wait (P) en semáforo
  * semid ID del conjunto
  * sem_num Número del semáforo
  * return: 0 en éxito, -1 en error
 */
int ipc_sem_wait(int semid, int sem_num);

/**
Operación signal (V) en semáforo
  * semid ID del conjunto
  * sem_num Número del semáforo
  * return: 0 en éxito, -1 en error
 */
int ipc_sem_signal(int semid, int sem_num);

/**
Intenta hacer wait sin bloquear
  * semid ID del conjunto
  * sem_num Número del semáforo
  * return: 0 si se obtuvo el semáforo, -1 si está ocupado
 */
int ipc_sem_trywait(int semid, int sem_num);

/**
Elimina conjunto de semáforos
  * semid ID del conjunto
  * return: 0 en éxito, -1 en error
 */
int ipc_remove_semaphores(int semid);

/* Implementación de funciones - Cola de mensajes */

int ipc_create_message_queue(key_t key, bool create) {
    int flags = 0666;
    if (create) {
        flags |= IPC_CREAT | IPC_EXCL;
    }
    
    int msgid = msgget(key, flags);
    if (msgid == -1) {
        if (errno == EEXIST && create) {
            /* La cola ya existe, intentar abrirla */
            msgid = msgget(key, 0666);
        }
        if (msgid == -1) {
            LOG_ERROR("Error al crear/abrir cola de mensajes: %s", strerror(errno));
            return -1;
        }
    }
    
    LOG_DEBUG("Cola de mensajes %s (ID: %d)", create ? "creada" : "abierta", msgid);
    return msgid;
}

int ipc_remove_message_queue(int msgid) {
    if (msgctl(msgid, IPC_RMID, NULL) == -1) {
        LOG_ERROR("Error al eliminar cola de mensajes: %s", strerror(errno));
        return -1;
    }
    LOG_DEBUG("Cola de mensajes eliminada (ID: %d)", msgid);
    return 0;
}

int ipc_send_box_data(int msgid, const Box *box) {
    if (box == NULL) {
        LOG_ERROR("Puntero a caja nulo");
        return -1;
    }
    
    BoxMessage msg;
    msg.mtype = MSG_TYPE_BOX_DATA;
    memcpy(&msg.box, box, sizeof(Box));
    
    if (msgsnd(msgid, &msg, sizeof(Box), 0) == -1) {
        LOG_ERROR("Error al enviar datos de caja: %s", strerror(errno));
        return -1;
    }
    
    LOG_DEBUG("Datos de caja %d enviados (%d mangos)", box->id, box->num_mangos);
    return 0;
}

int ipc_receive_box_data(int msgid, Box *box, bool blocking) {
    if (box == NULL) {
        LOG_ERROR("Puntero a caja nulo");
        return -1;
    }
    
    BoxMessage msg;
    int flags = blocking ? 0 : IPC_NOWAIT;
    
    ssize_t result = msgrcv(msgid, &msg, sizeof(Box), MSG_TYPE_BOX_DATA, flags);
    
    if (result == -1) {
        if (errno == ENOMSG && !blocking) {
            return -2; /* No hay mensajes disponibles */
        }
        if (errno != EINTR) {
            LOG_ERROR("Error al recibir datos de caja: %s", strerror(errno));
        }
        return -1;
    }
    
    memcpy(box, &msg.box, sizeof(Box));
    LOG_DEBUG("Datos de caja %d recibidos (%d mangos)", box->id, box->num_mangos);
    return 0;
}

int ipc_send_robot_status(int msgid, const Robot *robot) {
    if (robot == NULL) {
        LOG_ERROR("Puntero a robot nulo");
        return -1;
    }
    
    RobotStatusMessage msg;
    msg.mtype = MSG_TYPE_ROBOT_STATUS;
    msg.robot_id = robot->id;
    msg.state = robot->state;
    msg.labels_placed = robot->labels_placed;
    
    if (msgsnd(msgid, &msg, sizeof(RobotStatusMessage) - sizeof(long), 0) == -1) {
        LOG_ERROR("Error al enviar estado de robot: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

int ipc_send_control(int msgid, int command, int target_robot, int value) {
    ControlMessage msg;
    msg.mtype = MSG_TYPE_CONTROL;
    msg.command = command;
    msg.target_robot = target_robot;
    msg.value = value;
    
    if (msgsnd(msgid, &msg, sizeof(ControlMessage) - sizeof(long), 0) == -1) {
        LOG_ERROR("Error al enviar mensaje de control: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

/* Implementación de funciones - Memoria compartida */

int ipc_create_shared_memory(key_t key, size_t size, bool create) {
    int flags = 0666;
    if (create) {
        flags |= IPC_CREAT | IPC_EXCL;
    }
    
    int shmid = shmget(key, size, flags);
    if (shmid == -1) {
        if (errno == EEXIST && create) {
            /* Ya existe, intentar abrirlo */
            shmid = shmget(key, size, 0666);
        }
        if (shmid == -1) {
            LOG_ERROR("Error al crear/abrir memoria compartida: %s", strerror(errno));
            return -1;
        }
    }
    
    LOG_DEBUG("Memoria compartida %s (ID: %d, size: %zu)", 
              create ? "creada" : "abierta", shmid, size);
    return shmid;
}

void* ipc_attach_shared_memory(int shmid) {
    void *ptr = shmat(shmid, NULL, 0);
    if (ptr == (void*)-1) {
        LOG_ERROR("Error al adjuntar memoria compartida: %s", strerror(errno));
        return NULL;
    }
    LOG_DEBUG("Memoria compartida adjuntada en %p", ptr);
    return ptr;
}

int ipc_detach_shared_memory(void *ptr) {
    if (ptr == NULL) {
        return 0;
    }
    if (shmdt(ptr) == -1) {
        LOG_ERROR("Error al desadjuntar memoria compartida: %s", strerror(errno));
        return -1;
    }
    LOG_DEBUG("Memoria compartida desadjuntada");
    return 0;
}

int ipc_remove_shared_memory(int shmid) {
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        LOG_ERROR("Error al eliminar memoria compartida: %s", strerror(errno));
        return -1;
    }
    LOG_DEBUG("Memoria compartida eliminada (ID: %d)", shmid);
    return 0;
}

/* Implementación de funciones - Semáforos */

/* Unión requerida para semctl en algunos sistemas */
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int ipc_create_semaphores(key_t key, int num_sems, bool create) {
    int flags = 0666;
    if (create) {
        flags |= IPC_CREAT | IPC_EXCL;
    }
    
    int semid = semget(key, num_sems, flags);
    if (semid == -1) {
        if (errno == EEXIST && create) {
            semid = semget(key, num_sems, 0666);
        }
        if (semid == -1) {
            LOG_ERROR("Error al crear/abrir semáforos: %s", strerror(errno));
            return -1;
        }
    }
    
    LOG_DEBUG("Semáforos %s (ID: %d, count: %d)", 
              create ? "creados" : "abiertos", semid, num_sems);
    return semid;
}

int ipc_init_semaphore(int semid, int sem_num, int value) {
    union semun arg;
    arg.val = value;
    
    if (semctl(semid, sem_num, SETVAL, arg) == -1) {
        LOG_ERROR("Error al inicializar semáforo %d: %s", sem_num, strerror(errno));
        return -1;
    }
    
    LOG_DEBUG("Semáforo %d inicializado a %d", sem_num, value);
    return 0;
}

int ipc_sem_wait(int semid, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;
    
    while (semop(semid, &op, 1) == -1) {
        if (errno != EINTR) {
            LOG_ERROR("Error en wait semáforo %d: %s", sem_num, strerror(errno));
            return -1;
        }
        /* Si fue interrumpido por señal, reintentar */
    }
    
    return 0;
}

int ipc_sem_signal(int semid, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = 1;
    op.sem_flg = 0;
    
    if (semop(semid, &op, 1) == -1) {
        LOG_ERROR("Error en signal semáforo %d: %s", sem_num, strerror(errno));
        return -1;
    }
    
    return 0;
}

int ipc_sem_trywait(int semid, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = IPC_NOWAIT;
    
    if (semop(semid, &op, 1) == -1) {
        if (errno == EAGAIN) {
            return -1; /* Semáforo ocupado */
        }
        LOG_ERROR("Error en trywait semáforo %d: %s", sem_num, strerror(errno));
        return -1;
    }
    
    return 0;
}

int ipc_remove_semaphores(int semid) {
    if (semctl(semid, 0, IPC_RMID) == -1) {
        LOG_ERROR("Error al eliminar semáforos: %s", strerror(errno));
        return -1;
    }
    LOG_DEBUG("Semáforos eliminados (ID: %d)", semid);
    return 0;
}

#endif /* IPC_UTILS_H */
