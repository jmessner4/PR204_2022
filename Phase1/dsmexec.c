#include "common_impl.h"
#include <poll.h>
#define MAX_CONN 5
#define PORT "8080"


/* variables globales */

/* un tableau gerant les infos d'identification */
/* des processus dsm */
dsm_proc_t* proc_array = NULL; 

/* le nombre de processus effectivement crees */
volatile int num_procs_creat = 0;

volatile int zombies = 0;

void usage(void)
{
  fprintf(stdout,"Usage : dsmexec machine_file executable arg1 arg2 ...\n");
  fflush(stdout);
  exit(EXIT_FAILURE);
}



int n_procs()
{
	//Exemple du manuel de getline()
	int nprocs = 0;
	
	FILE * fd;
	ssize_t read;
	size_t len = 0;
	char * line = NULL;

	fd = fopen("machine_file","r");
	while ((read = getline(&line, &len, fd)) != -1) {
		nprocs++;
	}
	return nprocs;
}

void getmachine(dsm_proc_t* proc_array) {
   int nprocs = 0;
	
	FILE * fd;
	ssize_t read;
	size_t len = 0;
	char * line = NULL;

	fd = fopen("machine_file","r");
	while ((read = getline(&line, &len, fd)) != -1) {
		if(read>0){
			strncpy(proc_array[nprocs].connect_info.machine,line, strlen(line)-1);
			proc_array[nprocs].connect_info.rank = nprocs;
			nprocs++;
		}
	}
}

void sigchld_handler(int sig)
{

   zombies++;
   /* on traite les fils qui se terminent */
   /* pour eviter les zombies */
}

/*******************************************************/
/*********** ATTENTION : BIEN LIRE LA STRUCTURE DU *****/
/*********** MAIN AFIN DE NE PAS AVOIR A REFAIRE *******/
/*********** PLUS TARD LE MEME TRAVAIL DEUX FOIS *******/
/*******************************************************/

