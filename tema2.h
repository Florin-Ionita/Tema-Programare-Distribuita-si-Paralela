#ifndef TEMA2_H
#define TEMA2_H


#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define MAX_CLIENTS 100

typedef struct {
    char filename[MAX_FILENAME];
    int num_hashes;
    // vector de marime max_chunks de hash-uri
    char hashes[MAX_CHUNKS][HASH_SIZE + 1]; // +1 pentru ca nu am nicio idee C ul ma sperie
} FileData;

typedef struct {
    int num_owned_files;
    FileData owned_files[MAX_FILES];
    int num_desired_files;
    // vector de nume de fisiere dorite
    char desired_files[MAX_FILES][MAX_FILENAME];
} ClientData;

typedef struct {
    int num_files;
    FileData files[MAX_FILES];
    // Pentru ca fisierele sunt numerotate, putem folosi numarul fisierului ca index
    // Adica seeds[0] inseamna ca client1 este seed pentru fisierul 1, peers[0] este pentru fisierul 1
    char seeds[MAX_FILES][MAX_CLIENTS];
    char peers[MAX_FILES][MAX_CLIENTS];

} Swarm;

typedef struct {
    // Daca type e 0 atunci e un update cand clientul inca downloadeaza, 
    // daca e 1 atunci e un update cand clientul a terminat de downloadat
    int type;
    // Daca a terminat de downloadat, trackerul il pune in seeds si-l scoate din peers
    int client_index;
    int file_index;

} Update;

typedef struct { 
    int rank;
    int numtasks;
    Swarm swarm;
    int num_desired_files;
    char desired_files[MAX_FILES][MAX_FILENAME]; 
} DownloadArgs;

MPI_Datatype file_data_type, swarm_data_type, update_data_type;
ClientData client_data;

void create_update_struct_MPI() {
    MPI_Datatype oldtypes[3];
    MPI_Aint offsets[3];
    int blockcounts[3];

    // campul type
    offsets[0] = offsetof(Update, type);
    oldtypes[0] = MPI_INT;
    blockcounts[0] = 1;

    // campul client_index
    offsets[1] = offsetof(Update, client_index);
    oldtypes[1] = MPI_INT;
    blockcounts[1] = 1;

    // campul file_index
    offsets[2] = offsetof(Update, file_index);
    oldtypes[2] = MPI_INT;
    blockcounts[2] = 1;

    MPI_Type_create_struct(3, blockcounts, offsets, oldtypes, &update_data_type);
    MPI_Type_commit(&update_data_type);
}

void create_file_struct_MPI() {
    MPI_Datatype oldtypes[3];
    MPI_Aint offsets[3];
    int blockcounts[3];

    // campul filename
    offsets[0] = offsetof(FileData, filename);
    oldtypes[0] = MPI_CHAR;
    blockcounts[0] = MAX_FILENAME;

    // campul num_hashes
    offsets[1] = offsetof(FileData, num_hashes);
    oldtypes[1] = MPI_INT;
    blockcounts[1] = 1;

    // campul hashes
    offsets[2] = offsetof(FileData, hashes);
    oldtypes[2] = MPI_CHAR;
    blockcounts[2] = MAX_CHUNKS * (HASH_SIZE + 1);

    MPI_Type_create_struct(3, blockcounts, offsets, oldtypes, &file_data_type);
    MPI_Type_commit(&file_data_type);
}

void create_swarm_struct_MPI() {
    MPI_Datatype oldtypes[4];
    MPI_Aint offsets[4];
    int blockcounts[4];

    // campul num_files
    offsets[0] = offsetof(Swarm, num_files);
    oldtypes[0] = MPI_INT;
    blockcounts[0] = 1;

    // campul files
    offsets[1] = offsetof(Swarm, files);
    oldtypes[1] = file_data_type;
    blockcounts[1] = MAX_FILES;

    // campul seeds
    offsets[2] = offsetof(Swarm, seeds);
    oldtypes[2] = MPI_CHAR;
    blockcounts[2] = MAX_FILES * MAX_CLIENTS;

    // campul peers
    offsets[3] = offsetof(Swarm, peers);
    oldtypes[3] = MPI_CHAR;
    blockcounts[3] = MAX_FILES * MAX_CLIENTS;

    MPI_Type_create_struct(4, blockcounts, offsets, oldtypes, &swarm_data_type);
    MPI_Type_commit(&swarm_data_type);
}

bool has_downloaded_file(bool found[], int num_hashes) {
    for (int i = 0; i < num_hashes; i++) {
        if (!found[i]) {
            return false;
        }
    }
    return true;
}

