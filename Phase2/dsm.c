#include "dsm_impl.h"
#include <semaphore.h>

int DSM_NODE_NUM; /* nombre de processus dsm */
int DSM_NODE_ID;  /* rang (= numero) du processus */ 

int master;

static dsm_proc_conn_t *procs = NULL;
static dsm_page_info_t table_page[PAGE_NUMBER]; 
static pthread_t comm_daemon;

int * tab_fds; // Tableau initialisé plus tard, correspond aux fds des processus en global

int num_conns; // Entier, correspondant au nombre de personnes connectées, qui sont encore en train de fonctionner 
// Si cet entier arrive à 0, cela signifie que tous les processus ont fini leurs opérations

sem_t sem; // Sémaphore pour synchroniser le thread de communication avec le thread traitant de signal (lorsqu'il arrive)


/* indique l'adresse de debut de la page de numero numpage */
static char *num2address( int numpage )
{ 
   char *pointer = (char *)(BASE_ADDR+(numpage*(PAGE_SIZE)));
   
   if( pointer >= (char *)TOP_ADDR ){
      fprintf(stderr,"[%i] Invalid address !\n", DSM_NODE_ID);
      return NULL;
   }
   else return pointer;
}

void * get_page(int numpage, void * ptr)
{
   void * base = (void *) num2address(numpage);
   memcpy(ptr,base,PAGE_SIZE);
   return ptr;
}

/* cette fonction permet de recuperer un numero de page */
/* a partir  d'une adresse  quelconque */
static int address2num( char *addr )
{
  return (((intptr_t)(addr - BASE_ADDR))/(PAGE_SIZE));
}

/* cette fonction permet de recuperer l'adresse d'une page */
/* a partir d'une adresse quelconque (dans la page)        */
static char *address2pgaddr( char *addr )
{
  return  (char *)(((intptr_t) addr) & ~(PAGE_SIZE-1)); 
}

/* fonctions pouvant etre utiles */
static void dsm_change_info( int numpage, dsm_page_state_t state, dsm_page_owner_t owner)
{
   if ((numpage >= 0) && (numpage < PAGE_NUMBER)) {	
	if (state != NO_CHANGE )
	table_page[numpage].status = state;
      if (owner >= 0 )
	table_page[numpage].owner = owner;
      return;
   }
   else {
	fprintf(stderr,"[%i] Invalid page number !\n", DSM_NODE_ID);
      return;
   }
}

void write_socket(int sfd,int nb_tosend,void * buff) // Fonction d'écriture dans la socket
{
	int byte_sent = 0;
	int res = 0;
	while(byte_sent != nb_tosend)
	{
		res = send(sfd,buff + byte_sent, nb_tosend-byte_sent,0);
		if(res == -1){
			perror("send()");
		}
		byte_sent += res;
	}
}

void read_socket(int sfd, int nb_tosend,void * buff) // Fonction de lecture de la socket
{
	int byte_sent = 0;
	int res = 0;
	while(byte_sent != nb_tosend)
	{
		res = recv(sfd,buff + byte_sent, nb_tosend-byte_sent,0);
		if(res == -1){
			perror("send()");
		}
		byte_sent += res;
	}
}

int handle_connect(const char* SERV_ADDR, int SERV_PORT) { // Permet de gérer les connexions (pris du projet de réseau)
	struct addrinfo hints, *result, *rp;
	int sfd;
   char port_str[MAX_STR];

   memset(&port_str,0,MAX_STR);

   sprintf(port_str,"%d",SERV_PORT);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(SERV_ADDR, port_str, &hints, &result) != 0) {
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,rp->ai_protocol);
		if (sfd == -1) {
			continue;
		}
		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
			break;
		}
		close(sfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(result);
	return sfd;
}

static dsm_page_owner_t get_owner( int numpage)
{
   return table_page[numpage].owner;
}

static dsm_page_state_t get_status( int numpage)
{
   return table_page[numpage].status;
}

