/* Client code in C */ 
#include "DataStructure.hpp"
#include "DataStructure_UDP.hpp"

Client_Protocols clp_TCP;
Client_Protocols_UDP clp_UDP;

enum TypeConnection{
    TCP=1,
    UDP
};

TypeConnection connection_type;

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

void read_thread_TCP(int n,int SocketFD){
    char buffer;
    while (clp_TCP.running) {
        n = read(SocketFD, &buffer, 1);
        if(n == 0){
            std::cout << "Server closed connection because of logout :D " << std::endl;
            close(SocketFD);
	        clp_TCP.running=false;
	        clp_TCP.logging_status=false;
            break;
        }
        else if (n < 0) {
            std::cout << "Disconection from server because of an ERROR..." << std::endl;
            close(SocketFD);
	        clp_TCP.running=false;
	        clp_TCP.logging_status=false;
            break;
        }
        clp_TCP.Cases_Client(buffer, n, SocketFD);
    }
     
}

void read_thread_UDP(int SocketFD){
    char buffer[500];
    sockaddr_in sender;
    socklen_t len=sizeof(sender);
    while(true){
        int n=recvfrom(SocketFD,buffer,sizeof(buffer),0,(sockaddr*)&sender,&len);

        if(n<=0){
            continue;
        }

		std::string datagram(buffer,n);
		if(datagram.size() > 8 && datagram[8]=='F'){
			clp_UDP.Cases_Client_UDP('f',datagram,SocketFD,sender);
		}else{
			clp_UDP.Cases_Client_UDP(datagram[0],datagram,SocketFD,sender);
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
    int SocketFD;
    if(connection_type == TCP){
        SocketFD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    }else{
        SocketFD = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }
    
    int n;

    memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

    stSockAddr.sin_family = AF_INET;
    stSockAddr.sin_port = htons(45000);
    inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);
 
    if(connection_type == TCP){
        connect(SocketFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in));
		clp_TCP.running = true;
        std::thread(read_thread_TCP,n,SocketFD).detach();    
    }else{
		clp_UDP.running = true;
        std::thread(read_thread_UDP,SocketFD).detach();
    }
    

    clp_TCP.running = true;
    clp_UDP.running = true;
    print_menu();

    
    while((clp_TCP.running || clp_UDP.running)) {
        std::cout << "SELECT AN ACTION :D " << std::endl;
        int action;
        std::cin >> action;
	    std::cin.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
        char option = Cast_Option(action);
        if (option != 'L' && (clp_TCP.logging_status == false && clp_UDP.logging_status == false)) {
            std::cout << "You are not logged in, try logging pls :D" << std::endl;
            print_menu();
            continue;
        }

        if(connection_type == TCP){
            clp_TCP.Cases_Client(option,n,SocketFD);
        }else{
            clp_UDP.Cases_Client_UDP(option,std::string{option},SocketFD,stSockAddr);
        }
        
    }
    std::cout << " LEAVING ... " << std::endl;
    return 0;
}
