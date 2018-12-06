#include <stdio.h>

#include <string.h>

#include <stdlib.h>

#include <unistd.h>

#include <pthread.h>

#include <dirent.h>

#include <sys/types.h>

#include <sys/stat.h>

#include <sys/sysinfo.h>


//variabile per il path
char pathname[128];
//numero di core la metto globale perche' verra' utilizzata in diverse funzioni 
int numero_core;

int controlloesc[2];//variabile di controllo

//struttura file
typedef struct s1{
	char nomefile[64];//nome del file 
	struct stat stat_file; //tutte le statistiche 
    int flag_type;//se flag = 0 file, se flag =1 directory, se flag =-1 directory vuota 
    char pathfile[128];//path di un file
}filestruct;


//lista file
typedef struct n1{
	filestruct info;//ciascun file
	struct n1 *next;//puntatore al successivo 
}nodofile, *l_lista_file;

l_lista_file listafile;//lista globale

//funzione thread 
void *fthread(void* args);

//Mutex 
pthread_mutex_t tmutex;
pthread_mutex_t mutex_uscita;


//Funzioni

//INSERIMENTO
int inserisci_inlista(l_lista_file *lista,struct dirent *file,char *pathcorrente,int flag);

//STAMPA
void ls(l_lista_file listastampa);

//ricerca della directory
l_lista_file ricerca_directory(l_lista_file lista);

//cancellazione directory
int remove_directory(l_lista_file *lista);

//scansiono la directory 
l_lista_file scansione_dir(char *pathname_directory);

//unione dei file 
int unisci_liste(l_lista_file *lista_principale, l_lista_file listadaunire);

//inserimento della della directory nel caso in cui sia vuota 
int inserimentodirectory_vuota(l_lista_file *lista_principale,char *path_corrente);
//Menu
int menu();
//main
int main(int arg, char*argv[1]){
	if(argv[1]==NULL){
		printf("Errore non hai inserito un path.\n");
		exit(EXIT_FAILURE);
	}
DIR *directory;//flusso di directory
struct dirent *file;//Struttura dirent coppia i-number nome file di una directory   
struct stat s_stat;//informazioni contenute sugli inode del file 

//Path provvisorio 
char path[128];
//inizializzo la lista 
listafile=NULL;

//stabilisco il numero di core attraverso una funzione che mi restituisce il numero di core corrente 
numero_core=get_nprocs_conf();

//flag 
int flag;
int check;//variabile di controllo 

//THREAD
pthread_t thread[numero_core];

//MUTEX
pthread_mutex_init(&tmutex,NULL);
pthread_mutex_init(&mutex_uscita,NULL);

//Inizializzo il controlloesc 
//controllo uscita mantiene in vita tutti i thread fino a quando ci sono directory non ancora scansionate 
for(int i =0; i<numero_core;i++){
	controlloesc[i]=0;
}

//Applico la lock sul mutex della lista: tmutex  
pthread_mutex_lock(&tmutex);

//copio il path inserito da imput sul valore path
strcpy(path,argv[1]);
//Cosi posso leggere un qualsiasi path con terminazione '/'
if(path[strlen(path)-1]=='/'){
	strncpy(pathname,path,strlen(path)-1);
}
else {
	strcpy(pathname,argv[1]);
}
//stampo il path inserito.
printf("\nIL Path inserito  e': %s\n",pathname);


if((directory=opendir(pathname))!=NULL){
	//apro la directory che ha come path la variabile pathname

//funzione readdir legge all'interno di una directory restituisce un puntatore di tipo struct dirent
  while((file = readdir(directory))!=NULL){
     

     //controllo il file che sto analizzando e' una directory 
     if(file->d_type == DT_DIR)
     	flag =1; //flag =1 directory
     
     else{
     	flag =0;//flag =0 file 
     } 

    //inserisco in lista  
  	check=inserisci_inlista(&listafile,file,pathname,flag);
    //controllo l'esito dell'inserimento in lista 
    if(check==-1){
    	perror("Errore nell'inserimento in lista.\n");
    	exit(EXIT_FAILURE);
    }

  										}
  
  closedir(directory);	

								}

else{
	perror("\nErrore nell'apertura del path.\n");
	exit(EXIT_FAILURE);
}


//creo i thread in base al numero di core 
for(int i=0;i<numero_core;i++){
	pthread_create(&thread[i],NULL,fthread,(void*)(intptr_t)i);
}

//sblocco il mutex
pthread_mutex_unlock(&tmutex);

printf("\nAttendo che la Ricerca finisca...\n");
fflush(stdout);


//attendo la ricerca compiuta dai thread
for(int i=0;i<numero_core;i++ ){
	pthread_join(thread[i],NULL);
}
printf("Ricerca completata.\n");
fflush(stdout);
menu();
 
//distruggo i mutex 
pthread_mutex_destroy(&tmutex);
pthread_mutex_destroy(&mutex_uscita);

//*
exit(EXIT_SUCCESS);
}

