#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#define SHM_FILE "/tmp/ipc_shm.dat"
#define SIZE 1024

int main() {

    int fd = open(SHM_FILE, O_CREAT | O_RDWR, 0666);
    ftruncate(fd, SIZE);

    char *ptr = mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    printf("Shared Memory Server Started\n");

    while (1) {

        if (strlen(ptr) > 0) {
            printf("Received: %s\n", ptr);

            strcpy(ptr, "{\"status\":\"OK\",\"result\":\"SHM processed\"}");
        }

        sleep(1);
    }

    return 0;
}
