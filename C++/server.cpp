/* Server code in C */
#include "DataStructure_UDP.hpp"
Server_Protocols_UDP sv_UDP;



void print_server_menu(){
    std::cout << "===================================" << std::endl;
    std::cout << "|          Welcome to             |" << std::endl;
    std::cout << "|      Matrix NN distribution     |" << std::endl;
    std::cout << "|                                 |" << std::endl;
    std::cout << "|  1. Load Matrix                 |" << std::endl;
    std::cout << "|  2. Show Clients                |" << std::endl;
    std::cout << "|  3. Exit                        |" << std::endl;
    std::cout << "===================================" << std::endl;
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

    std::thread(read_thread_UDP,ServerFD).detach();
 
    while(true){
        int op;
        print_server_menu();
        std::cin >> op;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(),'\n');

        switch(op){
            case 1:{
                if(sv_UDP.client_map.size() < 3){
                    std::cout<< "Need more clients !!! " << std::endl;
                    break;
                }
                sv_UDP.Raw_Matrix_file(ServerFD);
                break;
            }

            case 2:{
                print(sv_UDP.client_map);
                break;
            }

            case 3:{
                close(ServerFD);
                return 0;
            }

            default:{
                std::cout<< "Invalid option" << std::endl;
            }
        }
    }


    close(ServerFD);
    return 0;
}
