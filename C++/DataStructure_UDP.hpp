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
#include <map>
#include <algorithm>
#include <limits>
#include <fstream>
#include <mutex>
#include <atomic>
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

	bool ParseProtocol(const std::string& buffer,char check_proto){
		int pos = 0;
		this->hash=buffer[0];
	   	pos += 1;
		this->datagram_id = std::atoi(buffer.substr(pos,4).c_str());
		pos += 4;
		this->total_packets = std::atoi(buffer.substr(pos,4).c_str());
		pos += 4;
		this->seq_number = std::atoi(buffer.substr(pos,5).c_str());
		pos += 5;

		char calculated = Calculate_Checksum(buffer.substr(HEADER_SIZE, DATAGRAM_SIZE - HEADER_SIZE));
	    if(hash != calculated){
			std::string error_msg = "[WARNING] CHECKSUM";
			std::cout << error_msg << std::endl;
	    }
	   
		this->action = buffer[pos++];
		
		if(this->action != check_proto){
			return false;
		}


		this->nickname_size =std::atoi(buffer.substr(pos,3).c_str());
		pos += 3;
		this->nickname=buffer.substr(pos,this->nickname_size);
		pos += this->nickname_size;
		
		this->matrix_size = std::atoll(buffer.substr(pos,20).c_str());
		pos += 20;
		this->matrixcontent = buffer.substr(pos,this->matrix_size);
		pos+=this->matrix_size;

		return true;
	}
};

void Send_OK(int socket,sockaddr_in& addr){
	ProtocolFormat protocol{'0',0,1,0,'K',0,"",0,""};
    std::string packet = protocol.ConstructDatagram();

    while(packet.size() < DATAGRAM_SIZE){
        packet.push_back('#');
	}

    packet[0] = protocol.Calculate_Checksum_Fragments(packet);

    sendto(socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&addr,sizeof(addr));
}

void Send_Error(int socket,sockaddr_in& addr,const std::string& msg){
	ProtocolFormat protocol{'0',0,1,0,'E',0,"",(int)msg.size(),msg};

    std::string packet = protocol.ConstructDatagram();

    while(packet.size() < DATAGRAM_SIZE){
        packet.push_back('#');
	}

    packet[0] = protocol.Calculate_Checksum_Fragments(packet);

    sendto(socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&addr,sizeof(addr));
}



void print(const std::unordered_map<std::string,sockaddr_in>& clients){
	std::cout << "================================" << std::endl;
	for(const auto& client : clients){
	    std::cout << "ID: " << client.first << std::endl;
	}
	std::cout << "================================" << std::endl;
}

class Server_Protocols_UDP {
public:
	std::unordered_map<std::string,sockaddr_in> client_map;
	std::unordered_map<std::string, ClientInfo> pending_transfers;
	std::mutex mtx;
public:

    std::string Login(const std::string& buffer, int server_socket, sockaddr_in& client_addr) {
		ProtocolFormat proto;
		if(!proto.ParseProtocol(buffer,'L')){
			return "";
		}

		std::lock_guard<std::mutex> lock(mtx);
		std::string sender = GetSenderKey(client_addr);

        if(pending_transfers[sender].client_datagrams.find(proto.datagram_id) == pending_transfers[sender].client_datagrams.end()){
            pending_transfers[sender].client_datagrams[proto.datagram_id].total_fragments = proto.total_packets;
            pending_transfers[sender].client_datagrams[proto.datagram_id].matrix_size = proto.nickname_size;
            pending_transfers[sender].client_datagrams[proto.datagram_id].packets.resize(proto.total_packets);
            pending_transfers[sender].client_datagrams[proto.datagram_id].acked.resize(proto.total_packets,false);
        }

		pending_transfers[sender].client_datagrams[proto.datagram_id].packets[proto.seq_number]=proto.nickname;
		pending_transfers[sender].client_datagrams[proto.datagram_id].acked[proto.seq_number]=true;
		
	   	std::cout << "===================================================================" << std::endl;
	   	std::cout << "Server received datagram # " << proto.seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;

		SentFile &file = pending_transfers[sender].client_datagrams[proto.datagram_id];
		bool all_acked = std::all_of(file.acked.begin(), file.acked.end(), [](bool b){ return b; });
		if(all_acked){
			std::string assembled;
            long long written = 0;
            for(const auto &frag : file.packets){
                long long remaining = file.matrix_size - written;
                long long to_write = std::min((long long)frag.size(), remaining);
                assembled += frag.substr(0, to_write);
                written += to_write;
            }
            pending_transfers[sender].client_datagrams.erase(proto.datagram_id);

            if(client_map.find(assembled) != client_map.end()){
                std::string msg = "ERROR nickname already exists";
                Send_Error(server_socket, client_addr, msg);
                return "";
            }

            client_map[assembled] = client_addr;
            print(client_map);
		}
		Send_OK(server_socket, client_addr);
        return proto.nickname;
    }