//Funzione thread
void *fthread(void *args){
	//quale thread sta lavorando
	int numero_thread= (intptr_t)args;
    
    l_lista_file directory;
    int trovato;
    int esc=0;//condizione per uscire dal ciclo while 
    int unione=0;//condizione di riuscita dell'unione
    int esito;
    while(!esc){
        //blocco con il thread il mutex della lista 
        pthread_mutex_lock(&tmutex);

        //cerco la directory all'interno della lista 
        directory= ricerca_directory(listafile);
        
        if(directory!=NULL){
        	//se ho trovato la directory la elimino dalla lista  
        	trovato= remove_directory(&listafile);
            directory->next=NULL;      
           //rilascio il mutex della lista 
          pthread_mutex_unlock(&tmutex);

          //leggo il contenuto della directory che ho eliminato e carico nella lista i file presenti all'interno 
          //blocco il mutex della variabile controlloesc e dopo averne inizializzato il valore a 0 la sblocco 
          pthread_mutex_lock(&mutex_uscita);
          controlloesc[0]=0;
          pthread_mutex_unlock(&mutex_uscita);

          //leggo il contenuto della directory e costruisco una lista locale al thread per poi successivamente unirla con quella globale 
          l_lista_file sottodirectory;

          char path_sottodirectory[128];
          //creo il path della sottodirectory 
          strcpy(path_sottodirectory,directory->info.pathfile);
          strcat(path_sottodirectory,"/");
          strcat(path_sottodirectory,directory->info.nomefile);
           
         //scansiono la sotto directory   
         sottodirectory=scansione_dir(path_sottodirectory);   
          //una volta creata la lista
          free(directory);//la elimino;

//unisco la lista locale a quella globale 
          while(!unione){
          //accedo alla lista ma prima ne devo sbloccare il mutex 
          	pthread_mutex_lock(&tmutex);

          //se la sottodirectory e' vuota 
            if(sottodirectory==NULL){
                //INSERISCO LA DIRECTORY VUOTA IN LISTA
   inserimentodirectory_vuota(&listafile,path_sottodirectory);
            unione=1;//l'unione e' riuscita 
             pthread_mutex_unlock(&tmutex);


            						}
            						
            						//se la sottodirectory e' piena unisco alla lista 
            						else
            						{

            							esito= unisci_liste(&listafile,sottodirectory);
                                      pthread_mutex_unlock(&tmutex);
                                      unione=1; 

            							}
                       

           					} 
          unione=0;//resetto la condizione di unione 
      }
else
{

	/*Quando la lista non avra' piu' directory allora i thread metteranno il loro stato di uscita ad
	cosi controllano la somma degli elementi contenuti nel vettore se e' uguale al numero di core allora tutti i thread
	non hanno rilevato directory quindi possono terminare 
	*/
	pthread_mutex_lock(&mutex_uscita);
	controlloesc[numero_thread]=1;
	//variabile di controllo 
	int somma=0;
	for(int i=0; i<numero_thread;i++)
		if(controlloesc[i]==1) somma=somma +1;
    if(somma==numero_core) esc=1;
    else esc=0;
    pthread_mutex_unlock(&mutex_uscita);
      pthread_mutex_unlock(&tmutex);
}
}
pthread_exit(NULL);
}

