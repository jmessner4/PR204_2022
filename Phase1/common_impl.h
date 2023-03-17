#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

/* autres includes (eventuellement) */

#define ERROR_EXIT(str) {perror(str);exit(EXIT_FAILURE);}

/**************************************************************/
/****************** DEBUT DE PARTIE NON MODIFIABLE ************/
/**************************************************************/

#define MAX_STR  (1024)
typedef char maxstr_t[MAX_STR];

/* definition du type des infos */
/* de connexion des processus dsm */
struct dsm_proc_conn  {
   int      rank;
   maxstr_t machine;
   int      port_num;
   int      fd; 
   int      fd_for_exit; /* special */  
};

typedef struct dsm_proc_conn dsm_proc_conn_t; 

/**************************************************************/
/******************* FIN DE PARTIE NON MODIFIABLE *************/
/**************************************************************/

#define ARG_LEN 128

/* definition du type des infos */
/* d'identification des processus dsm */

struct sons_pipes {
  int* stdoutfd;
  int* stderrfd;
};
typedef struct sons_pipes sons_pipes_t;

struct dsm_proc {   
  pid_t pid;
  dsm_proc_conn_t connect_info;
  sons_pipes_t pipe;

};
typedef struct dsm_proc dsm_proc_t;

int handle_bind(char* port);

int handle_connect(const char* SERV_ADDR, const char* SERV_PORT);

void read_socket(int nb_tosend,int sfd, const void * buff);

void write_socket(int nb_tosend,int sfd, const void * buff);