	void Logout(const std::string& buffer,int server_socket,sockaddr_in& client_addr){
	    ProtocolFormat proto;
		if(!proto.ParseProtocol(buffer,'O')){
			return;
		}
		std::lock_guard<std::mutex> lock(mtx);
		std::string sender = GetSenderKey(client_addr);
	    bool found = false;

		std::cout << "===================================================================" << std::endl;
	   	std::cout << "Server received datagram # " << proto.seq_number << " with the content | " << buffer << std::endl;
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
			std::string error_msg = "ERROR USER NOT LOGGED";
			Send_Error(server_socket,client_addr,error_msg);
	        return;
	    }
		Send_OK(server_socket, client_addr);
	    print(client_map);
	}

    void Broadcast(const std::string& buffer,int server_socket,sockaddr_in& client_addr){
		ProtocolFormat proto;
		if(!proto.ParseProtocol(buffer,'B')){
			return;
		}

		std::lock_guard<std::mutex> lock(mtx);
		std::string cp=buffer;
		std::string sender = GetSenderKey(client_addr);

        if(pending_transfers[sender].client_datagrams.find(proto.datagram_id) == pending_transfers[sender].client_datagrams.end()){
            pending_transfers[sender].client_datagrams[proto.datagram_id].total_fragments = proto.total_packets;
            pending_transfers[sender].client_datagrams[proto.datagram_id].matrix_size = proto.matrix_size;
            pending_transfers[sender].client_datagrams[proto.datagram_id].packets.resize(proto.total_packets);
            pending_transfers[sender].client_datagrams[proto.datagram_id].acked.resize(proto.total_packets,false);
        }
		
		cp[14]='b';
		pending_transfers[sender].client_datagrams[proto.datagram_id].packets[proto.seq_number]=cp;
		pending_transfers[sender].client_datagrams[proto.datagram_id].acked[proto.seq_number]=true;
		
	   	std::cout << "===================================================================" << std::endl;
	   	std::cout << "Server received datagram # " << proto.seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;

		SentFile &file = pending_transfers[sender].client_datagrams[proto.datagram_id];
		bool all_acked = std::all_of(file.acked.begin(), file.acked.end(), [](bool b){ return b; });
		if(all_acked){
			for(const auto& client : client_map){
				for(const auto& pac:file.packets){
					sendto(server_socket,pac.data(),DATAGRAM_SIZE,0,(sockaddr*)&client.second,sizeof(client.second));
				}
			}
			pending_transfers[sender].client_datagrams.erase(proto.datagram_id);
		}
		
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
            default: {
                std::cout << "This protocol is not registered in Server :( " << std::endl;
                break;
            }
        }
    }
};

class Client_Protocols_UDP {
public:
    std::atomic<bool> running;
	std::atomic<bool> logging_status;
	std::atomic<int> actual_datagram_id = 0;
	std::string final_name,pending_name;
	ClientInfo pending_transfers;
public:
    void Error(const std::string& buffer) {
        ProtocolFormat proto;
		if(!proto.ParseProtocol(buffer,'E')){
			return;
		}
	

		std::cout << "===================================================================" << std::endl;
	   	std::cout << "Client received error with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;
		
	    std::cout<< "ERROR -> "<< proto.matrixcontent << std::endl;
    }

    void Login(int client_socket, sockaddr_in& server_addr) {
		std::string name;
        std::cout << "Give me your nickname to send -> ";
        std::getline(std::cin, name);
		pending_name=name;
        int size_msg = name.size();
		
		int seq_numbers{0};
	
	    // First fragment
		int header=HEADER_SIZE+3+pending_name.size()+20+0;
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
		cf.client_datagrams[actual_datagram_id].total_fragments = total_fragments;
		cf.client_datagrams[actual_datagram_id].matrix_size = pending_name.size();
		cf.client_datagrams[actual_datagram_id].packets.resize(total_fragments);
		cf.client_datagrams[actual_datagram_id].acked.resize(total_fragments,false);

		std::cout << "=======================================================" << std::endl;
		std::cout << "Client Sending from -> " << protocol.nickname << " to server with the datagram format of" << std::endl;
		std::cout << packet << std::endl;
		std::cout << "=======================================================" << std::endl;
		
		cf.client_datagrams[actual_datagram_id].packets[0] = packet;
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
			cf.client_datagrams[actual_datagram_id].packets[i] = packet_2;
	        sendto(client_socket,packet_2.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

			start += frag_size;
	    }
        actual_datagram_id++;
    }