// Functie de citit fisierele de intrare
void read_file(int rank, ClientData *client_data) {
    char filename[MAX_FILENAME];
    sprintf(filename, "in%d.txt", rank);
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("Eroare la deschiderea fisierului %s\n", filename);
        exit(-1);
    }
    char line[256];

    if (fgets(line, sizeof(line), file) == NULL) {
        printf("Eroare la numarul de fisiere\n");
        exit(-1);
    }
    sscanf(line, "%d", &client_data->num_owned_files);

    for (int i = 0; i < client_data->num_owned_files; i++) {
        // Citesc numele fisierului si cate hash-uri are
        if (fgets(line, sizeof(line), file) == NULL) {
            printf("Eroare la citirea numelor fisierelor %d\n", i);
            exit(-1);
        }
        sscanf(line, "%s %d", 
            client_data->owned_files[i].filename, 
            &client_data->owned_files[i].num_hashes
        );
      
        // Citesc toate hash-urile
        char hash[256];
        for (int j = 0; j < client_data->owned_files[i].num_hashes; j++) {
            if (fgets(hash, 256, file) == NULL) {
                printf("Eroare la citirea hash-urilor %d\n", j);
                exit(-1);
            }
            hash[HASH_SIZE] = '\0';
            strncpy(client_data->owned_files[i].hashes[j], hash, HASH_SIZE);
            
        }
    }
    if (fgets(line, sizeof(line), file) == NULL) {
        printf("Eroare la citirea numarului de fisiere dorite\n");
        exit(-1);
    }
    sscanf(line, "%d", &client_data->num_desired_files);

    for (int i = 0; i < client_data->num_desired_files; i++) {
        if (fgets(line, sizeof(line), file) == NULL) {
            printf("Eroare la citirea numelor fisierelor dorite %d\n", i);
            exit(-1);
        }
        line[strcspn(line, "\n")] = '\0';

        strncpy(client_data->desired_files[i], line, MAX_FILENAME);

    }
    
    fclose(file);

}

// Functie pentru a nu mai avea cod duplicat in download_thread_func
void handle_peers_or_seeds(int rank, int file_num,
                    int hash_idx, bool *found, Swarm* swarm,
                    bool is_seed, int numtasks, int *should_update) {
    int client_index = rand() % numtasks;

    if (client_index == 0 || client_index == rank) {
        client_index = (client_index + 1) % numtasks;
    }

    for (int j = 0; j < numtasks; j++) {
        // If-ul acesta verifica daca apelam functia pentru seeds sau peers
        if ((is_seed && swarm->seeds[file_num][client_index] == 1) ||
            (!is_seed && swarm->peers[file_num][client_index] == 1)) {

            // Gatesc hash-ul pentru a fi trimis
            char hash[HASH_SIZE + 1];
            strncpy(hash, swarm->files[file_num].hashes[hash_idx], HASH_SIZE);
            hash[HASH_SIZE] = '\0';

            // Trimit cererea de download
            MPI_Send(&hash, HASH_SIZE + 1, MPI_CHAR, client_index, 0, MPI_COMM_WORLD);

            // Astept ACK
            int ACK = 0;
            MPI_Recv(&ACK, 1, MPI_INT, client_index, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (ACK == 1) {
                // printf("Am primit hash-ul de la minunatul %s\n", is_seed ? "seed" : "peer");
                found[hash_idx] = true;

                // Updatez client data
                client_data.owned_files[client_data.num_owned_files - 1].num_hashes++;
                strncpy(client_data.owned_files[client_data.num_owned_files - 1].hashes[hash_idx], hash, HASH_SIZE + 1);

                (*should_update)++;
                if (*should_update == 10) {
                    *should_update = 0;
                    // Gatirea unui mesaj de update de tipul 0
                    create_update_struct_MPI();
                    Update update;
                    update.type = 0;
                    update.client_index = rank;
                    update.file_index = file_num;
                    MPI_Send(&update, 1, update_data_type, TRACKER_RANK, 2, MPI_COMM_WORLD);
                    create_swarm_struct_MPI();
                    Swarm aux = *swarm;
                    MPI_Recv(&aux, 1, swarm_data_type, TRACKER_RANK, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    *swarm = aux;

                }

            }
        }

        client_index = (client_index + 1) % numtasks;
        if (client_index == 0 || client_index == rank) {
            client_index = (client_index + 1) % numtasks;
        }
    }
}


#endif