#pragma once
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <limits>
#include <fstream>
#include <sstream>
#include "json.hpp"

#define DATAGRAM_SIZE 500
#define HEADER_SIZE 7

typedef nlohmann::json json;

std::string number_to_string_2(int number, int size) {
    std::string result(size, ' ');
    int count = size - 1;
    if (number < 0) {
        number = -number;
    }
    while (number > 0) {
        int division = number % 10;
        result[count--] = division + '0';
        number /= 10;
    }
    while (count >= 0) {
        result[count--] = '0';
    }
    return result;
}

char Calculate_Checksum(std::string content){
    int sum = 0;
    for(unsigned char c : content){
        sum += c;
	}

    return static_cast<char>(sum % 7);
}

std::string GetSenderKey(sockaddr_in& addr){
    return std::string(inet_ntoa(addr.sin_addr)) + ":" + std::to_string(ntohs(addr.sin_port));
}



struct SentFile{
    int total_fragments;
	long long file_size;
    std::vector<std::string> packets;
    std::vector<bool> acked;
};

struct ProtocolFormat{
	char hash; // 1 byte
	int order; // 2 byte
	int seq_number; // 4 byte
	char action; // 1 byte
	int nickname_size; // 3 byte
	std::string nickname; // Variable
	int nickname_dest_size; // 3 byte
	std::string nickname_dest; // Variable
	int msg_size; // 5 byte
	std::string msg; // Variable
	long long filename_size; // 11 byte
	std::string filename; // Variable
	long long filecontent_size; // 20 byte
	std::string filecontent; // Variable -> max 10kb

	std::string ConstructDatagram(){
		return std::string{hash}+number_to_string_2(order,2)+number_to_string_2(seq_number,4)+std::string{action}
		+number_to_string_2(nickname_size,3)+nickname+number_to_string_2(nickname_dest_size,3)+nickname_dest+number_to_string_2(msg_size,5)+msg
		+number_to_string_2(filename_size,11)+filename+number_to_string_2(filecontent_size,20)+filecontent;
	}
	char Calculate_Checksum_Fragments(std::string& packet){
		return Calculate_Checksum(packet.substr(7,packet.size()-7));
	}
};

struct ProtocolFormat_Normal{
	char hash; // 1 byte
	int order; // 2 byte
	int seq_number; // 4 byte
	std::string filecontent; // 493 byte maximum

	std::string ConstructDatagram(){
		return std::string{hash}+number_to_string_2(order,2)+number_to_string_2(seq_number,4)+filecontent;
	}
	char Calculate_Checksum_Fragments(std::string& packet){
		return Calculate_Checksum(packet.substr(7,packet.size()-7));
	}
};

struct TransferState {
    std::string destination;
	std::string file_name;
    std::string origin;
	long long total_size   = 0;
	int last_seq = -1;
    bool last_received = false;
	char action = 0;
    std::vector<std::pair<int,std::string>> fragments;
};

void Send_OK(int socket,sockaddr_in& addr){
    ProtocolFormat p{'0',11,0,'K',0,"",0,"",0,"",0,"",0,""};

    std::string packet = p.ConstructDatagram();

    while(packet.size() < DATAGRAM_SIZE){
        packet.push_back('#');
	}

    packet[0] = p.Calculate_Checksum_Fragments(packet);

    sendto(socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&addr,sizeof(addr));
}

void Send_Error(int socket,sockaddr_in& addr,const std::string& msg){
    ProtocolFormat p{'0',11,0,'E',0,"",0,"",(int)msg.size(),msg,0,"",0,""};

    std::string packet = p.ConstructDatagram();

    while(packet.size() < DATAGRAM_SIZE){
        packet.push_back('#');
	}

    packet[0] = p.Calculate_Checksum_Fragments(packet);

    sendto(socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&addr,sizeof(addr));
}

void print(const std::unordered_map<std::string,sockaddr_in>& clientes){
	std::cout << "================================" << std::endl;
	for(const auto& cliente : clientes){
	    std::cout << "ID: " << cliente.first << std::endl;
	}
	std::cout << "================================" << std::endl;
}

class Server_Protocols_UDP {
public:
	std::unordered_map<std::string, sockaddr_in> client_map;
	std::unordered_map<std::string, TransferState> pending_transfers;
public:

    std::string Login(const std::string& buffer, int server_socket, sockaddr_in& client_addr) {
		std::string senderKey = GetSenderKey(client_addr);
	   
	    int pos = 0;
		char hash=buffer[0];
	   	pos += 1;
	    int order =std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	    int seq_number =std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;

		char calculated = Calculate_Checksum(buffer.substr(7, DATAGRAM_SIZE - 7));
	    if(hash != calculated){
			std::string error_msg = "ERROR CHECKSUM";
			Send_Error(server_socket,client_addr,error_msg);
	        return "";
	    }
	   
	   	int size_origin,size_dest,size_msg;
	    long long size_file_name,size_content;
	   	char protocol_type;
	   	std::string origin,destination,file_name,content;
		if(order == 1 || (order == 11 && seq_number == 0)){
			protocol_type = buffer[pos++];
		    if(protocol_type != 'L'){
		        return "";
		    }
		
		    size_origin =std::atoi(buffer.substr(pos,3).c_str());
		    pos += 3;
		
		    origin=buffer.substr(pos,size_origin);
		    pos += size_origin;
			
			if (client_map.find(origin) != client_map.end()) {
				Send_Error(server_socket,client_addr,"ERROR nickname already in server");
				return origin;
	        }
		   	pos += 3;
			pos += 5;
		    pos += 11;
		
		    size_content=std::atoll(buffer.substr(pos,20).c_str());
		    pos += 20;
		
		    content=buffer.substr(pos,size_content);
		    pos += size_content;
			
			pending_transfers[senderKey].total_size= size_origin;
			pending_transfers[senderKey].action = 'L';
			pending_transfers[senderKey].origin=origin;
        	pending_transfers[senderKey].fragments.clear();
		}else {
	        content = buffer.substr(7, DATAGRAM_SIZE - 7);
	    }

	   if(pending_transfers.find(senderKey) == pending_transfers.end()){
		   	std::string error_msg ="ERROR: no transfer state for"+std::string{senderKey};
		   	Send_Error(server_socket,client_addr,error_msg);
	        return "";
	    }

		
		
	   	pending_transfers[senderKey].fragments.push_back({seq_number, content});
	   	std::cout << "===================================================================" << std::endl;
	   	std::cout << "Server received datagram # " << seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;
	   	auto& transfer = pending_transfers[senderKey];
		if (order == 11){
			transfer.last_seq = seq_number;
    		transfer.last_received = true;
		}
		std::string nickname;
	    if(transfer.last_received && (int)transfer.fragments.size() == transfer.last_seq + 1){
			std::sort(transfer.fragments.begin(), transfer.fragments.end(), [](const auto& a, const auto& b){return a.first < b.first;});
			
		    long long written = 0;
		    for(auto& pares : transfer.fragments){
		        long long remaining = transfer.total_size - written;
		        long long to_write  = std::min((long long)pares.second.size(), remaining);
		        written += to_write;
				nickname+=pares.second;
		    }
		    pending_transfers.erase(senderKey);
			client_map[nickname] = client_addr;

            Send_OK(server_socket, client_addr);
	        print(client_map);
		}
        
        return nickname;
    }