//INSERIMENTO 
int inserisci_inlista(l_lista_file *lista,struct dirent *file,char *pathcorrente,int flag){
	char name[50];
	strcpy(name,file->d_name);
	//se la lista e' vuota inserisco il file 
	if((*lista)==NULL){
		//inserisco in lista tutti gli elementi eccetto le cartelle che iniziano per .
		if(strcmp(name,".")!=0 && strncmp(name,".",1)!=0 ){
			
			l_lista_file aux;//variabile ausiliare 
			
			//alloco memoria
			aux= (l_lista_file)malloc(sizeof(nodofile));
            
            //copio nella lista ausiliare il nome del file 
            strcpy(aux->info.nomefile,name);
            
            //copio nella lista ausiliare il path
            strcpy(aux->info.pathfile,pathcorrente);
            
            stat(file->d_name, &aux->info.stat_file);

            aux->info.flag_type=flag;
            aux->next= *lista;
            *lista=aux;
            return 1;
		}
	}
   if(strcmp(name,".")!=0 && strncmp(name,".",1)!=0 ){
     
     if(strcmp((*lista)->info.nomefile,name)>0){
     //inserimento in testa
l_lista_file aux;//variabile ausiliare 
			
			//alloco memoria
			aux= (l_lista_file)malloc(sizeof(nodofile));
            
            //copio nella lista ausiliare il nome del file 
            strcpy(aux->info.nomefile,name);
            
            //copio nella lista ausialiare il path
            strcpy(aux->info.pathfile,pathcorrente);
            
            stat(file->d_name, &aux->info.stat_file);

            aux->info.flag_type=flag;
            aux->next= *lista;
            *lista=aux;
            return 1;
     }
 else  if(strcmp((*lista)->info.nomefile,name)<0){
//implemento la ricorsività 
inserisci_inlista(&(*lista)->next,file,pathcorrente,flag);
  }

   }
return 0;
}

//STAMPA
void ls(l_lista_file listastampa){
	printf("\n");
	while(listastampa!=NULL){
		//Nome
		printf("\n|Nome_File:%s ",listastampa->info.nomefile);
    //Pathname
    printf("\n|                                ||PathFile: %s",listastampa->info.pathfile);

   listastampa=listastampa->next;
	}
	printf("\n-----------------------------------------------------------\n\n");
}

//ricerca della directory
l_lista_file ricerca_directory(l_lista_file lista){
	while(lista!=NULL){
		if(lista->info.flag_type==1) return lista;
		else {
			lista=lista->next;
	
	}
}
	return NULL;
}

//cancellazione directory
int remove_directory(l_lista_file *lista){
	while(*lista!=NULL){
		if((*lista)->info.flag_type ==1){
			*lista=(*lista)->next;
			return 1;
		}
		else {
			return remove_directory(&(*lista)->next);
	}
}
	return 0;
}

//scansiono la directory 
l_lista_file scansione_dir(char *pathname_directory){
	DIR *directory;
	struct dirent *file;
	struct stat fstat;
	l_lista_file sottodirectory=NULL;
	int check;
	int flag;
   if((directory=opendir(pathname_directory))!=NULL){
   	
   	while((file=readdir(directory))!=NULL){
   		if(file->d_type==DT_DIR){

   			flag=1;
   		}
   		else
   			{ 
   				flag=0;
   	}
   	check = inserisci_inlista(&sottodirectory,file,pathname_directory,flag);
   }
   closedir(directory);
}  
   else {
   }
   if(sottodirectory != NULL){
   	return sottodirectory;
   }
   return NULL;
}

//unione dei file 
int unisci_liste(l_lista_file *lista_principale, l_lista_file listadaunire)
//unisce alla lista globale la lista locale dei thread 
{
   if((*lista_principale)==NULL){
   	*lista_principale= listadaunire;
   	return 1;
   }
   else if((*lista_principale)->next==NULL){
   	(*lista_principale)->next= listadaunire;
return 1;
   	   }
return unisci_liste(&(*lista_principale)->next,listadaunire);
}

//inserimento della della directory nel caso in cui sia vuota 
int inserimentodirectory_vuota(l_lista_file *lista_principale,char *path_corrente){
	l_lista_file aux;
	aux=(l_lista_file)malloc(sizeof(nodofile));
	strcpy(aux->info.nomefile,"|| DIRECTORY VUOTA || ");
	strcpy(aux->info.pathfile,path_corrente);
    aux->info.flag_type= -1;//directory vuota 
    aux->next=*lista_principale;
    *lista_principale=aux;
    return 1;
}
int menu(){
	int scelta;
	printf("_____________________MENU'_____________________\n");
	printf("1)Stampa di tutti file presenti all'interno del path inserito.\n");
    printf("2)\n");
    printf("3)\n");
    printf("Scelta:\n");
    scanf("%d",&scelta);
    switch(scelta){
        case 1:
              ls(listafile);break;
        default: perror("Valore della scelta non corretto.\n");
                 exit(EXIT_FAILURE);
                 break;
    }
}
