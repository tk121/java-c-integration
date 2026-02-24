# Java-C Integration Project 
 
This project demonstrates: 
- TCP Socket IPC 
- Shared Memory IPC 
- JNI Integration 
 
Build Java: mvn package 
Build C: make 
or
gcc -O2 -Wall -Wextra -pthread socket_server_epoll_worker.c -o server_full

 java -jar client.jar 192.168.100.2 5000 1 2
