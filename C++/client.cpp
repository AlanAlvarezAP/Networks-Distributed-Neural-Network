/* Client code in C */ 
#include "DataStructure_UDP.hpp"

Client_Protocols_UDP clp_UDP;

void print_menu() {
    std::cout << "===================================" << std::endl;
    std::cout << "|          Welcome to             |" << std::endl;
    std::cout << "|        Chat Simulation          |" << std::endl;
    std::cout << "|                                 |" << std::endl;
    std::cout << "|  1. Login                       |" << std::endl;
    std::cout << "|  2. Logout                      |" << std::endl;
    std::cout << "|  3. Broadcast                   |" << std::endl;
    std::cout << "|  4. Unicast                     |" << std::endl;
    std::cout << "|  5. Receive all clients         |" << std::endl;
    std::cout << "|  6. Send file                   |" << std::endl;
    std::cout << "===================================" << std::endl;
}

char Cast_Option(int option){
	switch(option){
        case 1: {
            return 'L';
        }
		case 2:{
			return 'O';
		}
		case 3:{
			return 'B';
		}
		case 4:{
			return 'U';
		}
        case 5:{
            return 'T';
        }
        case 6:{
            return 'F';
        }
		default:{
			return 'z';
		}
	}
}

void read_thread_UDP(int SocketFD){
    char buffer[500];
    sockaddr_in sender;
    socklen_t len=sizeof(sender);
    while(clp_UDP.running){
        int n=recvfrom(SocketFD,buffer,sizeof(buffer),0,(sockaddr*)&sender,&len);

        if(n<=0){
            continue;
        }

		std::string datagram(buffer,n);
		std::string senderKey = GetSenderKey(sender);

        int order      = std::atoi(datagram.substr(1, 2).c_str());
        int seq_number = std::atoi(datagram.substr(3, 4).c_str());

        char action;
        if(order == 1 || (order == 11 && seq_number == 0)){
            action = datagram[7];
        } else {
            auto it = clp_UDP.pending_transfers.find(senderKey);
            if(it == clp_UDP.pending_transfers.end()){
                std::cout << "ERROR no state for " << senderKey << std::endl;
                continue;
            }
            action = it->second.action;
        }

        clp_UDP.Cases_Client_UDP(action, datagram, SocketFD, sender);
		
    }
}

int main(void){

    struct sockaddr_in stSockAddr;
    int SocketFD;
    SocketFD = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    int n;

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

	clp_UDP.running = true;
	std::thread(read_thread_UDP,SocketFD).detach();
    
    print_menu();

    while(clp_UDP.running) {
        std::cout << "SELECT AN ACTION :D " << std::endl;
        int action;
        std::cin >> action;
	    std::cin.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
        char option = Cast_Option(action);
        if (option != 'L' && clp_UDP.logging_status == false) {
            std::cout << "You are not logged in, try logging pls :D" << std::endl;
            print_menu();
            continue;
        }

        clp_UDP.Cases_Client_UDP(option,std::string{option},SocketFD,stSockAddr);
        
    }
    std::cout << " LEAVING ... " << std::endl;
	clp_UDP.running = false;
	close(SocketFD);
    return 0;
}
