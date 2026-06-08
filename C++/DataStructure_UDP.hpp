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
#include <vector>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <algorithm>
#include <limits>
#include <fstream>
#include <sstream>

#define DATAGRAM_SIZE 500
#define HEADER_SIZE 15


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

    return static_cast<char>(sum % 9) + '0';
}

std::string GetSenderKey(sockaddr_in& addr){
    return std::string(inet_ntoa(addr.sin_addr)) + ":" + std::to_string(ntohs(addr.sin_port));
}


struct SentFile{
    int total_fragments;
	long long matrix_size;
    std::vector<std::string> packets;
    std::vector<bool> acked;
};

struct ClientInfo{
	std::map<int,SentFile> client_datagrams;
};

struct ProtocolFormat{
	char hash; // 1 byte
	int datagram_id; // 4 byte
	int total_packets = -1; // 4 byte
	int seq_number; // 5 byte
	char action; // 1 byte
	int nickname_size; // 3 byte
	std::string nickname; // Variable
	long long matrix_size; // 20 byte
	std::string matrixcontent; // Variable

	std::string ConstructDatagram(){
		return std::string{hash}+number_to_string_2(datagram_id,4)+number_to_string_2(total_packets,4)+number_to_string_2(seq_number,5)+std::string{action}
		+number_to_string_2(nickname_size,3)+nickname+number_to_string_2(matrix_size,20)+matrixcontent;
	}
	char Calculate_Checksum_Fragments(std::string& packet){
		return Calculate_Checksum(packet.substr(HEADER_SIZE,packet.size()-HEADER_SIZE));
	}
};



void print(const std::unordered_map<sockaddr_in,std::string>& clients){
	std::cout << "================================" << std::endl;
	for(const auto& client : clients){
	    std::cout << "ID: " << client.second << std::endl;
	}
	std::cout << "================================" << std::endl;
}

class Server_Protocols_UDP {
public:
	std::unordered_map<std::string,sockaddr_in> client_map;
	std::unordered_map<std::string, ClientInfo> pending_transfers;
public:

    std::string Login(const std::string& buffer, int server_socket, sockaddr_in& client_addr) {
	   
	    int pos = 0;
		char hash=buffer[0];
	   	pos += 1;
		int datagram_id = std::atoi(buffer.substr(pos,4).c_str());
		pos += 4;
		int total_packets = std::atoi(buffer.substr(pos,4).c_str());
		pos += 4;
		int seq_number = std::atoi(buffer.substr(pos,5).c_str());
		pos += 5;

		char calculated = Calculate_Checksum(buffer.substr(HEADER_SIZE, DATAGRAM_SIZE - HEADER_SIZE));
	    if(hash != calculated){
			std::string error_msg = "[WARNING] CHECKSUM";
			std::cout << error_msg << std::endl;
	    }
	   
	   	int size_nickname;
	    long long size_matrix;
	   	char protocol_type;
	   	std::string nickname,matrix_content;
		protocol_type = buffer[pos++];
		
		if(protocol_type != 'L'){
			return "";
		}
	
		size_nickname =std::atoi(buffer.substr(pos,3).c_str());
		pos += 3;
	
		nickname=buffer.substr(pos,size_nickname);
		pos += size_origin;
		
		if (client_map.find(nickname) != client_map.end()) {
			std::string error_msg = "ERROR nickname already in server";
			std::cout << error_msg << std::endl;
			return nickname;
		}
		pos += 20;

		pending_transfers[nickname].client_datagrams[datagram_id].total_fragments= total_packets;
		pending_transfers[nickname].client_datagrams[datagram_id].matrix_size = size_nickname;
		pending_transfers[nickname].client_datagrams[datagram_id].packets.resize(total_packets);
		pending_transfers[nickname].client_datagrams[datagram_id].acked.resize(total_packets,false);

		pending_transfers[nickname].client_datagrams[datagram_id].packets[seq_number]=nickname;
		pending_transfers[nickname].client_datagrams[datagram_id].acked[seq_number]=true;
		
	   	std::cout << "===================================================================" << std::endl;
	   	std::cout << "Server received datagram # " << seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;

		SentFile &file = pending_transfers[nickname].client_datagrams[datagram_id];
		bool all_acked = std::all_of(file.acked.begin(), file.acked.end(), [](bool b){ return b; });
		if(all_acked){
			std::string new_buff;
			long long written = 0;
			for(const auto &frag : file.packets){
			    long long remaining = file.matrix_size - written;
			    long long to_write = std::min((long long)frag.size(), remaining);
			    new_buff += frag.substr(0, to_write);
			    written += to_write;
			}
			pending_transfers.erase(nickname);
			client_map[nickname] = client_addr;
	        print(client_map);
		}
		
        return nickname;
    }