int main(int argc, char *argv[])
{

	if (argc < 3){
    	usage();
	}
	else if(strcmp("machine_file",argv[1]) != 0){
		usage();
	} else {       
      pid_t pid;
      int num_procs = 0;
      int i;
      

      struct sigaction sa = {0};
      sa.sa_handler = sigchld_handler;
      sa.sa_flags = SA_RESTART;

      sigaction(SIGCHLD,&sa,NULL);

      /* Mise en place d'un traitant pour recuperer les fils zombies*/      
      /* XXX.sa_handler = sigchld_handler; */
      
      /* lecture du fichier de machines */
      /* 1- on recupere le nombre de processus a lancer */
      /* 2- on recupere les noms des machines : le nom de */
      /* la machine est un des elements d'identification */
      
      num_procs = n_procs();      //on récupère le nombre de machine
      proc_array = malloc(num_procs*sizeof(dsm_proc_t));
      getmachine(proc_array);     //On récupère le nom des machines et on remplit la structure

      /* creation de la socket d'ecoute */
      char host[32];
      gethostname(host,32);

      int lfd;
      lfd = handle_bind(host);
      if((listen(lfd,SOMAXCONN)) != 0)
      {
         perror("listen()\n");
         exit(EXIT_FAILURE);
      }

      //Nous avons utilisé le lien suivant pour faire les lignes qui suivent https://gist.github.com/listnukira/4045436

      struct sockaddr_in my_addr;
      bzero(&my_addr, sizeof(my_addr));
      socklen_t len = sizeof(my_addr);
      if (getsockname(lfd, (struct sockaddr *) &my_addr, &len) < 0){
         perror("getsockname()");
      };

      struct sockaddr_in * out = (struct sockaddr_in *)&my_addr;
      
      // Récupération du port en chaine de caractère
      char port_str[64];
      int port=htons(out->sin_port);
      sprintf(port_str,"%d",port);      
      

      

      struct pollfd pollp[2*num_procs];
      int poll_offset = 0;
      /* creation des fils */
      for(i = 0; i < num_procs ; i++) {
         proc_array[i].pipe.stdoutfd = malloc(sizeof(int)*2);
         proc_array[i].pipe.stderrfd = malloc(sizeof(int)*2);
         printf("================== > %s\n", proc_array[i].connect_info.machine);
         /* creation du tube pour rediriger stdout */
         
         pipe(proc_array[i].pipe.stdoutfd);
         
         /* creation du tube pour rediriger stderr */
         pipe(proc_array[i].pipe.stderrfd);
         
         pid = fork();
         if(pid == -1) ERROR_EXIT("fork");
         
         if (pid == 0) { /* fils */	
            /* redirection stdout */
            char rank[8];
            sprintf(rank,"%d",i);

            close(proc_array[i].pipe.stdoutfd[0]);	  
            close(STDOUT_FILENO);
            dup(proc_array[i].pipe.stdoutfd[1]);
            close(proc_array[i].pipe.stdoutfd[1]);
               
            /* redirection stderr */
            close(proc_array[i].pipe.stderrfd[0]);
            close(STDERR_FILENO);	      	      
            dup(proc_array[i].pipe.stderrfd[1]);
            close(proc_array[i].pipe.stderrfd[1]);
            
            /* Creation du tableau d'arguments pour le ssh */ 

            char** argv2 = malloc((argc+4)*sizeof(char *));    //attention argc va peut-être poser problème à l'avenir
            argv2[0] = "ssh";
            argv2[1] = proc_array[i].connect_info.machine;
            argv2[2] = "dsmwrap";
            argv2[3] = host;
            argv2[4] = port_str;
            argv2[5] = rank;

            for(i=2; i<argc; i++) {
               argv2[i+4] = argv[i];  
            }   

            argv2[argc+3] = NULL;

            /* jump to new prog : */

            execvp("ssh",argv2);


         } 
         else  if(pid > 0) { /* pere */		      
            /* fermeture des extremites des tubes non utiles */
            close(proc_array[i].pipe.stdoutfd[1]);
            close(proc_array[i].pipe.stderrfd[1]);

            pollp[poll_offset].fd = proc_array[i].pipe.stdoutfd[0];
            pollp[poll_offset].events = POLLIN;
            pollp[poll_offset].revents = 0;

            poll_offset++;
            
            pollp[poll_offset].fd = proc_array[i].pipe.stderrfd[0];
            pollp[poll_offset].events = POLLIN;
            pollp[poll_offset].revents = 0;

            poll_offset++;

            num_procs_creat++;
         }
     }
      if(pid > 0){ // Création des connexions temporaires
         int num_procs_conn = 0;
         char buff[MAX_STR];
         dsm_proc_conn_t message;
		   dsm_proc_conn_t annuaire[num_procs_creat];

         while(num_procs_conn != (num_procs_creat))
         {
            memset(buff,0,MAX_STR);
            memset(&message,0,sizeof(dsm_proc_conn_t));
            for(i = 0;i<num_procs_creat;i++){
               struct sockaddr client_addr;
               socklen_t size = sizeof(client_addr);
               int client_fd = accept(lfd, &client_addr, &size);
               if (-1 == (client_fd)) 
               {
                  perror("Accept");
               }

               struct sockaddr_in *sockptr = (struct sockaddr_in *)(&client_addr);
               struct in_addr client_address = sockptr->sin_addr;
               write_socket(sizeof(int),client_fd,&num_procs_creat);

               read_socket(sizeof(dsm_proc_conn_t),client_fd,&message);

               annuaire[message.rank].rank = message.rank;
               annuaire[message.rank].port_num = message.port_num; 
               annuaire[message.rank].fd = client_fd; 
               strcpy(annuaire[message.rank].machine,message.machine);
               
               num_procs_conn++;
            }
        }
		for(i = 0;i<num_procs_creat;i++)
		{
			write_socket(num_procs_creat*sizeof(dsm_proc_conn_t),annuaire[i].fd,&annuaire);
		}
		for(i = 0;i<num_procs_creat;i++)
		{
			close(annuaire[i].fd);
		}
      }
      /* on accepte les connexions des processus dsm */

      /*  On recupere le nom de la machine distante */
      /* les chaines ont une taille de MAX_STR */
         //Il faut penser à fermer les extrémités des tubes des frères précédents qui sont transmises lors du fork
      if(pid > 0)
      {
         char buffer[MAX_STR]; 
         char std[MAX_STR];
         int ID = 0;
         while(zombies != num_procs_creat)
         {
            if(poll(pollp,2*num_procs,-1) == -1)
            {
               perror("Poll");
            }   
            int i = 0;
            for(i = 0;i < 2*num_procs;i++)
            {
               memset(buffer,0,MAX_STR);
               if(pollp[i].revents & POLLIN)
               {
                  read(pollp[i].fd,buffer,MAX_STR);
                  pollp[i].revents = 0;
                  if(i%2 == 0)
                  {
                     strcpy(std,"stdout");
                     ID = i/2;
                  }
                  else
                  {
                     strcpy(std,"stderr");
                     ID = i/2;
                  }
                  printf("[Proc %d : %s : %s] %s\n",ID,proc_array[ID].connect_info.machine,std,buffer); 
               }
            }
         }
         free(proc_array);
      }      
      /* on accepte les connexions des processus dsm */
      /*  On recupere le nom de la machine distante */
      /* les chaines ont une taille de MAX_STR */

            
      /* On recupere le pid du processus distant  (optionnel)*/

      /* On recupere le numero de port de la socket */
      /* d'ecoute des processus distants */
            /* cf code de dsmwrap.c */    

      /***********************************************************/ 
      /********** ATTENTION : LE PROTOCOLE D'ECHANGE *************/
      /********** DECRIT CI-DESSOUS NE DOIT PAS ETRE *************/
      /********** MODIFIE, NI DEPLACE DANS LE CODE   *************/
      /***********************************************************/
      
      /* 1- envoi du nombre de processus aux processus dsm*/
      /* On envoie cette information sous la forme d'un ENTIER */
      /* (IE PAS UNE CHAINE DE CARACTERES */
      
      /* 2- envoi des rangs aux processus dsm */
      /* chaque processus distant ne reçoit QUE SON numéro de rang */
      /* On envoie cette information sous la forme d'un ENTIER */
      /* (IE PAS UNE CHAINE DE CARACTERES */
      
      /* 3- envoi des infos de connexion aux processus */
      /* Chaque processus distant doit recevoir un nombre de */
      /* structures de type dsm_proc_conn_t égal au nombre TOTAL de */
      /* processus distants, ce qui signifie qu'un processus */
      /* distant recevra ses propres infos de connexion */
      /* (qu'il n'utilisera pas, nous sommes bien d'accords). */

      /***********************************************************/
      /********** FIN DU PROTOCOLE D'ECHANGE DES DONNEES *********/
      /********** ENTRE DSMEXEC ET LES PROCESSUS DISTANTS ********/
      /***********************************************************/
      
      /* gestion des E/S : on recupere les caracteres */
      /* sur les tubes de redirection de stdout/stderr */     
      /* while(1)
         {
            je recupere les infos sur les tubes de redirection
            jusqu'à ce qu'ils soient inactifs (ie fermes par les
            processus dsm ecrivains de l'autre cote ...)
         
         };
      */

      /* on attend les processus fils */

      
      /* on ferme les descripteurs proprement */
      
      
      /* on ferme la socket d'ecoute */
      close(lfd);
  }   
   exit(EXIT_SUCCESS);  
}