    void Broadcast(const std::string& buffer,int server_socket,sockaddr_in& client_addr){
	    std::string senderKey = GetSenderKey(client_addr);
	
	    int pos = 0;
	
	    char hash = buffer[0];
	    pos += 1;
	
	    int order = std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	
	    int seq_number = std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;
	
	    std::string copy = buffer;
	
	    char calculated =
	        Calculate_Checksum(buffer.substr(7,DATAGRAM_SIZE-7));
	
	    if(hash != calculated){
	        Send_Error(server_socket,client_addr,"ERROR CHECKSUM");
	        return;
	    }
	    std::string content;
	
	    if(order == 1 || (order == 11 && seq_number == 0))
	    {
	        char protocol_type = buffer[pos++];
	
	        if(protocol_type != 'B')
	            return;
	
	        copy[7] = 'b';
	
	        calculated =
	            Calculate_Checksum(copy.substr(7,DATAGRAM_SIZE-7));
	
	        copy[0] = calculated;
	
	        int size_origin = std::atoi(buffer.substr(pos,3).c_str());
	        pos += 3;
	
	        std::string origin =
	            buffer.substr(pos,size_origin);
	
	        pos += size_origin;
	
	        int size_dest = std::atoi(buffer.substr(pos,3).c_str());
	        pos += 3;
	
	        pos += size_dest;
	
	        int size_msg = std::atoi(buffer.substr(pos,5).c_str());
	        pos += 5;
	
	        pos += size_msg;
	
	        long long size_file =std::atoll(buffer.substr(pos,11).c_str());
	        pos += 11;
	
	        pos += size_file;
	
	        long long size_content =std::atoll(buffer.substr(pos,20).c_str());
	
	        pos += 20;
	
	        content=buffer.substr(pos,size_content);
	
	        pending_transfers[senderKey].action = 'B';
	        pending_transfers[senderKey].origin = origin;
	        pending_transfers[senderKey].fragments.clear();
	    }
	
	    if(pending_transfers.find(senderKey)== pending_transfers.end()){
	        Send_Error(server_socket,client_addr,"ERROR no transfer state");
	        return;
	    }
	
	    pending_transfers[senderKey].fragments.push_back({seq_number,copy});
		std::cout << "===================================================================" << std::endl;
	   	std::cout << "Server received datagram # " << seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;

		
	    auto& transfer=pending_transfers[senderKey];
	
	    if(order == 11){
	        transfer.last_seq = seq_number;
	        transfer.last_received = true;
	    }
	
	    if(transfer.last_received && (int)transfer.fragments.size()== transfer.last_seq + 1){
	        std::sort(transfer.fragments.begin(),transfer.fragments.end(),[](const auto& a,const auto& b){return a.first < b.first;});
	
	        for(const auto& client : client_map){
	            for(const auto& fragment :transfer.fragments){
	                sendto(server_socket,fragment.second.data(),DATAGRAM_SIZE,0,(sockaddr*)&client.second,sizeof(client.second));
	                std::this_thread::sleep_for(std::chrono::microseconds(100));
	            }
	        }
	
	        pending_transfers.erase(senderKey);
	    }
	}

    void Unicast(const std::string& buffer, int server_socket, sockaddr_in& client_addr) {
		std::string senderKey = GetSenderKey(client_addr);
	
	    int pos = 0;
	
	    char hash = buffer[0];
	    pos += 1;
	
	    int order = std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	
	    int seq_number = std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;
	
	    std::string copy = buffer;
	
	    char calculated =
	        Calculate_Checksum(buffer.substr(7,DATAGRAM_SIZE-7));
	
	    if(hash != calculated){
	        Send_Error(server_socket,client_addr,"ERROR CHECKSUM");
	        return;
	    }
	    std::string content;
	
	    if(order == 1 || (order == 11 && seq_number == 0)){
	        char protocol_type = buffer[pos++];
	
	        if(protocol_type != 'U'){
	            return;
			}
	
	        copy[7] = 'u';
	
	        calculated =Calculate_Checksum(copy.substr(7,DATAGRAM_SIZE-7));
	
	        copy[0] = calculated;
	
	        int size_origin = std::atoi(buffer.substr(pos,3).c_str());
	        pos += 3;
	
	        std::string origin =buffer.substr(pos,size_origin);
	        pos += size_origin;
	
	        int size_dest = std::atoi(buffer.substr(pos,3).c_str());
	        pos += 3;

			std::string destination =buffer.substr(pos,size_dest);
	        pos += size_dest;
	
	        int size_msg = std::atoi(buffer.substr(pos,5).c_str());
	        pos += 5;
	
	        pos += size_msg;
	
	        long long size_file =std::atoll(buffer.substr(pos,11).c_str());
	
	        pos += 11;
	
	        pos += size_file;
	
	        long long size_content =std::atoll(buffer.substr(pos,20).c_str());
	
	        pos += 20;
	
	        content=buffer.substr(pos,size_content);
	
	        pending_transfers[senderKey].action = 'U';
	        pending_transfers[senderKey].origin = origin;
			pending_transfers[senderKey].destination = destination;
	        pending_transfers[senderKey].fragments.clear();
	    }
	
	    if(pending_transfers.find(senderKey)== pending_transfers.end()){
	        Send_Error(server_socket,client_addr,"ERROR no transfer state");
	        return;
	    }
	
	    pending_transfers[senderKey].fragments.push_back({seq_number,copy});
		std::cout << "===================================================================" << std::endl;
	   	std::cout << "Server received datagram # " << seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;

		
	    auto& transfer=pending_transfers[senderKey];
	
	    if(order == 11){
	        transfer.last_seq = seq_number;
	        transfer.last_received = true;
	    }
	
	    if(transfer.last_received && (int)transfer.fragments.size()== transfer.last_seq + 1){
	        std::sort(transfer.fragments.begin(),transfer.fragments.end(),[](const auto& a,const auto& b){return a.first < b.first;});
			sockaddr_in dst =client_map[transfer.destination];
			for(const auto& fragment :transfer.fragments){
	                sendto(server_socket,fragment.second.data(),DATAGRAM_SIZE,0,(sockaddr*)&dst,sizeof(dst));
	                std::this_thread::sleep_for(std::chrono::microseconds(100));
	            }
	
	        pending_transfers.erase(senderKey);
	    }
    }