/* Allocation d'une nouvelle page */
static void dsm_alloc_page( int numpage )
{
   char *page_addr = num2address( numpage );
   mmap(page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   return ;
}

/* Changement de la protection d'une page */
static void dsm_protect_page( int numpage , int prot)
{
   char *page_addr = num2address( numpage );
   mprotect(page_addr, PAGE_SIZE, prot);
   return;
}

static void dsm_free_page( int numpage )
{
   char *page_addr = num2address( numpage );
   munmap(page_addr, PAGE_SIZE);
   return;
}

static int dsm_send(int dest,void *buf,size_t size)
{
   /* a completer */
   write_socket(dest, size, buf);
   return 0;
}

static int dsm_recv(int from,void *buf,size_t size)
{
   /* a completer */
   read_socket(from, size, buf);
   return 0;
}

static void *dsm_comm_daemon( void *arg)
{  
   int i;


   int buff = 0; // Buffer d'entier correspondant au type dsm_req_type_t
   dsm_req_t infos = {0}; // Informations sur la page
   struct pollfd pollcom[DSM_NODE_NUM]; 
   num_conns = DSM_NODE_NUM; // Au début, on considère que tous les processus ne sont pas finis

   for(i = 0;i<DSM_NODE_NUM;i++)
   {
      pollcom[i].fd = tab_fds[i];
      pollcom[i].events = POLLIN;
      pollcom[i].revents = 0;
   }

   printf("[%i] Waiting for incoming reqs \n", DSM_NODE_ID);
   while(num_conns > 0)
   {
	/* a modifier */
      if(poll(pollcom,sizeof(struct pollfd),-1) == -1)
      {
         perror("Poll");
      }
      memset(&buff,0,sizeof(int));
      memset(&infos,0,sizeof(dsm_req_t));
      for(i=0;i<DSM_NODE_NUM;i++) {
         if((pollcom[i].revents & POLLIN) && (i != DSM_NODE_ID)) { // Dès qu'on recoit quelque chose venant d'un autre processus...
            pollcom[i].revents = 0;

            dsm_recv(pollcom[i].fd, &buff, sizeof(int)); // On traite d'abord quel type de message on va recevoir


            switch((dsm_req_type_t) buff) {
               case DSM_NO_TYPE : // Pas de type : on fait rien 
                  break;

               case DSM_REQ : // Demande de changement de propriétaire de page

                  dsm_recv(pollcom[i].fd,&infos,sizeof(dsm_req_t)); // On recoit les informations sur la page

                  if(table_page[infos.page_num].owner == DSM_NODE_ID) // On vérifie si on possède bien la page
                  {
                     void * ptr = malloc(PAGE_SIZE);
                     void * base = (void *) num2address(infos.page_num);
                     memcpy(ptr,base,PAGE_SIZE);  // On copie la page mémoire demandée dans un void * ptr
                     dsm_change_info(infos.page_num, READ_ONLY, infos.source);
                     dsm_free_page(infos.page_num); // On libère la page 
                     int j;
                     buff = DSM_PAGE;
                     for(j = 0; j < DSM_NODE_NUM; j++)
                     {
                        if(j != DSM_NODE_ID) // Envoi des informations des pages à tous les processus
                        {
                           dsm_send(pollcom[j].fd,&buff,sizeof(int));
                           dsm_send(pollcom[j].fd, &infos, sizeof(dsm_req_t));
                        }
                     }
                     dsm_send(pollcom[infos.source].fd,ptr,PAGE_SIZE); // Envoi de la page au demandeur 
                  }
                  
                  break;

               case DSM_PAGE :  //On recoit les informations de la page (et la page en question si on l'a demandée et qu'elle est disponible)
                  dsm_recv(pollcom[i].fd,&infos,sizeof(dsm_req_t));
                  if(infos.source != DSM_NODE_ID)
                  {
                     dsm_change_info(infos.page_num,NO_CHANGE,infos.source); // Reception des infos
                  }
                  else
                  {
                     void * ptr = malloc(PAGE_SIZE);
                     dsm_recv(pollcom[i].fd,ptr,PAGE_SIZE); // Réception et allocation de la page en mémoire
                     dsm_change_info(infos.page_num,WRITE,infos.source);
                     dsm_alloc_page(infos.page_num);
                     void * base = (void *) num2address(infos.page_num);
                     memcpy(base,ptr,PAGE_SIZE);
                  }               
                  sem_post(&sem); // On débloque le traitant de signal 
                  break; 

               case DSM_NREQ :
                  break;

               case DSM_FINALIZE : // On recoit un DSM_FINALIZE, correspondant à l'entrée d'un processus dans dsm_finalize()
                  num_conns--;
                  if(num_conns == 0)
                  {
                     return NULL;
                  }
                  break;
            }
         }
      }
   }
   return NULL;
}


static void dsm_handler(dsm_page_owner_t owner, int pagenum)
{  
   /* A modifier */
   dsm_req_type_t access = DSM_REQ;
   printf("[%i] FAULTY  ACCESS !!! \n",DSM_NODE_ID);
   if(owner != DSM_NODE_ID) // On demande la page qui a causé l'erreur
   {      
      dsm_send(tab_fds[owner], &access, sizeof(dsm_req_type_t));

      dsm_req_t* request = malloc(sizeof(dsm_req_t));
      request->page_num = pagenum;
      request->source = DSM_NODE_ID;

      dsm_send(tab_fds[owner], request, sizeof(dsm_req_t));
      free(request);
      sem_wait(&sem); // On attends de recevoir une page
   }
   
}

/* traitant de signal adequat */
static void segv_handler(int sig, siginfo_t *info, void *context)
{
   /* A completer */
   /* adresse qui a provoque une erreur */
   void  *addr = info->si_addr;
  /* Si ceci ne fonctionne pas, utiliser a la place :*/
  /*
   #ifdef __x86_64__
   void *addr = (void *)(context->uc_mcontext.gregs[REG_CR2]);
   #elif __i386__
   void *addr = (void *)(context->uc_mcontext.cr2);
   #else
   void  addr = info->si_addr;
   #endif
   */
   /*
   pour plus tard (question ++):
   dsm_access_t access  = (((ucontext_t *)context)->uc_mcontext.gregs[REG_ERR] & 2) ? WRITE_ACCESS : READ_ACCESS;   
  */   
   /* adresse de la page dont fait partie l'adresse qui a provoque la faute */
   void  *page_addr  = (void *)(((unsigned long) addr) & ~(PAGE_SIZE-1));

   if ((addr >= (void *)BASE_ADDR) && (addr < (void *)TOP_ADDR))
     {
        int numpage = address2num(addr);
        dsm_page_owner_t owner = get_owner(numpage);
        dsm_handler(owner, numpage);
     }
   else
     {
	/* SIGSEGV normal : ne rien faire*/
         fprintf(stderr, "SEG FAULT \n");
     }
}

/* Seules ces deux dernieres fonctions sont visibles et utilisables */
/* dans les programmes utilisateurs de la DSM                       */
char *dsm_init(int argc, char *argv[])
{   
   struct sigaction act;
   int index;   
   /* Récupération de la valeur des variables d'environnement */
   /* DSMEXEC_FD et MASTER_FD                                 */
   char * DSMEXEC_FD = getenv("DSMEXEC_FD");
   char * MASTER_FD = getenv("MASTER_FD");

   sem_init(&sem,0,0);

   int dsmexec = atoi(DSMEXEC_FD);
   master = atoi(MASTER_FD); 

   printf("fdsmexec, fdmaster : %d %d\n",dsmexec,master);


   
   /* reception du nombre de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_NUM) */
   read_socket(dsmexec,sizeof(int),&DSM_NODE_NUM);
   printf("DSMNUM : %d\n",DSM_NODE_NUM);
   /* reception de mon numero de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_ID)      */
   read_socket(dsmexec,sizeof(int),&DSM_NODE_ID);
   printf("DSMID : %d\n",DSM_NODE_ID);
   /* reception des informations de connexion des autres */
   /* processus envoyees par le lanceur :                */
   /* nom de machine, numero de port, etc.               */
   dsm_proc_conn_t annuaire[DSM_NODE_NUM]; // Annuaire de tous les processus distants
   read_socket(dsmexec, sizeof(dsm_proc_conn_t)*DSM_NODE_NUM, &annuaire);
   
   close(dsmexec);
   annuaire[DSM_NODE_ID].fd = master;
   /* initialisation des connexions              */ 
   /* avec les autres processus : connect/accept */
   int i = 0;
   int nbaccept = 0;
   int rankrec = 0;
   if((listen(master,DSM_NODE_NUM)) != 0)
   {
      perror("listen()\n");
   }   
   if(DSM_NODE_ID != 0) {
      while(nbaccept<DSM_NODE_ID) {
         struct sockaddr client_addr;
         socklen_t size = sizeof(client_addr);
         int client_fd = accept(master, &client_addr, &size);  //on accepte la connexion
         read_socket(client_fd, sizeof(int), &rankrec);   //on récupère le rang de la machine à laquelle on vient de se connecter
         annuaire[rankrec].fd = client_fd;
         nbaccept ++;
      }
      for(i=nbaccept+1;i<DSM_NODE_NUM;i++) { // On se connecte aux autres processus
         annuaire[i].fd = handle_connect(annuaire[i].machine, annuaire[i].port_num);
         write_socket(annuaire[i].fd, sizeof(int), &DSM_NODE_ID);
      }
   }
   else if(DSM_NODE_ID == 0) { // Si on est de rang 0, alors on fait uniquement des demandes de connexion 
       for(i=1;i<DSM_NODE_NUM;i++) {
         annuaire[i].fd = handle_connect(annuaire[i].machine, annuaire[i].port_num);
         write_socket(annuaire[i].fd, sizeof(int), &DSM_NODE_ID);
      }
   }

   tab_fds = malloc(DSM_NODE_NUM*sizeof(int)); // Tab contenant les fds
   int k = 0;
   for(k = 0;k<DSM_NODE_NUM;k++)
   {
      tab_fds[k] = annuaire[k].fd;
   }

   /* Allocation des pages en tourniquet */
   for(index = 0; index < PAGE_NUMBER; index ++){	
     if ((index % DSM_NODE_NUM) == DSM_NODE_ID)
       dsm_alloc_page(index);	     
     dsm_change_info( index, WRITE, index % DSM_NODE_NUM);
   }

   pthread_create(&comm_daemon, NULL, dsm_comm_daemon, NULL);
   
   
   /* mise en place du traitant de SIGSEGV */
   act.sa_flags = SA_SIGINFO; 
   act.sa_sigaction = segv_handler;
   sigaction(SIGSEGV, &act, NULL);
   
   /* creation du thread de communication           */
   /* ce thread va attendre et traiter les requetes */
   /* des autres processus                          */
   
   
   /* Adresse de début de la zone de mémoire partagée */
   return ((char *)BASE_ADDR);
}

void dsm_finalize( void )
{
   num_conns--; // On réduit num_conns de 1 lorsqu'on arrive dans dsm_finalize()
   
   int i;
   dsm_req_type_t type = DSM_FINALIZE;
   for(i = 0;i < DSM_NODE_NUM;i++) // On notifie les autres processus de notre état
   {
      if(DSM_NODE_ID != i)
      {
         dsm_send(tab_fds[i],&type,sizeof(dsm_req_type_t));
      }
   } 

   pthread_join(comm_daemon,NULL); // On join les threads et on close les connexions
   for(i = 0;i<DSM_NODE_NUM;i++)
   {
      if(i != DSM_NODE_ID)
      {
         close(tab_fds[i]);
      }
   }
   close(master);

  return;
}