    void Broadcast(int client_socket, sockaddr_in& server_addr) {
		std::string msg;
        std::cout << "Give me the message to everyone -> ";
        std::getline(std::cin, msg);
		
		int seq_numbers{0};
	
	    // First fragment
		int header=HEADER_SIZE+3+final_name.size()+20+msg.size();
		int remaining_size_first=DATAGRAM_SIZE-header;
		int current_size =std::min(remaining_size_first,(int)msg.size());

	    int total_remaining = (int)msg.size() - current_size;
	    int max_content = DATAGRAM_SIZE - header;
	    int extra_fragments = (total_remaining + max_content - 1) / max_content;
	    int total_fragments = 1 + extra_fragments;
	
		
		ProtocolFormat protocol{'0',actual_datagram_id,total_fragments,seq_numbers++,'B',(int)final_name.size(),final_name,(int)msg.size(),msg};
		
		std::string packet=protocol.ConstructDatagram();
		
		while(packet.size() < DATAGRAM_SIZE){
			packet.push_back('#');
		}
		packet[0]=protocol.hash=protocol.Calculate_Checksum_Fragments(packet);

		ClientInfo cf;
		cf.client_datagrams[actual_datagram_id].total_fragments = total_fragments;
		cf.client_datagrams[actual_datagram_id].matrix_size = msg.size();
		cf.client_datagrams[actual_datagram_id].packets.resize(total_fragments);
		cf.client_datagrams[actual_datagram_id].acked.resize(total_fragments,false);

		std::cout << "=======================================================" << std::endl;
		std::cout << "Client Sending from -> " << protocol.nickname << " to server with the datagram format of" << std::endl;
		std::cout << packet << std::endl;
		std::cout << "=======================================================" << std::endl;
		
		cf.client_datagrams[actual_datagram_id].packets[0] = packet;
		sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

		int start = current_size;
	    for(int i=1;i<total_fragments;i++){
	        int frag_size =std::min(max_content,(int)msg.size()-start);
	        std::string fragment =msg.substr(start,frag_size);

			ProtocolFormat protocol_normal{'0',actual_datagram_id,total_fragments,seq_numbers++,'B',(int)final_name.size(),final_name,(int)msg.size(),msg};
			
			std::string packet_2=protocol_normal.ConstructDatagram();
			while((int)packet_2.size() < 500){
				packet_2.push_back('#');
			}
			
	        packet_2[0]=protocol_normal.hash=protocol_normal.Calculate_Checksum_Fragments(packet_2);

			std::cout << "=======================================================" << std::endl;
			std::cout << "Client Sending ----> Fragment #" << i+1 << " | " << packet_2 << std::endl;
			std::cout << "=======================================================" << std::endl;
			cf.client_datagrams[actual_datagram_id].packets[i] = packet_2;
	        sendto(client_socket,packet_2.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

			start += frag_size;
	    }
        actual_datagram_id++;
    }

	void Broadcast_react(const std::string& buffer,sockaddr_in& server_addr){
	    ProtocolFormat proto;
		if(!proto.ParseProtocol(buffer,'b')){
			return;
		}


		if(pending_transfers.client_datagrams.find(proto.datagram_id) == pending_transfers.client_datagrams.end()){
			pending_transfers.client_datagrams[proto.datagram_id].total_fragments= proto.total_packets;
			pending_transfers.client_datagrams[proto.datagram_id].matrix_size = proto.matrix_size;
			pending_transfers.client_datagrams[proto.datagram_id].packets.resize(proto.total_packets);
			pending_transfers.client_datagrams[proto.datagram_id].acked.resize(proto.total_packets,false);
		}
		
		pending_transfers.client_datagrams[proto.datagram_id].packets[proto.seq_number]=proto.matrixcontent;
		pending_transfers.client_datagrams[proto.datagram_id].acked[proto.seq_number]=true;
		
	   	std::cout << "===================================================================" << std::endl;
	   	std::cout << "Client received datagram # " << proto.seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;

		SentFile &file = pending_transfers.client_datagrams[proto.datagram_id];
		bool all_acked = std::all_of(file.acked.begin(), file.acked.end(), [](bool b){ return b; });
		if(all_acked){
			std::string new_buff;
			long long written = 0;
			for(const auto& pac :file.packets){
				long long remaining = file.matrix_size - written;
				long long to_write = std::min((long long)pac.size(), remaining);
				new_buff += pac.substr(0, to_write);
				written += to_write;
			}
			std::cout << "Message from: " << proto.nickname<< " with a message of -> " << new_buff << std::endl;
			pending_transfers.client_datagrams.erase(proto.datagram_id);
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
				ProtocolFormat protocol{'0',actual_datagram_id++,1,0,'O',(int)final_name.size(),final_name,0,""};
			    std::string packet = protocol.ConstructDatagram();
			
			    while(packet.size() < DATAGRAM_SIZE){
			        packet.push_back('#');
			    }
			
			    packet[0] = protocol.Calculate_Checksum_Fragments(packet);
			    sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));
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
			case 'B': {
                Broadcast(client_socket, server_addr);
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