    void Send_List(int server_socket, sockaddr_in& client_addr) {
		json js;
		js["clients"] = json::array();
	
		for(const auto& pair : client_map){
			js["clients"].push_back(pair.first);
		}
	
		std::string json_msg = js.dump();
		int size_msg = json_msg.size();
	
		std::string requester_name;
		for(const auto& client : client_map){
			if(client.second.sin_addr.s_addr == client_addr.sin_addr.s_addr && client.second.sin_port == client_addr.sin_port){
				requester_name = client.first;
				break;
			}
		}
	
		int seq_numbers = 0;
		int header =7+1+3+requester_name.size()+3+requester_name.size()+5+size_msg+11+20;
	
		int remaining_size_first = DATAGRAM_SIZE - header;
		int current_size = std::min(remaining_size_first, size_msg);
	
		int total_remaining = size_msg - current_size;
		int max_content = DATAGRAM_SIZE - 7;
	
		int extra_fragments =(total_remaining + max_content - 1) / max_content;
	
		int total_fragments = 1 + extra_fragments;
	
		int first_order =(total_fragments == 1) ? 11 : 1;
	
		ProtocolFormat protocol{'0',first_order,seq_numbers++,'t',(int)requester_name.size(),requester_name,(int)requester_name.size(),requester_name,size_msg,json_msg,0,"",0,""};
	
		std::string packet = protocol.ConstructDatagram();
	
		while(packet.size() < DATAGRAM_SIZE){
			packet.push_back('#');
		}
	
		packet[0] = protocol.Calculate_Checksum_Fragments(packet);
		std::cout << "=======================================================" << std::endl;
		std::cout << "Server Sending List to -> " << protocol.nickname_dest << " with the datagram format of" << std::endl;
		std::cout << packet << std::endl;
		std::cout << "=======================================================" << std::endl;
		sendto(server_socket,packet.data(),DATAGRAM_SIZE, 0,(sockaddr*)&client_addr,sizeof(client_addr));
	
		int start = current_size;
		for(int i = 1; i < total_fragments; i++){
			int frag_size =std::min(max_content,size_msg - start);
			std::string fragment =json_msg.substr(start, frag_size);
	
			int frag_order =(i == total_fragments - 1) ? 11 : 0;
			ProtocolFormat_Normal protocol_normal{'0',frag_order,seq_numbers++,fragment};
	
			std::string packet2 =protocol_normal.ConstructDatagram();
	
			while(packet2.size() < DATAGRAM_SIZE){
				packet2.push_back('#');
			}
	
			packet2[0] =protocol_normal.Calculate_Checksum_Fragments(packet2);
			std::cout << "=======================================================" << std::endl;
			std::cout << "Server Sending List fragment # " << i << " to -> with the datagram format of" << std::endl;
			std::cout << packet2 << std::endl;
			std::cout << "=======================================================" << std::endl;
			sendto(server_socket,packet2.data(),DATAGRAM_SIZE,0,(sockaddr*)&client_addr,sizeof(client_addr));
	
			start += frag_size;
		}
		
    }

   void File_redirect(const std::string& buffer,int server_socket,sockaddr_in& client_addr){
		std::string senderKey = GetSenderKey(client_addr);
	   
	    int pos = 0;
		char hash=buffer[0];
	   	pos += 1;
	    int order =std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	    int seq_number =std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;

	   	std::string copy=buffer;
		char calculated = Calculate_Checksum(buffer.substr(7, DATAGRAM_SIZE - 7));
		/*std::cout << "RECEIVE HASH = " << (int)hash << std::endl;
		std::cout << "CALCULATED HASH = " << (int)calculated << std::endl;*/
	    if(hash != calculated){
	        std::string error_msg = "ERROR CHECKSUM";
			Send_Error(server_socket,client_addr,error_msg);
	        return;
	    }

	   
	   	int size_origin,size_dest,size_msg;
	    long long size_file_name,size_content;
	   	char protocol_type;
	   	std::string origin,destination,file_name,content;
		if(order == 1 || (order == 11 && seq_number == 0)){
			protocol_type = buffer[pos++];
		    if(protocol_type != 'F'){
				std::cout << "Omitting..." << std::endl;
		        return;
		    }
			copy[7]='f';
			calculated = Calculate_Checksum(copy.substr(7, DATAGRAM_SIZE - 7));
		   	copy[0]=calculated;
		    size_origin =std::atoi(buffer.substr(pos,3).c_str());
		    pos += 3;
		
		    origin=buffer.substr(pos,size_origin);
		    pos += size_origin;
	
			size_dest=std::atoi(buffer.substr(pos,3).c_str());
		   	pos += 3;
	
		   	destination=buffer.substr(pos,size_dest);
		   	pos += size_dest;

			if(client_map.find(destination) == client_map.end()){
		        std::string error_msg ="ERROR destination not in the server for file";
		        Send_Error(server_socket,client_addr,error_msg);
		        return;
		    }
			
		   	size_msg = std::atoi(buffer.substr(pos,5).c_str());
			pos += 5;
	
		   	// NO MSG
		   
		    size_file_name =std::atoll(buffer.substr(pos,11).c_str());
		    pos += 11;
		
		    file_name =buffer.substr(pos,size_file_name);
		    pos += size_file_name;
		
		    size_content=std::atoll(buffer.substr(pos,20).c_str());
		    pos += 20;
		
		    content=buffer.substr(pos,size_content);
		    pos += size_content;

			pending_transfers[senderKey].destination = destination;
			pending_transfers[senderKey].file_name = file_name;
			pending_transfers[senderKey].action = 'F';
			pending_transfers[senderKey].origin=origin;
        	pending_transfers[senderKey].fragments.clear();
			
		}

	   if(pending_transfers.find(senderKey) == pending_transfers.end()){
		   	std::string error_msg ="ERROR: no transfer state for"+std::string{senderKey};
	        Send_Error(server_socket,client_addr,error_msg);
	        return;
	    }

	   	pending_transfers[senderKey].fragments.push_back({seq_number, copy});
	   	std::cout << "===================================================================" << std::endl;
	   	std::cout << "Server received datagram # " << seq_number << " with the content" << copy << std::endl;
	   	std::cout << "===================================================================" << std::endl;
	   auto& transfer = pending_transfers[senderKey];
	   	if (order == 11){
			transfer.last_seq = seq_number;
    		transfer.last_received = true;
		}
	    if(transfer.last_received && (int)transfer.fragments.size() == transfer.last_seq + 1){
			std::sort(transfer.fragments.begin(), transfer.fragments.end(), [](const auto& a, const auto& b){return a.first < b.first;});
		    sockaddr_in dst = client_map[transfer.destination];
		    for(auto& pares : transfer.fragments){
		        sendto(server_socket, pares.second.data(), DATAGRAM_SIZE, 0, (sockaddr*)&dst, sizeof(dst));
				std::this_thread::sleep_for(std::chrono::microseconds(100));
		    }
		    pending_transfers.erase(senderKey);
		}

	}

