#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h> 
#include <stdatomic.h>

#define PORT 8080
#define BACKLOG_SIZE 5
#define MAX_COMMAND_SIZE 1024
#define MAX_KEY_SIZE 256
#define MAX_VALUE_SIZE 512
#define MAX_CLIENTS 100
_Atomic int server_running = 1;

//structure pour les clés / valeurs 
struct KeyValue {
    char key[MAX_KEY_SIZE];
    char value[MAX_VALUE_SIZE];
};
//structure pour stocker les données client
struct ClientKeyValue {
    int client_socket;
    struct KeyValue keyValueStore[1000];
};
//intiliasitation de stableaux de données pour stocker les infos des clients et les verrous 
struct ClientKeyValue clientKeyValues[MAX_CLIENTS];
pthread_mutex_t clientLock[MAX_CLIENTS];

 //sauvegarde de données dans un fichier 
void sauvegarder_donnees() {

    FILE *file = fopen("donnees.txt", "w");
    if (file == NULL) {
        perror("Erreur lors de l'ouverture du fichier de sauvegarde");
        return;
    }

    for (int i = 0; i < MAX_CLIENTS; ++i) {

        pthread_mutex_lock(&clientLock[i]);
        for (int j = 0; j < 1000; ++j) {


            // vérifier si la première lettre de la clé n'est pas nulle
            if (clientKeyValues[i].keyValueStore[j].key[0] != '\0') {
                fprintf(file, "%d %s %s\n", i, clientKeyValues[i].keyValueStore[j].key, clientKeyValues[i].keyValueStore[j].value);

            }
        }
        pthread_mutex_unlock(&clientLock[i]);
    }

    fclose(file);
}
//charger les données 
void charger_donnees() {
    FILE *file = fopen("donnees.txt", "r");
    if (file == NULL) {
        perror("Aucun fichier de données trouvé");
        return;
    }

    char key[MAX_KEY_SIZE];
    char value[MAX_VALUE_SIZE];
    int client_index;

    while (fscanf(file, "%d %s %s\n", &client_index, key, value) == 3) {
        if (client_index >= 0 && client_index < MAX_CLIENTS) {
            // Recherche de la clé existante pour mettre à jour la valeur
            int key_found = 0;
            for (int j = 0; j < 1000; ++j) {
                if (strcmp(clientKeyValues[client_index].keyValueStore[j].key, key) == 0) {
                    // Clé trouvée, mettre à jour la valeur correspondante
                    strcpy(clientKeyValues[client_index].keyValueStore[j].value, value);
                    key_found = 1;
                    break;
                }
            }
            // Si la clé n'est pas trouvée, ajouter la nouvelle paire clé-valeur
            if (!key_found) {
                for (int j = 0; j < 1000; ++j) {
                    if (clientKeyValues[client_index].keyValueStore[j].key[0] == '\0') {
                        strcpy(clientKeyValues[client_index].keyValueStore[j].key, key);
                        strcpy(clientKeyValues[client_index].keyValueStore[j].value, value);
                        break;
                    }
                }
               
            }
        }
    }

    fclose(file);
}




//fonction gestion de clients et commandes 

