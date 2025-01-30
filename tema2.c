#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "tema2.h"

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define MAX_CLIENTS 100



void *download_thread_func(void *arg)
{
    DownloadArgs *args = (DownloadArgs *) arg;
    int rank = args->rank; 
    int numtasks = args->numtasks;
    Swarm swarm = args->swarm;
    srand(time(NULL));
    int should_update = 0;

    for (int i = 0; i < args->num_desired_files; i++) {
        
        // Indexul fisierului dorit
        int file_num;
        sscanf(args->desired_files[i], "file%d", &file_num);
        // printf("Hash-ul %d: %s\n", j, swarm.files[file_num].hashes[j]);
        bool found[swarm.files[file_num].num_hashes];
        // Incepe sa detina fisierul file_num
        client_data.num_owned_files++;

        for (int j = 0; j < swarm.files[file_num].num_hashes; j++) {
            found[j] = false;
        }

        int hash_idx = 0;


        while(!has_downloaded_file(found, swarm.files[file_num].num_hashes)) {
            if (should_update == 10) {
                // insert logica de trimis la tracker
                should_update = 0;
                swarm.peers[file_num][rank] = 1;
                create_update_struct_MPI();
                Update update;
                update.type = 0;
                update.client_index = rank;
                update.file_index = file_num;
                // Tag-ul 2 este rezervat pentru comunicare de swarm
                MPI_Send(&update, 1, update_data_type, TRACKER_RANK, 2 , MPI_COMM_WORLD);
                create_swarm_struct_MPI();
                // Primesc lista actualizata
                MPI_Recv(&swarm, 1, swarm_data_type, TRACKER_RANK, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
            
            // Daca deja are hash-ul merge mai departe
            if (found[hash_idx]) {
                hash_idx = (hash_idx + 1) % swarm.files[file_num].num_hashes;
                continue;
            }

            handle_peers_or_seeds(rank, file_num, hash_idx, found, &swarm, true, numtasks, &should_update); // Interact with seeds
            handle_peers_or_seeds(rank, file_num, hash_idx, found, &swarm, false, numtasks, &should_update); // Interact with peers
            
        }
        // Daca ajunge aici a downloadat tot fisierul deci hai sa facem fisierul de output
        char output_filename[MAX_FILENAME + 10];
        sprintf(output_filename, "client%d_%s", rank, args->desired_files[i]);
        FILE *output_file = fopen(output_filename, "w");
        if (output_file == NULL) { 
            printf("Eroare la crearea fisierului %s\n", output_filename); 
            continue; 
        } 
        for (int j = 0; j < swarm.files[file_num].num_hashes; j++) {
            fprintf(output_file, "%s\n", client_data.owned_files[client_data.num_owned_files - 1].hashes[j]);
        }
        fclose(output_file);
        
        // Inseamna ca l-a downloadat deci nu mai e peer e seed
        swarm.peers[file_num][rank] = 0;
        swarm.seeds[file_num][rank] = 1;
        create_update_struct_MPI();
        Update update;
        update.type = 1;
        update.client_index = rank;
        update.file_index = file_num;
        MPI_Send(&update, 1, update_data_type, TRACKER_RANK, 2, MPI_COMM_WORLD);
        create_swarm_struct_MPI();
        MPI_Recv(&swarm, 1, swarm_data_type, TRACKER_RANK, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
    }
    // S-au downloadat toate fileu-rile, anuntam trackerul
    create_update_struct_MPI();
    Update update;
    update.type = 2;
    update.client_index = 0;
    update.file_index = 0;
    MPI_Send(&update, 1, update_data_type, TRACKER_RANK, 2, MPI_COMM_WORLD);


    
    return NULL;
}

void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;
    while(true) {
        char hash[HASH_SIZE + 1];
        MPI_Status status;
        MPI_Recv(&hash, HASH_SIZE + 1, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
        // s-au downloadat toate fisierele
        if (strncmp(hash, "00000000000000000000000000000000", HASH_SIZE) == 0) {
            break;
        }
        hash[HASH_SIZE] = '\0';
        
        // Verifica in CLientData daca are hash-ul
        bool found = false;
        for (int i = 0; i < client_data.num_owned_files; i++) {
            for (int j = 0; j < client_data.owned_files[i].num_hashes; j++) {
                if (strncmp(client_data.owned_files[i].hashes[j], hash, HASH_SIZE + 1) == 0) {
                    // printf("Clientul %d are hash-ul %s!!!!!!\n", rank, hash);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            // printf("Clientul %d nu are hash-ul %s\n", rank, hash);
            int NACK = 0;
            MPI_Send(&NACK, 1, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
            continue;
        }
        // Trimite ACK
        // printf("sursa este %d\n", status.MPI_SOURCE);
        int ACK = 1;
        MPI_Send(&ACK, 1, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
    }
    return NULL;
}

void tracker(int numtasks, int rank) {

    MPI_Status status;
    Swarm swarm;
    FileData file_data;
    int num_files;
    // initializare
    swarm.num_files = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        swarm.files[i].num_hashes = 0;
        for (int j = 0; j < MAX_CLIENTS; j++) {
            swarm.seeds[i][j] = 0;
            swarm.peers[i][j] = 0;
        }
    }

    create_file_struct_MPI();
    for (int i = 1; i < numtasks; i++) {
        MPI_Recv(&num_files, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);
        for (int j = 0; j < num_files; j++) {
            MPI_Recv(&file_data, 1, file_data_type, i, 0, MPI_COMM_WORLD, &status);
            
            int file_num;
            // Extrag numarul fisierului din filename
            sscanf(file_data.filename, "file%d", &file_num);
            // Da informatia despre hash-uri
            if (swarm.files[file_num].num_hashes == 0) {
                swarm.num_files++;
                swarm.files[file_num] = file_data;
            }
            // Daca a primit fisierul, inseamna ca este seed pentru el
            swarm.seeds[file_num][status.MPI_SOURCE] = 1;
        }
    }
    create_swarm_struct_MPI();
   
    for (int i = 1; i < numtasks; i++) {
        MPI_Send(&swarm, 1, swarm_data_type, i, 0, MPI_COMM_WORLD);
    }
    int should_stop = 0;
    // Partea de actualizare
    while (true) {
        if (should_stop == numtasks - 1) {
            break;
        }
        Update update;
        create_update_struct_MPI();
        MPI_Recv(&update, 1, update_data_type, MPI_ANY_SOURCE, 2, MPI_COMM_WORLD, &status);
        
        
        if (update.type == 0) {
            // Adauga peer-ul in swarm
            swarm.peers[update.file_index][update.client_index] = 1;

            MPI_Send(&swarm, 1, swarm_data_type, status.MPI_SOURCE, 2, MPI_COMM_WORLD);
        } else if (update.type == 1) {
            // Adauga seed-ul in swarm si-l elimina din peers
            swarm.peers[update.file_index][update.client_index] = 0;
            swarm.seeds[update.file_index][update.client_index] = 1;
        
            MPI_Send(&swarm, 1, swarm_data_type, status.MPI_SOURCE, 2, MPI_COMM_WORLD);
        } else {
            // Aici inseaman ca primit mesaj de tipul 2, nu returneaza nimic
            should_stop++;
        }
    }
    // Trimite semnalul de stop catre thread-urile de upload
    for (int i = 1; i < numtasks; i++) {
        char hash[HASH_SIZE + 1];
        strncpy(hash, "00000000000000000000000000000000", HASH_SIZE);
        hash[HASH_SIZE] = '\0';
        MPI_Send(&hash, HASH_SIZE + 1, MPI_CHAR, i, 0, MPI_COMM_WORLD);

    }
}



void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    // Initializare

    read_file(rank, &client_data);

    create_file_struct_MPI();

    // Trimit datele catre tracker
    // Trimit mai intai numarul de fisiere
    MPI_Send(&client_data.num_owned_files, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

    for (int i = 0; i < client_data.num_owned_files; i++) {
        MPI_Send(&client_data.owned_files[i], 1, file_data_type, TRACKER_RANK, 0, MPI_COMM_WORLD);
    }

    // Primeste informatiile despre swarm
    Swarm swarm;

    create_swarm_struct_MPI();
    MPI_Recv(&swarm, 1, swarm_data_type, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    DownloadArgs download_args;
    download_args.rank = rank;
    download_args.swarm = swarm;
    download_args.num_desired_files = client_data.num_desired_files;
    download_args.numtasks = numtasks;

    for (int i = 0; i < client_data.num_desired_files; i++) {
        strncpy(download_args.desired_files[i], client_data.desired_files[i], MAX_FILENAME);
    }
    
    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &download_args);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