    void Logout(const std::string& buffer,int server_socket,sockaddr_in& client_addr){
	    int pos = 0;
	    char hash = buffer[0];
	    pos += 1;
	    int order = std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	    int seq_number = std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;
	    char calculated =Calculate_Checksum(buffer.substr(7, DATAGRAM_SIZE - 7));
	
	    if(hash != calculated){
	        Send_Error(server_socket,client_addr,"ERROR CHECKSUM");
	        return;
	    }
	
	    if(!(order == 11 && seq_number == 0)){
	        Send_Error(server_socket,client_addr,"ERROR INVALID LOGOUT FORMAT");
	        return;
	    }
	
	    char protocol_type = buffer[pos++];
	    if(protocol_type != 'O'){
	        Send_Error(server_socket,client_addr,"ERROR INVALID LOGOUT TYPE");
	        return;
	    }
	    std::string nickname;
	    int nickname_size =std::atoi(buffer.substr(pos,3).c_str());
	    pos += 3;
	
	    nickname =buffer.substr(pos,nickname_size);
	    pos += nickname_size;
	    bool found = false;
	
	    for(auto it = client_map.begin();it != client_map.end();++it){
	        if(it->second.sin_addr.s_addr ==client_addr.sin_addr.s_addr &&it->second.sin_port ==client_addr.sin_port){
	            found = true;
	            std::cout<< "User disconnected -> "<< it->first<< std::endl;
	            client_map.erase(it);
	            break;
	        }
	    }
	
	    if(!found){
	        Send_Error(server_socket,client_addr,"ERROR USER NOT LOGGED");
	        return;
	    }
	    Send_OK(server_socket, client_addr);
	    print(client_map);
	}

    void Cases_Server(char type,const std::string& buffer, int server_socket, sockaddr_in& client_addr) {
        switch (type) {
            case 'L': {
                Login(buffer, server_socket, client_addr);
                break;
            }
            case 'O': {
                Logout(buffer,server_socket, client_addr);
                break;
            }
            case 'B': {
                Broadcast(buffer, server_socket, client_addr);
                break;
            }
            case 'U': {
                Unicast(buffer, server_socket, client_addr);
                break;
            }
            case 'T': {
                Send_List(server_socket, client_addr);
                break;
            }
            case 'F': {
                File_redirect(buffer, server_socket, client_addr);
                break;
            }
            default: {
                std::cout << "This protocol is not registered in Server :( " << std::endl;
                break;
            }
        }
    }
};

class Client_Protocols_UDP {
public:
    bool logging_status = false, running = false,waiting_ACK=false;
	std::string final_name,pending_name;
	std::unordered_map<std::string, TransferState> pending_transfers;
public:
    void Error(const std::string& buffer) {
        int pos = 8;
	    int nickname_size =std::atoi(buffer.substr(pos,3).c_str());
	    pos += 3;
	
	    pos += nickname_size;
	
	    int destination_size = std::atoi(buffer.substr(pos,3).c_str());
	    pos += 3;
	    pos += destination_size;
	
	    int msg_size =std::atoi(buffer.substr(pos,5).c_str());
	    pos += 5;
	
	    std::string error_msg =buffer.substr(pos,msg_size);
	
	    std::cout<< "ERROR -> "<< error_msg<< std::endl;
    }