void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    char buffer[MAX_COMMAND_SIZE];
    int bytes_received;

    int client_index = -1;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        //si un emplacement pour un nouveau client est disponible dans le tableau clientKeyValues on verouille cet emplacement 
        if (clientKeyValues[i].client_socket == 0) {
            pthread_mutex_lock(&clientLock[i]);
            clientKeyValues[i].client_socket = client_socket;
            client_index = i;
            break;
        }
    }

    if (client_index == -1) {
        close(client_socket);
        pthread_exit(NULL);
    }
    //gérer les différentes commandes reçues du client 
    while ( server_running &&(  bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        buffer[bytes_received] = '\0'; 

        char *command = strtok(buffer, "\n"); 

        while (command != NULL) {
            if (strlen(command) > 0) {
                int command_length = strlen(command);
                while (command[command_length - 1] == '\r' || command[command_length - 1] == '\n' || command[command_length - 1] == ' ') {
                    command[command_length - 1] = '\0';
                    command_length = strlen(command);
                }

                char *token = strtok(command, " ");
                char *cmd = token;
                int KeyValueStoreSize = sizeof(clientKeyValues[client_index].keyValueStore) / sizeof(clientKeyValues[client_index].keyValueStore[0]);
            //traitement des commandes
                if (strcmp(cmd, "PING") == 0) {

                    char response[] = " > PONG\n";
                    send(client_socket, response, strlen(response), 0);

                } else if (strcmp(cmd, "SET") == 0) {
                    char *key = strtok(NULL, " ");
                    char *value = strtok(NULL, "");

                    int key_found = 0;
                    for (int i = 0; i < KeyValueStoreSize; ++i) {
                        if (strcmp(clientKeyValues[client_index].keyValueStore[i].key, key) == 0) {
                            strcpy(clientKeyValues[client_index].keyValueStore[i].value, value);
                            key_found = 1;
                            break;
                        }
                    }

                    if (!key_found) {
                        for (int i = 0; i < KeyValueStoreSize; ++i) {
                            if (clientKeyValues[client_index].keyValueStore[i].key[0] == '\0') {
                                strcpy(clientKeyValues[client_index].keyValueStore[i].key, key);
                                strcpy(clientKeyValues[client_index].keyValueStore[i].value, value);
                                break;
                            }
                        }
                    }

                    pthread_mutex_unlock(&clientLock[client_index]);

                    char response[] = "OK\n";
                    send(client_socket, response, strlen(response), 0);

                   sauvegarder_donnees();
                } else if (strcmp(cmd, "GET") == 0) {
                    char *key = strtok(NULL, " ");

                    pthread_mutex_lock(&clientLock[client_index]);
                    char response[MAX_VALUE_SIZE];
                    response[0] = '\0';
                    int key_found = 0;
                    for (int i = 0; i < KeyValueStoreSize; ++i) {
                        if (strcmp(clientKeyValues[client_index].keyValueStore[i].key, key) == 0) {
                            strcpy(response, clientKeyValues[client_index].keyValueStore[i].value);
                            strcat(response, "  \n ");
                            key_found = 1;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&clientLock[client_index]);

                    if (!key_found) {
                        strcpy(response, "Clé non trouvée\n");
                    }

                    send(client_socket, response, strlen(response), 0);
                    
                } else if (strcmp(cmd, "DEL") == 0) {
                    char *key = strtok(NULL, " ");

                    pthread_mutex_lock(&clientLock[client_index]);
                    int key_found = 0;
                    for (int i = 0; i < KeyValueStoreSize; ++i) {
                        if (strcmp(clientKeyValues[client_index].keyValueStore[i].key, key) == 0) {
                            clientKeyValues[client_index].keyValueStore[i].key[0] = '\0';
                            clientKeyValues[client_index].keyValueStore[i].value[0] = '\0';
                            key_found = 1;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&clientLock[client_index]);

                    char response[MAX_VALUE_SIZE];
                    if (key_found) {
                        sprintf(response, "Clé '%s' supprimée\n", key);
                    } else {
                        sprintf(response, "Clé '%s' non trouvée\n", key);
                    }
                    send(client_socket, response, strlen(response), 0);
                    sauvegarder_donnees();
                }else if (strcmp(cmd, "KEYS") == 0) {
                        char response[MAX_VALUE_SIZE];
                        response[0] = '\0';
                        pthread_mutex_lock(&clientLock[client_index]);
                        for (int i = 0; i < KeyValueStoreSize; ++i) {
                            if (clientKeyValues[client_index].keyValueStore[i].key[0] != '\0') {
                                strcat(response, clientKeyValues[client_index].keyValueStore[i].key);
                                strcat(response, "\n");
                            }
                        }
                        pthread_mutex_unlock(&clientLock[client_index]);
                        send(client_socket, response, strlen(response), 0);
                        
                    } else if (strcmp(cmd, "EXISTS") == 0) {
                        char *key = strtok(NULL, " ");
                        int key_exists = 0;

                        pthread_mutex_lock(&clientLock[client_index]);
                        for (int i = 0; i < KeyValueStoreSize; ++i) {
                            if (strcmp(clientKeyValues[client_index].keyValueStore[i].key, key) == 0) {
                                key_exists = 1;
                                break;
                            }
                        }
                        pthread_mutex_unlock(&clientLock[client_index]);

                        char response[MAX_VALUE_SIZE];
                        if (key_exists) {
                            sprintf(response, "Clé '%s' existe\n", key);
                        } else {
                            sprintf(response, "Clé '%s' n'existe pas\n", key);
                        }
                        send(client_socket, response, strlen(response), 0);
                        
                    } else if (strcmp(cmd, "FLUSHALL") == 0) {
                        pthread_mutex_lock(&clientLock[client_index]);
                        memset(clientKeyValues[client_index].keyValueStore, 0, sizeof(clientKeyValues[client_index].keyValueStore));
                        pthread_mutex_unlock(&clientLock[client_index]);

                        char response[] = "Toutes les clés ont été supprimées\n";
                        send(client_socket, response, strlen(response), 0);
                        
                    }else if (strcmp(cmd, "QUIT")  == 0) {
                        
                        close(client_socket);
                    } else if (strcmp(cmd, "APPEND") == 0) {
                                    char *key = strtok(NULL, " ");
                                    char *value = strtok(NULL, "");

                                    pthread_mutex_lock(&clientLock[client_index]);
                                    int KeyValueStoreSize = sizeof(clientKeyValues[client_index].keyValueStore) / sizeof(clientKeyValues[client_index].keyValueStore[0]);

                                    // Recherche de la clé dans le keyValueStore
                                    int key_found = 0;
                                    for (int i = 0; i < KeyValueStoreSize; ++i) {
                                        if (strcmp(clientKeyValues[client_index].keyValueStore[i].key, key) == 0) {
                                            key_found = 1;
                                            // Concaténation de la nouvelle valeur à la valeur existante (même pour les entiers)
                                            strcat(clientKeyValues[client_index].keyValueStore[i].value, value);
                                            break;
                                        }
                                    }

                                    pthread_mutex_unlock(&clientLock[client_index]);
                                    // Envoyer la réponse appropriée au client
                                    if (key_found) {
                                        char response[MAX_VALUE_SIZE];
                                        sprintf(response, "Valeur de la clé '%s' mise à jour\n", key);
                                        send(client_socket, response, strlen(response), 0);
                                        sauvegarder_donnees();
                                    } else {
                                        char response[MAX_VALUE_SIZE];
                                        sprintf(response, "Clé '%s' non trouvée\n", key);
                                        send(client_socket, response, strlen(response), 0);
                                    }                       
                                }
                                }

                                command = strtok(NULL, "\n");
                            }
                        }

                        pthread_mutex_lock(&clientLock[client_index]);
                        clientKeyValues[client_index].client_socket = 0;
                        memset(clientKeyValues[client_index].keyValueStore, 0, sizeof(clientKeyValues[client_index].keyValueStore));
                        pthread_mutex_unlock(&clientLock[client_index]);
                        sauvegarder_donnees();
                        close(client_socket);
                         
                        pthread_exit(NULL);
}




//fonction démarrage  srver 
void start_server() {
     charger_donnees();
    // Initialisation du socket serveur et configuration
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Erreur lors de la création du socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

     // Boucle pour accepter les connexions des clients et les gérer dans des threads

    int bind_result = bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address));
    if (bind_result == -1) {
        perror("Erreur lors du bind");
        exit(EXIT_FAILURE);
    }

    int listen_result = listen(server_socket, BACKLOG_SIZE);
    if (listen_result == -1) {
        perror("Erreur lors de la mise en écoute");
        exit(EXIT_FAILURE);
    }

    printf("Serveur connecté. En attente de connexions...\n");

   

    //création de thread pour gerer les connex simultanées des clients 
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        pthread_mutex_init(&clientLock[i], NULL);
    }
   
    while (server_running) {
        // Accepter une nouvelle connexion client

        struct sockaddr_in client_address;
        socklen_t client_address_size = sizeof(client_address);
        
        int *client_socket = malloc(sizeof(int));
        *client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_size);

        printf("Nouvelle connexion client acceptée.\n");

        if (*client_socket == -1) {
            perror("Erreur lors de l'acceptation de la connexion du client");
            exit(EXIT_FAILURE);
        }

        pthread_t tid;
        //creer un thread pour un client 
        pthread_create(&tid, NULL, handle_client, client_socket);
        pthread_detach(tid);
    }
    sauvegarder_donnees();
    close(server_socket);
    
}
void stop_server() {
    server_running = 0; // Arrête le serveur
}
int main() {
   sauvegarder_donnees();
    charger_donnees();
    start_server();
    stop_server();
    return 0;
}
