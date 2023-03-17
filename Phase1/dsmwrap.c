#include "common_impl.h"
#define PORT "8080"
int rank;

int rank = 0;
volatile int num_procs_creat = 0;
int DSM_NODE_NUM = 0;
int DSM_NODE_ID = 0;

int main(int argc, char **argv)
{   
    char host[32];
    char port_str[64];
    memset(host,0,32);
    memset(port_str,0,64);

    strcpy(host,argv[1]);
    strcpy(port_str,argv[2]);
    rank = atoi(argv[3]);

    char hostname[32];
    gethostname(hostname,32);
    rank = atoi(argv[3]);


    /* processus intermediaire pour "nettoyer" */
    /* la liste des arguments qu'on va passer */
    /* a la commande a executer finalement  */

    /* creation d'une socket pour se connecter au */
    /* au lanceur et envoyer/recevoir les infos */
    /* necessaires pour la phase dsm_init */   
    int sfd1 = -1;
    while(sfd1 == -1)
    {
       sfd1 = handle_connect(host,port_str);
    }
	// quoi comme SERV_ADDR ? sfd1 = handle_connect(ADDR, PORT);
    read_socket(sizeof(int),sfd1, &num_procs_creat); // On recoit le nombre de machines connectées 
    DSM_NODE_NUM = num_procs_creat;
    DSM_NODE_ID = rank;


    /* Envoi du nom de machine au lanceur */
    /* Envoi du pid au lanceur (optionnel) */

    /* Creation de la socket d'ecoute pour les */
    /* connexions avec les autres processus dsm */
    int sfd;
    //char* port;
    sfd = handle_bind(hostname);
    if ((listen(sfd, SOMAXCONN)) != 0) {
        perror("listen()\n");
        exit(EXIT_FAILURE);
    }

    char sfd_str[8];
    sprintf(sfd_str,"%d",sfd);
    char sfd1_str[8];
    sprintf(sfd1_str,"%d",sfd1);

    char num_str[64];
    sprintf(num_str,"%d",DSM_NODE_NUM);
    char id_str[64];
    sprintf(id_str,"%d",DSM_NODE_ID);

    setenv("MASTER_FD",sfd_str,1);
    setenv("DSMEXEC_FD",sfd1_str,1);
    setenv("DSM_NODE_NUM",num_str,1);
    setenv("DSM_NODE_ID",id_str,1);

    struct sockaddr_in my_addr;
    bzero(&my_addr, sizeof(my_addr));
    socklen_t len = sizeof(my_addr);
    if (getsockname(sfd, (struct sockaddr *) &my_addr, &len) < 0){
        perror("getsockname()");
    };

    struct sockaddr_in * out = (struct sockaddr_in *)&my_addr;

    int port=htons(out->sin_port);  

    dsm_proc_conn_t message;
    strcpy(message.machine,hostname);
    message.port_num = port;
    message.rank = rank;

    write_socket(sizeof(dsm_proc_conn_t),sfd1,&message);


    dsm_proc_conn_t annuaire[num_procs_creat];
    memset(&annuaire,0,num_procs_creat*sizeof(dsm_proc_conn_t));

    read_socket(num_procs_creat*sizeof(dsm_proc_conn_t),sfd1,&annuaire);

    int i;

    // /* Envoi du numero de port au lanceur */
    // /* pour qu'il le propage à tous les autres */
    // /* processus dsm */
    // /* on execute la bonne commande */
    // /* attention au chemin à utiliser ! */
    char* newargv[argc-4];
    for(i=4; i<=argc; i++) {
        newargv[i-4] =  argv[i];
     }
     newargv[argc-3] = NULL;
     execvp(argv[4], newargv);
     perror("execvp()");
 
    // /************** ATTENTION **************/
    // /* vous remarquerez que ce n'est pas   */
    // /* ce processus qui récupère son rang, */
    // /* ni le nombre de processus           */
    // /* ni les informations de connexion    */
    // /* (cf protocole dans dsmexec)         */
    // /***************************************/

    return 0;
}