    void Login(int client_socket, sockaddr_in& server_addr) {
		std::string name;
        std::cout << "Give me your nickname to send -> ";
        std::getline(std::cin, name);
		pending_name=name;
        int size_msg = name.size();
		
		int seq_numbers{0};
	
	    // First fragment
		int header=7+1+3+final_name.size()+3+0+5+0+11+0+20+0;
		int remaining_size_first=DATAGRAM_SIZE-header;
		int current_size =std::min(remaining_size_first,(int)pending_name.size());

	    int total_remaining = (int)pending_name.size() - current_size;
	    int max_content = DATAGRAM_SIZE - 7;
	    int extra_fragments = (total_remaining + max_content - 1) / max_content;
	    int total_fragments = 1 + extra_fragments;
	
	    int first_order = (total_fragments == 1) ? 11 : 1;
		
		ProtocolFormat protocol{'0',first_order,seq_numbers++,'L',(int)pending_name.size(),pending_name,0,"",0,"",0,"",(long long)pending_name.size(),pending_name.substr(0,current_size)};
		
		std::string packet=protocol.ConstructDatagram();
		
		while(packet.size() < DATAGRAM_SIZE){
			packet.push_back('#');
		}
		packet[0]=protocol.hash=protocol.Calculate_Checksum_Fragments(packet);

		SentFile sf;
		sf.total_fragments = total_fragments;
		sf.file_size = pending_name.size();
		sf.packets.resize(total_fragments);
		sf.acked.resize(total_fragments,false);

		std::cout << "=======================================================" << std::endl;
		std::cout << "Client Sending from -> " << protocol.nickname << " to " << protocol.nickname_dest << " with the datagram format of" << std::endl;
		std::cout << packet << std::endl;
		std::cout << "=======================================================" << std::endl;
		
		sf.packets[0] = packet;
		sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

		int start = current_size;
	    for(int i=1;i<total_fragments;i++){
	        int frag_size =std::min(max_content,(int)pending_name.size()-start);
	        std::string fragment =pending_name.substr(start,frag_size);

			int frag_order = (i == total_fragments - 1) ? 11 : 0;
			ProtocolFormat_Normal protocol_normal{'0',frag_order,seq_numbers++,fragment};
			
			std::string packet_2=protocol_normal.ConstructDatagram();
			while((int)packet_2.size() < 500){
				packet_2.push_back('#');
			}
			
	        packet_2[0]=protocol_normal.hash=protocol_normal.Calculate_Checksum_Fragments(packet_2);

			std::cout << "=======================================================" << std::endl;
			std::cout << "Client Sending ----> Fragment #" << i+1 << " | " << packet_2 << std::endl;
			std::cout << "=======================================================" << std::endl;
			sf.packets[i] = packet_2;
	        sendto(client_socket,packet_2.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

			start += frag_size;
	    }
        
    }

    void Broadcast(int client_socket, sockaddr_in& server_addr) {
        std::string msg;
        std::cout << "Give me the message to everyone -> ";
        std::getline(std::cin, msg);
        int size_msg = msg.size();
		
		int seq_numbers{0};
	
	    // First fragment
		int header=7+1+3+final_name.size()+3+0+5+size_msg+11+0+20+0;
		int remaining_size_first=DATAGRAM_SIZE-header;
		int current_size =std::min(remaining_size_first,size_msg);

	    int total_remaining = size_msg - current_size;
	    int max_content = DATAGRAM_SIZE - 7;
	    int extra_fragments = (total_remaining + max_content - 1) / max_content;
	    int total_fragments = 1 + extra_fragments;
	
	    int first_order = (total_fragments == 1) ? 11 : 1;
		
		ProtocolFormat protocol{'0',first_order,seq_numbers++,'B',(int)final_name.size(),final_name,0,"",size_msg,msg,0,"",0,""};
		
		std::string packet=protocol.ConstructDatagram();
		
		while(packet.size() < DATAGRAM_SIZE){
			packet.push_back('#');
		}
		packet[0]=protocol.hash=protocol.Calculate_Checksum_Fragments(packet);

		SentFile sf;
		sf.total_fragments = total_fragments;
		sf.file_size = size_msg;
		sf.packets.resize(total_fragments);
		sf.acked.resize(total_fragments,false);

		std::cout << "=======================================================" << std::endl;
		std::cout << "Client Sending from -> " << protocol.nickname << " to everyone with the datagram format of" << std::endl;
		std::cout << packet << std::endl;
		std::cout << "=======================================================" << std::endl;
		
		sf.packets[0] = packet;
		sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

		int start = current_size;
	    for(int i=1;i<total_fragments;i++){
	        int frag_size =std::min(max_content,size_msg-start);
	        std::string fragment =msg.substr(start,frag_size);

			int frag_order = (i == total_fragments - 1) ? 11 : 0;
			ProtocolFormat_Normal protocol_normal{'0',frag_order,seq_numbers++,fragment};
			
			std::string packet_2=protocol_normal.ConstructDatagram();
			while((int)packet_2.size() < 500){
				packet_2.push_back('#');
			}
			
	        packet_2[0]=protocol_normal.hash=protocol_normal.Calculate_Checksum_Fragments(packet_2);

			std::cout << "=======================================================" << std::endl;
			std::cout << "Client Sending ----> Fragment #" << i << " | " << packet_2 << std::endl;
			std::cout << "=======================================================" << std::endl;
			sf.packets[i] = packet_2;
	        sendto(client_socket,packet_2.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

			start += frag_size;
	    }
    }

void Broadcast_react(const std::string& buffer,sockaddr_in& server_addr){
	    std::string senderKey = GetSenderKey(server_addr);
	    int pos = 0;
	
	    char hash = buffer[0];
	    pos += 1;
	
	    int order = std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	
	    int seq_number = std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;
	
	    char calculated =Calculate_Checksum(buffer.substr(7,DATAGRAM_SIZE-7));
	
	    if(hash != calculated){
	        std::string error_msg= "ERROR CHECKSUM";
			ProtocolFormat protocol{'0',11,0,'E',0,"",0,"",(int)error_msg.size(),error_msg,0,"",0,""};
			std::string packet=protocol.ConstructDatagram();
			Error(packet);
	        return;
	    }
	
	    std::string content;
	
	    if(order == 1 || (order == 11 && seq_number == 0)){
	        char protocol_type = buffer[pos++];
	
	        if(protocol_type != 'b')
	            return;
	
	        int size_origin =std::atoi(buffer.substr(pos,3).c_str());
	
	        pos += 3;
	
	        std::string origin =buffer.substr(pos,size_origin);
	
	        pos += size_origin;
	
	        pos += 3;
	        pos += 0;
	
	        int size_msg =std::atoi(buffer.substr(pos,5).c_str());
	        pos += 5;
	
	        std::string msg = buffer.substr(pos,size_msg);
	
	        pos += size_msg;
	
	        pos += 11;
	        pos += 20;
	
	        pending_transfers[senderKey].origin = origin;
	        pending_transfers[senderKey].total_size = size_msg;
	        pending_transfers[senderKey].action = 'b';
	        pending_transfers[senderKey].fragments.clear();
	
	        content = msg;
	    }
	    else{
	        content = buffer.substr(7,DATAGRAM_SIZE-7);
	    }
	
	    auto& transfer = pending_transfers[senderKey];
	
	    transfer.fragments.push_back({seq_number,content});
		std::cout << "===================================================================" << std::endl;
	   	std::cout << "Client Broadcast Destination received datagram # " << seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;
	    if(order == 11){
	        transfer.last_seq = seq_number;
	        transfer.last_received = true;
	    }
	
	    if(transfer.last_received && (int)transfer.fragments.size()== transfer.last_seq + 1){
	        std::sort(transfer.fragments.begin(),transfer.fragments.end(),[](const auto& a,const auto& b){return a.first < b.first;});
	        std::string final_msg;
	
	        long long written = 0;
	
	        for(auto& frag : transfer.fragments){
	            long long remaining =transfer.total_size - written;
	
	            long long to_copy = std::min((long long)frag.second.size(),remaining);
	
	            final_msg.append(frag.second.data(),to_copy);
	            written += to_copy;
	        }
	
	        std::cout << "Message from: " << transfer.origin<< " with a message of -> " << final_msg<< std::endl;
	        pending_transfers.erase(senderKey);
	    }
	}

    void Unicast(int client_socket, sockaddr_in& server_addr) {
        std::string msg, nickname_dest;
        std::cout << "Give me the msg to be sent ";
        std::getline(std::cin, msg);
        int size_msg = msg.size();
        std::cout << "Give me the destination " << std::endl;
        std::getline(std::cin, nickname_dest);
        int size_dst = nickname_dest.size();
		
		int seq_numbers{0};
	
	    // First fragment
		int header=7+1+3+final_name.size()+3+size_dst+5+size_msg+11+0+20+0;
		int remaining_size_first=DATAGRAM_SIZE-header;
		int current_size =std::min(remaining_size_first,size_msg);

	    int total_remaining = size_msg - current_size;
	    int max_content = DATAGRAM_SIZE - 7;
	    int extra_fragments = (total_remaining + max_content - 1) / max_content;
	    int total_fragments = 1 + extra_fragments;
	
	    int first_order = (total_fragments == 1) ? 11 : 1;
		
		ProtocolFormat protocol{'0',first_order,seq_numbers++,'U',(int)final_name.size(),final_name,size_dst,nickname_dest,size_msg,msg,0,"",0,""};
		
		std::string packet=protocol.ConstructDatagram();
		
		while(packet.size() < DATAGRAM_SIZE){
			packet.push_back('#');
		}
		packet[0]=protocol.hash=protocol.Calculate_Checksum_Fragments(packet);

		SentFile sf;
		sf.total_fragments = total_fragments;
		sf.file_size = size_msg;
		sf.packets.resize(total_fragments);
		sf.acked.resize(total_fragments,false);

		std::cout << "=======================================================" << std::endl;
		std::cout << "Client Sending from -> " << protocol.nickname << " to " << protocol.nickname_dest << " with the datagram format of" << std::endl;
		std::cout << packet << std::endl;
		std::cout << "=======================================================" << std::endl;
		
		sf.packets[0] = packet;
		sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

		int start = current_size;
	    for(int i=1;i<total_fragments;i++){
	        int frag_size =std::min(max_content,size_msg-start);
	        std::string fragment =msg.substr(start,frag_size);

			int frag_order = (i == total_fragments - 1) ? 11 : 0;
			ProtocolFormat_Normal protocol_normal{'0',frag_order,seq_numbers++,fragment};
			
			std::string packet_2=protocol_normal.ConstructDatagram();
			while((int)packet_2.size() < 500){
				packet_2.push_back('#');
			}
			
	        packet_2[0]=protocol_normal.hash=protocol_normal.Calculate_Checksum_Fragments(packet_2);

			std::cout << "=======================================================" << std::endl;
			std::cout << "Client Sending ----> Fragment #" << i << " | " << packet_2 << std::endl;
			std::cout << "=======================================================" << std::endl;
			sf.packets[i] = packet_2;
	        sendto(client_socket,packet_2.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

			start += frag_size;
	    }
		
    }

    void Unicast_react(const std::string& buffer,sockaddr_in& server_addr) {
		std::string senderKey = GetSenderKey(server_addr);
	    int pos = 0;
	
	    char hash = buffer[0];
	    pos += 1;
	
	    int order = std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	
	    int seq_number = std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;
	
	    char calculated =Calculate_Checksum(buffer.substr(7,DATAGRAM_SIZE-7));
	
	    if(hash != calculated){
	        std::string error_msg= "ERROR CHECKSUM";
			ProtocolFormat protocol{'0',11,0,'E',0,"",0,"",(int)error_msg.size(),error_msg,0,"",0,""};
			std::string packet=protocol.ConstructDatagram();
			Error(packet);
	        return;
	    }
	
	    std::string content;
	
	    if(order == 1 || (order == 11 && seq_number == 0)){
	        char protocol_type = buffer[pos++];
	
	        if(protocol_type != 'u')
	            return;
	
	        int size_origin =std::atoi(buffer.substr(pos,3).c_str());
	        pos += 3;
	
	        std::string origin =buffer.substr(pos,size_origin);
	        pos += size_origin;

			int size_destination =std::atoi(buffer.substr(pos,3).c_str());
	        pos += 3;
			std::string destination =buffer.substr(pos,size_destination);
	        pos += size_destination;
	
	        int size_msg =std::atoi(buffer.substr(pos,5).c_str());
	        pos += 5;
	
	        std::string msg = buffer.substr(pos,size_msg);
	        pos += size_msg;
	
	        pos += 11;
	        pos += 20;
	
	        pending_transfers[senderKey].origin = origin;
	        pending_transfers[senderKey].total_size = size_msg;
	        pending_transfers[senderKey].action = 'u';
	        pending_transfers[senderKey].fragments.clear();
	
	        content = msg;
	    }
	    else{
	        content = buffer.substr(7,DATAGRAM_SIZE-7);
	    }
	
	    auto& transfer = pending_transfers[senderKey];
	
	    transfer.fragments.push_back({seq_number,content});
		std::cout << "===================================================================" << std::endl;
	   	std::cout << "Client Unicast Destination received datagram # " << seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;
	    if(order == 11){
	        transfer.last_seq = seq_number;
	        transfer.last_received = true;
	    }
	
	    if(transfer.last_received && (int)transfer.fragments.size()== transfer.last_seq + 1){
	        std::sort(transfer.fragments.begin(),transfer.fragments.end(),[](const auto& a,const auto& b){return a.first < b.first;});
	        std::string final_msg;
	
	        long long written = 0;
	
	        for(auto& frag : transfer.fragments){
	            long long remaining =transfer.total_size - written;
	
	            long long to_copy = std::min((long long)frag.second.size(),remaining);
	
	            final_msg.append(frag.second.data(),to_copy);
	            written += to_copy;
	        }
	
	        std::cout << "Message Unicast from: " << transfer.origin << " with a message of -> " << final_msg<< std::endl;
	        pending_transfers.erase(senderKey);
	    }
    }

    void JSON_react(const std::string& buffer, sockaddr_in& server_addr){
	    std::string senderKey = GetSenderKey(server_addr);
	    int pos = 0;
	    char hash = buffer[0];
	    pos += 1;
	
	    int order = std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	
	    int seq_number = std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;
	
	    char calculated =Calculate_Checksum(buffer.substr(7,DATAGRAM_SIZE-7));
	
	    if(hash != calculated){
	        std::string error_msg= "ERROR CHECKSUM";
			ProtocolFormat protocol{'0',11,0,'E',0,"",0,"",(int)error_msg.size(),error_msg,0,"",0,""};
			std::string packet=protocol.ConstructDatagram();
			Error(packet);
	        return;
	    }
	
	    std::string content;
	    if(order == 1 || (order == 11 && seq_number == 0)){
	        char protocol_type = buffer[pos++];
	
	        if(protocol_type != 't'){
	            return;
	        }
	
	        int size_origin =std::atoi(buffer.substr(pos,3).c_str());
	        pos += 3;
	
	        std::string origin =buffer.substr(pos,size_origin);
	        pos += size_origin;
	
	        int size_dest =std::atoi(buffer.substr(pos,3).c_str());
	        pos += 3;
	
	        std::string destination =buffer.substr(pos,size_dest);
	        pos += size_dest;
	
	        int size_msg =std::atoi(buffer.substr(pos,5).c_str());
	        pos += 5;
	
	        std::string msg =buffer.substr(pos,size_msg);
	        pos += size_msg;
	
	        pos += 11;
	        pos += 20;
	
	        pending_transfers[senderKey].origin = origin;
	        pending_transfers[senderKey].destination = destination;
	        pending_transfers[senderKey].total_size = size_msg;
	        pending_transfers[senderKey].action = 't';
	        pending_transfers[senderKey].fragments.clear();
	
	        content = msg;
	    }
	    else{
	        content = buffer.substr(7,DATAGRAM_SIZE-7);
	    }
	
	    auto& transfer = pending_transfers[senderKey];
	    transfer.fragments.push_back({seq_number,content});
	
	    std::cout << "===================================================================" << std::endl;
	    std::cout << "Client received JSON fragment # " << seq_number << std::endl;
	    std::cout << "===================================================================" << std::endl;
	
	    if(order == 11){
	        transfer.last_seq = seq_number;
	        transfer.last_received = true;
	    }
	
	    if(transfer.last_received &&(int)transfer.fragments.size() == transfer.last_seq + 1){
	        std::sort(transfer.fragments.begin(),transfer.fragments.end(),[](const auto& a,const auto& b){return a.first < b.first;});
	        std::string final_json;
	        long long copied = 0;
	
	        for(auto& fragment : transfer.fragments){
	            long long remaining =transfer.total_size - copied;
	            long long to_copy =std::min((long long)fragment.second.size(),remaining);
	            final_json.append(fragment.second.data(),to_copy);
	            copied += to_copy;
	        }
	       	json js = json::parse(final_json);
			std::cout << "==============================================" << std::endl;
			std::cout << "Client received list: " << js.dump(4) << std::endl;
			std::cout << "===============================================" << std::endl;
	
	        pending_transfers.erase(senderKey);
	    }
	}
    void Send_File(int client_socket,sockaddr_in& server_addr,std::string& file_name,std::string& destination){
	    int seq_numbers{0};
		std::ifstream file(file_name,std::ios::binary);
	
	    if(!file.is_open()){
	        std::cout << "Could not open file" << std::endl;
	        return;
	    }
	
	    std::stringstream ss;
	    ss << file.rdbuf();
	    std::string complete_file = ss.str();
	
	    // First fragment
		int header=7+1+3+final_name.size()+3+destination.size()+5+0+11+file_name.size()+20;
		int remaining_size_first=DATAGRAM_SIZE-header;
		int current_size =std::min(remaining_size_first,(int)complete_file.size());

	    int total_remaining = (int)complete_file.size() - current_size;
	    int max_content = DATAGRAM_SIZE - 7;
	    int extra_fragments = (total_remaining + max_content - 1) / max_content;
	    int total_fragments = 1 + extra_fragments;
	
	    int first_order = (total_fragments == 1) ? 11 : 1;
		
		ProtocolFormat protocol{'0',first_order,seq_numbers++,'F',(int)final_name.size(),final_name,(int)destination.size(),destination,0,"",(long long)file_name.size(),file_name,(long long)complete_file.size(),complete_file.substr(0,current_size)};
		
		std::string packet=protocol.ConstructDatagram();
		
		while(packet.size() < DATAGRAM_SIZE){
			packet.push_back('#');
		}
		packet[0]=protocol.hash=protocol.Calculate_Checksum_Fragments(packet);
		std::cout << (int)protocol.hash << " - " << (int)packet[0] << std::endl;
		SentFile sf;
		sf.total_fragments = total_fragments;
		sf.file_size = complete_file.size();
		sf.packets.resize(total_fragments);
		sf.acked.resize(total_fragments,false);

		std::cout << "=======================================================" << std::endl;
		std::cout << "Client Sending from -> " << protocol.nickname << " to " << protocol.nickname_dest << " with the datagram format of " << std::endl;
		std::cout << packet << std::endl;
		std::cout << "=======================================================" << std::endl;
		
		sf.packets[0] = packet;
		sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

		int start = current_size;
	    for(int i=1;i<total_fragments;i++){
	        int frag_size =std::min(max_content,(int)complete_file.size()-start);
	        std::string fragment =complete_file.substr(start,frag_size);

			int frag_order = (i == total_fragments - 1) ? 11 : 0;
			ProtocolFormat_Normal protocol_normal{'0',frag_order,seq_numbers++,fragment};
			
			std::string packet_2=protocol_normal.ConstructDatagram();
			while((int)packet_2.size() < 500){
				packet_2.push_back('#');
			}
			
	        packet_2[0]=protocol_normal.hash=protocol_normal.Calculate_Checksum_Fragments(packet_2);
			std::cout << (int)protocol_normal.hash << " - " << (int)packet_2[0] << std::endl;
			std::cout << "=======================================================" << std::endl;
			std::cout << "Client Sending ----> Fragment #" << i+1 << " | " << packet_2 << std::endl;
			std::cout << "=======================================================" << std::endl;
			sf.packets[i] = packet_2;
	        sendto(client_socket,packet_2.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));
			std::this_thread::sleep_for(std::chrono::microseconds(100));
			start += frag_size;
	    }
		
	}

    void File_read(const std::string& buffer, sockaddr_in& server_addr){
		std::string senderKey = GetSenderKey(server_addr);
	   
	    int pos = 0;
		char hash=buffer[0];
	   	pos += 1;
	    int order =std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	    int seq_number =std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;

		char calculated = Calculate_Checksum(buffer.substr(7, DATAGRAM_SIZE - 7));
		/*std::cout << "RECEIVED HASH = " << (int)hash << std::endl;
		std::cout << "CALCULATED HASH = " << (int)calculated << std::endl;*/
	    if(hash != calculated){
	        std::string error_msg = "ERROR CHECKSUM";
			ProtocolFormat protocol{'0',11,0,'E',0,"",0,"",(int)error_msg.size(),error_msg,0,"",0,""};
			std::string packet=protocol.ConstructDatagram();
			Error(packet);
	        return;
	    }
	   
	   	int size_origin,size_dest,size_msg;
	    long long size_file_name,size_content;
	   	char protocol_type;
	   	std::string origin,destination,file_name,content;
		std::cout << "Arrived with order -> " << order << " and SEQ # " << seq_number << std::endl;
		if(order == 1 || (order == 11 && seq_number == 0)){
			protocol_type = buffer[pos++];
		    if(protocol_type != 'f'){
		        return;
		    }
		
		    size_origin =std::atoi(buffer.substr(pos,3).c_str());
		    pos += 3;
		
		    origin=buffer.substr(pos,size_origin);
		    pos += size_origin;
	
			size_dest=std::atoi(buffer.substr(pos,3).c_str());
		   	pos += 3;
	
		   	destination=buffer.substr(pos,size_dest);
		   	pos += size_dest;
			
		   	size_msg = std::atoi(buffer.substr(pos,5).c_str());
			pos += 5;
	
		   	// NO MSG
		   
		    size_file_name =std::atoll(buffer.substr(pos,11).c_str());
		    pos += 11;
		
		    file_name =buffer.substr(pos,size_file_name);
		    pos += size_file_name;
		
		    size_content=std::atoll(buffer.substr(pos,20).c_str());
		    pos += 20;
		
		    content=buffer.substr(pos,size_content);
		    pos += size_content;

			pending_transfers[senderKey].destination = destination;
			pending_transfers[senderKey].file_name = file_name;
			pending_transfers[senderKey].total_size= size_content;
			pending_transfers[senderKey].action = 'f';
			pending_transfers[senderKey].origin=origin;
        	pending_transfers[senderKey].fragments.clear();
		}else {
	        content = buffer.substr(7, DATAGRAM_SIZE - 7);
	    }

	   if(pending_transfers.find(senderKey) == pending_transfers.end()){
		   	std::string error_msg ="ERROR: no transfer state for"+std::string{senderKey};
		   	ProtocolFormat protocol{'0',11,0,'E',0,"",0,"",(int)error_msg.size(),error_msg,0,"",0,""};
			std::string packet=protocol.ConstructDatagram();
			Error(packet);
	        return;
	    }

	   	pending_transfers[senderKey].fragments.push_back({seq_number, content});
	   	std::cout << "===================================================================" << std::endl;
	   	std::cout << "Client Destination received datagram # " << seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;
	   	auto& transfer = pending_transfers[senderKey];
		if (order == 11){
			transfer.last_seq = seq_number;
    		transfer.last_received = true;
		}

	    if(transfer.last_received && (int)transfer.fragments.size() == transfer.last_seq + 1){
			std::sort(transfer.fragments.begin(), transfer.fragments.end(), [](const auto& a, const auto& b){return a.first < b.first;});

		    std::ofstream ofs("received_" + transfer.file_name, std::ios::binary);
		    long long written = 0;
		    for(auto& pares : transfer.fragments){
		        long long remaining = transfer.total_size - written;
		        long long to_write  = std::min((long long)pares.second.size(), remaining);
		        ofs.write(pares.second.data(), to_write);
		        written += to_write;
		    }
		    ofs.close();
		    pending_transfers.erase(senderKey);
		}
	}

    void Cases_Client_UDP(char type,const std::string& buffer, int client_socket, sockaddr_in& server_addr) {
        switch (type) {
            case 'L': {
                Login(client_socket, server_addr);
                break;
            }
            case 'O': {
                logging_status = false;
                running = false;
                ProtocolFormat protocol{'0',11,0,'O',(int)final_name.size(),final_name,0,"",0,"",0,"",0,""};
			    std::string packet = protocol.ConstructDatagram();
			
			    while(packet.size() < DATAGRAM_SIZE){
			        packet.push_back('#');
			    }
			
			    packet[0] = protocol.Calculate_Checksum_Fragments(packet);
			    sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));
			    break;
            }
            case 'B': {
                Broadcast(client_socket, server_addr);
                break;
            }
            case 'U': {
                Unicast(client_socket, server_addr);
                break;
            }
            case 'T': {
                ProtocolFormat protocol{'0',11,0,'T',(int)final_name.size(),final_name,0,"",0,"",0,"",0,""};
			
			    std::string packet = protocol.ConstructDatagram();
			    while(packet.size() < DATAGRAM_SIZE){
			        packet.push_back('#');
			    }
			    packet[0] = protocol.Calculate_Checksum_Fragments(packet);
			    sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));
                break;
            }
            case 'F': {
                std::string dest, file_nam;
                std::cout << "Give me the file name ";
                std::getline(std::cin, file_nam);
                std::cout << " Give me the destination ";
                std::getline(std::cin, dest);
                Send_File(client_socket, server_addr, file_nam, dest);
                break;
            }
			case 'K': {
				std::cout << "All good OK" << std::endl;

			    if (logging_status == true) {
			        logging_status = false;
			        running = false;
			    }
			    else {
			        logging_status = true;
			        final_name = pending_name;
			    }
                break;
            }
            case 'E': {
                Error(buffer);
                break;
            }
            case 'b': {
                Broadcast_react(buffer,server_addr);
                break;
            }
            case 'u': {
                Unicast_react(buffer,server_addr);
                break;
            }
            case 't': {
                JSON_react(buffer, server_addr);
                break;
            }
            case 'f': {
                File_read(buffer,server_addr);
                break;
            }
            default: {
                std::cout << "This protocol is not registered in Client :( " << std::endl;
                break;
            }
        }
    }
};
