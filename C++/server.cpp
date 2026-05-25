/* Server code in C */
#include "DataStructure.hpp"
#include "DataStructure_UDP.hpp"

Server_Protocols sv;
Server_Protocols_UDP sv_UDP;

enum TypeConnection{
    TCP=1,
    UDP
};

TypeConnection connection_type;


void read_thread_TCP(int n,int SocketFD){
    char buffer;
    for(;;){
        n = read(SocketFD, &buffer, 1);
        if (n == 0) {
            std::cout << "Client disconnected correctly" << std::endl;
            sv.Cases_Server('O', n, SocketFD);
            close(SocketFD);
            break;
        }
        if (n < 0) {
            std::cout << "Error reading from client" << std::endl;
            sv.Cases_Server('O', n, SocketFD);
            close(SocketFD);
            break;
        }
        sv.Cases_Server(buffer, n, SocketFD);
    }
    
}

void read_thread_UDP(int SocketFD){
    char buffer[500];
    sockaddr_in sender;
    socklen_t len = sizeof(sender);

    while(true){
        int n = recvfrom(SocketFD,buffer,sizeof(buffer),0,(sockaddr*)&sender,&len);

        if(n <= 0){
            continue;
        }

        std::string datagram(buffer,n);
        if(datagram.size() > 8 && datagram[8]=='F'){
            sv_UDP.Cases_Server(datagram[8],datagram, SocketFD, sender);
        }else{
            sv_UDP.Cases_Server(datagram[0],datagram, SocketFD, sender);
        }
        
    }
}


int main(void){
    int option;

    std::cout << "What type of connection you want? " << std::endl;
    std::cout << "1.TCP" << std::endl;
    std::cout << "2.UDP" << std::endl;
    std::cin >> option;

    connection_type=(option==1)?TCP:UDP;


    struct sockaddr_in stSockAddr;
    int ServerFD;
    
    if(connection_type == TCP){
        ServerFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    }else{
        ServerFD = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }
    
    int n;
 
    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
 
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;
 
    bind(ServerFD,(const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in));
 
    
    int ClientFD;
    if(connection_type == TCP){
        listen(ServerFD, 10);

        for(;;){
            ClientFD = accept(ServerFD,NULL,NULL);
            std::thread(read_thread_TCP,0,ClientFD).detach();
        }
    }else{
        read_thread_UDP(ServerFD);
    }
 
    shutdown(ClientFD, SHUT_RDWR);
 
    close(ClientFD);
 
    close(ServerFD);
    return 0;
}