    /*void Broadcast(const std::string& buffer,int server_socket,sockaddr_in& client_addr){
	    std::string senderKey = GetSenderKey(client_addr);
	
	    int pos = 0;
	
	    char hash = buffer[0];
	    pos += 1;
	
	    int order = std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	
	    int seq_number = std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;
	
	    std::string copy = buffer;
	
	    char calculated = Calculate_Checksum(buffer.substr(7,DATAGRAM_SIZE-7));
	
	    if(hash != calculated){
	        Send_Error(server_socket,client_addr,"[WARNING] CHECKSUM");
	    }
	    std::string content;
	
	    if(order == 1 || (order == 11 && seq_number == 0)){
	        char protocol_type = buffer[pos++];
	
	        if(protocol_type != 'B'){
	            return;
			}
	
	        copy[7] = 'b';
	
	        calculated = Calculate_Checksum(copy.substr(7,DATAGRAM_SIZE-7));
	
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

		std::cout << "===================================================================" << std::endl;
	   	std::cout << "Server received datagram # " << seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;
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
	}*/

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
            default: {
                std::cout << "This protocol is not registered in Server :( " << std::endl;
                break;
            }
        }
    }
};

class Client_Protocols_UDP {
public:
    bool logging_status = false, running = false;
	std::string final_name,pending_name;
	ClientInfo pending_transfers;
	int actual_datagram_id=0;
public:
    /*void Error(const std::string& buffer) {
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

		std::cout << "===================================================================" << std::endl;
	   	std::cout << "Client received error with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;
		
	    std::cout<< "ERROR -> "<< error_msg<< std::endl;
    }*/

    void Login(int client_socket, sockaddr_in& server_addr) {
		std::string name;
        std::cout << "Give me your nickname to send -> ";
        std::getline(std::cin, name);
		pending_name=name;
        int size_msg = name.size();
		
		int seq_numbers{0};
	
	    // First fragment
		int header=HEADER_SIZE+3+final_name.size()+20+0;
		int remaining_size_first=DATAGRAM_SIZE-header;
		int current_size =std::min(remaining_size_first,(int)pending_name.size());

	    int total_remaining = (int)pending_name.size() - current_size;
	    int max_content = DATAGRAM_SIZE - header;
	    int extra_fragments = (total_remaining + max_content - 1) / max_content;
	    int total_fragments = 1 + extra_fragments;
	
		
		ProtocolFormat protocol{'0',actual_datagram_id,total_fragments,seq_numbers++,'L',(int)pending_name.size(),pending_name,0,""};
		
		std::string packet=protocol.ConstructDatagram();
		
		while(packet.size() < DATAGRAM_SIZE){
			packet.push_back('#');
		}
		packet[0]=protocol.hash=protocol.Calculate_Checksum_Fragments(packet);

		ClientInfo cf;
		cf[actual_datagram_id].total_fragments = total_fragments;
		cf[actual_datagram_id].matrix_size = pending_name.size();
		cf[actual_datagram_id].packets.resize(total_fragments);
		cf[actual_datagram_id].acked.resize(total_fragments,false);

		std::cout << "=======================================================" << std::endl;
		std::cout << "Client Sending from -> " << protocol.nickname << " to server with the datagram format of" << std::endl;
		std::cout << packet << std::endl;
		std::cout << "=======================================================" << std::endl;
		
		cf[actual_datagram_id].packets[0] = packet;
		sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

		int start = current_size;
	    for(int i=1;i<total_fragments;i++){
	        int frag_size =std::min(max_content,(int)pending_name.size()-start);
	        std::string fragment =pending_name.substr(start,frag_size);

			ProtocolFormat protocol_normal{'0',actual_datagram_id,total_fragments,seq_numbers++,'L',(int)pending_name.size(),pending_name,0,""};
			
			std::string packet_2=protocol_normal.ConstructDatagram();
			while((int)packet_2.size() < 500){
				packet_2.push_back('#');
			}
			
	        packet_2[0]=protocol_normal.hash=protocol_normal.Calculate_Checksum_Fragments(packet_2);

			std::cout << "=======================================================" << std::endl;
			std::cout << "Client Sending ----> Fragment #" << i+1 << " | " << packet_2 << std::endl;
			std::cout << "=======================================================" << std::endl;
			cf[actual_datagram_id].packets[i] = packet_2;
	        sendto(client_socket,packet_2.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

			start += frag_size;
	    }
        actual_datagram_id++;
    }

    /*void Broadcast(int client_socket, sockaddr_in& server_addr) {
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
	        std::string error_msg= "[WARNING] CHECKSUM";
			ProtocolFormat protocol{'0',11,0,'E',0,"",0,"",(int)error_msg.size(),error_msg,0,"",0,""};
			std::string packet=protocol.ConstructDatagram();
			Error(packet);
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
	}*/

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
            default: {
                std::cout << "This protocol is not registered in Client :( " << std::endl;
                break;
            }
        }
    }
};
