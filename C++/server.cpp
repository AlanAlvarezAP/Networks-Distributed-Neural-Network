/* Server code in C */
#include "DataStructure_UDP.hpp"
Server_Protocols_UDP sv_UDP;

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
        std::string senderKey = GetSenderKey(sender);

        int order      = std::atoi(datagram.substr(1, 2).c_str());
        int seq_number = std::atoi(datagram.substr(3, 4).c_str());

        char action=datagram[14];

        sv_UDP.Cases_Server(action, datagram, SocketFD, sender);
        
    }
}


int main(void){
    struct sockaddr_in stSockAddr;
    int ServerFD;
    
    ServerFD = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int n;
 
    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
 
    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    stSockAddr.sin_addr.s_addr = INADDR_ANY;
 
    bind(ServerFD,(const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in));
    read_thread_UDP(ServerFD);
 
    close(ServerFD);
    return 0;
}
