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

typedef nlohmann::json json;

int global_seq = 1;

struct FileAssembly{
    int total_fragments;

    std::string file_name;
    std::string origin;

    std::vector<std::string> fragments;
    std::vector<bool> received;
};

struct SentFile{
    int total_fragments;
	long long file_size;
    std::vector<std::string> packets;
    std::vector<bool> acked;
};

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

int Calculate_Max_Content(std::string& destination,std::string& file_name,std::string& origin){
    int header_size = 0;
    header_size += 14;
    header_size += destination.size();

    header_size += 3;
    header_size += file_name.size();

    header_size += 5;
    header_size += origin.size();
    header_size += 35;
    return DATAGRAM_SIZE - header_size;
}

char Calculate_Checksum(std::string& content){
    int sum = 0;
    for(unsigned char c : content){
        sum += c;
	}

    return static_cast<char>(sum % 256);
}

std::string Create_ACKNACK(char type,int seq,int total,int current){
    std::string packet;

    packet += type;

    packet += number_to_string_2(seq,4);
    packet += number_to_string_2(total,2);
    packet += number_to_string_2(current,2);

    while(packet.size() < DATAGRAM_SIZE){
        packet.push_back('#');
    }

    return packet;
}

void NACKACK_read(const std::string& buffer,int client_socket,sockaddr_in& server_addr,bool ack,bool &waiting_ACK,std::unordered_map<int,SentFile> &sent_files){
    int seq=std::atoi(buffer.substr(1,4).c_str());
    int total=std::atoi(buffer.substr(5,2).c_str());
    int current =std::atoi(buffer.substr(7,2).c_str());

	auto it = sent_files.find(seq);
    if(it == sent_files.end()){
        return;
    }
    SentFile& file = it->second;
	waiting_ACK=false;
	if(ack){
		sent_files[seq].acked[current-1] = true;

        bool complete = true;
        for(bool ok : sent_files[seq].acked){
            if(!ok){
                complete = false;
                break;
            }
			//std::cout << "Seq=" << seq << " fragment=" << current << "/" << total << std::endl;
        }

        if(complete){
            std::cout<< "Transfer "<< seq<< " completed"<< std::endl;
            global_seq++;
			sent_files.erase(it);
        }

        return;
    }

    std::cout << "Retransmitting fragment " << current << std::endl;
    sendto(client_socket,sent_files[seq].packets[current-1].data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));
    
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

    std::string Login(const std::string& buffer, int server_socket, sockaddr_in& client_addr) {
        std::string size_str = buffer.substr(1, 4);
        int size_name = std::atoi(size_str.c_str());
        
        std::string nickname = buffer.substr(5, size_name);
        if (client_map.find(nickname) != client_map.end()) {
            std::string error_msg = "ERROR nickname already in server";
            int size_error = error_msg.size();
            std::string final_msg = "E" + number_to_string_2(size_error, 5) + error_msg;
            sendto(server_socket, final_msg.data(), final_msg.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
        } else {
            std::cout << "Hiiii" << std::endl;
            client_map[nickname] = client_addr;
            char k = 'K';
            sendto(server_socket, &k, 1, 0, (sockaddr*)&client_addr, sizeof(client_addr));
	        print(client_map);
        }
        
        return nickname;
    }

    void Broadcast(const std::string& buffer, int server_socket, sockaddr_in& client_addr) {
        std::string size_str = buffer.substr(1, 7);
        int size_msg = std::atoi(size_str.c_str());
        
        std::string msg = buffer.substr(8, size_msg);
        
        std::string author;
        for (auto& pair : client_map) {
            if (pair.second.sin_addr.s_addr == client_addr.sin_addr.s_addr && pair.second.sin_port == client_addr.sin_port) {
                author = pair.first;
                break;
            }
        }
        
        int size_author = author.size();
        std::string broadcast_msg = "b" + number_to_string_2(size_author, 3) + author + number_to_string_2(size_msg, 7) + msg;
        
        for (const auto& pair : client_map) {
            sendto(server_socket, broadcast_msg.data(), broadcast_msg.size(), 0, (sockaddr*)&(pair.second), sizeof(pair.second));
        }
    }

    void Unicast(const std::string& buffer, int server_socket, sockaddr_in& client_addr) {
        std::string size_str = buffer.substr(1, 5);
        int size_msg = std::atoi(size_str.c_str());
        
        std::string msg = buffer.substr(6, size_msg);
        
        size_str = buffer.substr(6 + size_msg, 7);
        int size_dst = std::atoi(size_str.c_str());
        
        std::string destination = buffer.substr(6 + size_msg + 7, size_dst);

        if (client_map.find(destination) == client_map.end()) {
            std::string error_msg = "ERROR destination not in the server";
            int size_error = error_msg.size();
            std::string final_msg = "E" + number_to_string_2(size_error, 5) + error_msg;
            sendto(server_socket, final_msg.data(), final_msg.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
            return;
        }
        
        std::string author;
        for (const auto& pair : client_map) {
            if (pair.second.sin_addr.s_addr == client_addr.sin_addr.s_addr && pair.second.sin_port == client_addr.sin_port) {
                author = pair.first;
                break;
            }
        }
        
        int size_auth = author.size();
        sockaddr_in dst_addr = client_map.find(destination)->second;
        
        std::string final_msg = "u" + number_to_string_2(size_auth, 7) + author + number_to_string_2(size_msg, 5) + msg;
        sendto(server_socket, final_msg.data(), final_msg.size(), 0, (sockaddr*)&dst_addr, sizeof(dst_addr));
    }

    void Send_List(int server_socket, sockaddr_in& client_addr) {
        json js;
        js["clients"] = json::array();
        for (const auto& pair : client_map) {
            js["clients"].push_back(pair.first);
        }
        std::string to_send = js.dump();
        std::string final_msg = "t" + number_to_string_2((int)to_send.size(), 5) + to_send;
        sendto(server_socket, final_msg.data(), final_msg.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
    }

   void File_redirect(const std::string& buffer,int server_socket,sockaddr_in& client_addr){
	    int pos = 0;
	    int total_fragments =std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	    int current_fragment =std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	    int global_seq =std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;
	
	    char protocol_type = buffer[pos++];
	    if(protocol_type != 'F'){
	        return;
	    }
	
	    int size_dest =std::atoi(buffer.substr(pos,5).c_str());
	    pos += 5;
	
	    std::string destination =buffer.substr(pos,size_dest);
	    pos += size_dest;
	
	    int size_file_name =std::atoi(buffer.substr(pos,3).c_str());
	    pos += 3;
	
	    std::string file_name =buffer.substr(pos,size_file_name);
	    pos += size_file_name;
	
	    int size_origin =std::atoi(buffer.substr(pos,5).c_str());
	    pos += 5;
	
	    std::string origin =buffer.substr(pos,size_origin);
	    pos += size_origin;
	
	    long long fragment_seq =std::atoll(buffer.substr(pos,12).c_str());
	    pos += 12;
	
	    long long size_content=std::atoll(buffer.substr(pos,22).c_str());
	    pos += 22;
	
	    std::string content=buffer.substr(pos,size_content);
	    pos += size_content;

		static bool corrupt_once = true;
		if(corrupt_once){
		    corrupt_once = false;
		    content[0] = 'X';
		    std::cout << "[Test] Corrupting packet" << std::endl;
		}
		/*static bool delay_once = true;
		if(delay_once){
		    delay_once = false;
		    std::cout << "[Test] Delaying ACK 2 seconds" << std::endl;
		    std::this_thread::sleep_for(std::chrono::seconds(2));
		}*/
	    char received_checksum =buffer[pos];
	    char calculated_checksum =Calculate_Checksum(content);
	
	    if(received_checksum != calculated_checksum){
	        std::string nack=Create_ACKNACK('k',global_seq,total_fragments,current_fragment);
	        sendto(server_socket,nack.data(),DATAGRAM_SIZE,0,(sockaddr*)&client_addr,sizeof(client_addr));
	        return;
	    }
	
	    std::string ack=Create_ACKNACK('K',global_seq,total_fragments,current_fragment);
	    sendto(server_socket,ack.data(),DATAGRAM_SIZE,0,(sockaddr*)&client_addr,sizeof(client_addr));
	
	    if(client_map.find(destination) == client_map.end()){
	        std::string error_msg ="ERROR destination not in the server for file";
	        std::string final_msg ="E" +number_to_string_2(error_msg.size(),5) +error_msg;
	        sendto(server_socket,final_msg.data(),final_msg.size(),0,(sockaddr*)&client_addr,sizeof(client_addr));
	        return;
	    }
	    sockaddr_in dst = client_map[destination];
	    sendto(server_socket,buffer.data(),DATAGRAM_SIZE,0,(sockaddr*)&dst,sizeof(dst));
	}

    void Logout(int server_socket, sockaddr_in& client_addr) {
        std::string author;
        for (auto it = client_map.begin(); it != client_map.end(); ++it) {
            if (it->second.sin_addr.s_addr == client_addr.sin_addr.s_addr && it->second.sin_port == client_addr.sin_port) {
                author = it->first;
                client_map.erase(it);
                char k = 'K';
                sendto(server_socket, &k, 1, 0, (sockaddr*)&client_addr, sizeof(client_addr));
		        print(client_map);
                return;
            }
        }
        
        std::string error_msg = "ERROR logout";
        int size_error = error_msg.size();
        std::string final_msg = "E" + number_to_string_2(size_error, 5) + error_msg;
        sendto(server_socket, final_msg.data(), final_msg.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
    }

    void Cases_Server(char type,const std::string& buffer, int server_socket, sockaddr_in& client_addr) {
        switch (type) {
            case 'L': {
                Login(buffer, server_socket, client_addr);
                break;
            }
            case 'O': {
                Logout(server_socket, client_addr);
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
	std::unordered_map<int,FileAssembly> pending_files;
	std::unordered_map<int,SentFile> sent_files;
public:
    void Error(const std::string& buffer) {
        std::string size_str = buffer.substr(1, 5);
        int size_msg = std::atoi(size_str.c_str());
        
        std::string error_msg = buffer.substr(6, size_msg);
        std::cout << "ERROR -> " << error_msg << std::endl;
    }

    void Login(int client_socket, sockaddr_in& server_addr) {
        std::string name;
        std::cout << "Give me your nickname to send -> ";
        std::getline(std::cin, name);
        int size_msg = name.size();
        std::string final_msg = "L" + number_to_string_2(size_msg, 4) + name;
        
        sendto(client_socket, final_msg.data(), final_msg.size(), 0, (sockaddr*)&server_addr, sizeof(server_addr));
    }

    void Broadcast(int client_socket, sockaddr_in& server_addr) {
        std::string msg;
        std::cout << "Give me the message to everyone -> ";
        std::getline(std::cin, msg);
        int size_msg = msg.size();
        std::string final_msg = "B" + number_to_string_2(size_msg, 7) + msg;
        
        sendto(client_socket, final_msg.data(), final_msg.size(), 0, (sockaddr*)&server_addr, sizeof(server_addr));
    }

    void Broadcast_react(const std::string& buffer) {
        std::string size_str = buffer.substr(1, 3);
        int size_author = std::atoi(size_str.c_str());
        
        std::string author = buffer.substr(4, size_author);
        
        size_str = buffer.substr(4 + size_author, 7);
        int size_msg = std::atoi(size_str.c_str());
        
        std::string msg = buffer.substr(4 + size_author + 7, size_msg);
        
        std::cout << "Message from: " << author << " with a message of " << msg << std::endl;
    }

    void Unicast(int client_socket, sockaddr_in& server_addr) {
        std::string msg, nickname_dest;
        std::cout << "Give me the msg to be sent ";
        std::getline(std::cin, msg);
        int size_msg = msg.size();
        std::cout << "Give me the destination " << std::endl;
        std::getline(std::cin, nickname_dest);
        int size_dst = nickname_dest.size();
        std::string final_msg = "U" + number_to_string_2(size_msg, 5) + msg + number_to_string_2(size_dst, 7) + nickname_dest;
        
        sendto(client_socket, final_msg.data(), final_msg.size(), 0, (sockaddr*)&server_addr, sizeof(server_addr));
    }

    void Unicast_react(const std::string& buffer) {
        std::string size_str = buffer.substr(1, 7);
        int size_origin = std::atoi(size_str.c_str());
        
        std::string origin = buffer.substr(8, size_origin);
        
        size_str = buffer.substr(8 + size_origin, 5);
        int size_msg = std::atoi(size_str.c_str());
        
        std::string msg = buffer.substr(8 + size_origin + 5, size_msg);
        
        std::cout << "Message from: " << origin << " with a message of -> " << msg << std::endl;
    }

    void JSON_react(const std::string& buffer) {
        std::string size_str = buffer.substr(1, 5);
        int size_json = std::atoi(size_str.c_str());
        
        std::string json_str = buffer.substr(6, size_json);
        json js = json::parse(json_str);
        std::cout << js.dump(4) << std::endl;
    }

    void Send_File(int client_socket,sockaddr_in& server_addr,std::string& file_name,std::string& destination){
		std::string origin;
		std::cout << "Give your nickname" << std::endl;
		std::cin >> origin;
		
	    std::ifstream file(file_name,std::ios::binary);
	
	    if(!file.is_open()){
	        std::cout << "Could not open file" << std::endl;
	        return;
	    }
	
	    std::stringstream ss;
	    ss << file.rdbuf();
	    std::string complete_file = ss.str();
	
	    int max_content =Calculate_Max_Content(destination,file_name,origin);
	
	    int total_fragments =(complete_file.size() + max_content - 1)/ max_content;

		SentFile sf;
		sf.total_fragments = total_fragments;
		sf.file_size = complete_file.size();
		sf.packets.resize(total_fragments);
		sf.acked.resize(total_fragments,false);
		
	    for(int i=0;i<total_fragments;i++){
	        int start =i*max_content;
	
	        int current_size =std::min(max_content,(int)complete_file.size()-start);
	        std::string fragment =complete_file.substr(start,current_size);
	
	        char checksum =Calculate_Checksum(fragment);
	
	        std::string packet;
	
	        packet += number_to_string_2(total_fragments,2);
	        packet += number_to_string_2(i+1,2);
	        packet += number_to_string_2(global_seq,4);
	        packet += 'F';
	        packet += number_to_string_2(destination.size(),5);
	        packet += destination;
	        packet += number_to_string_2(file_name.size(),3);
	        packet += file_name;
	        packet += number_to_string_2(origin.size(),5);
	        packet += origin;
	        packet += number_to_string_2(i+1,12);
	        packet += number_to_string_2(fragment.size(),22);
	        packet += fragment;
	        packet.push_back(checksum);
	
	        while(packet.size() < 500){
	            packet.push_back('#');
			}
			
			std::cout << "Sending from -> " << origin << " to " << destination << " with the datagram format of" << std::endl;
			//std::cout << packet << std::endl;
			sf.packets[i] = packet;
	        sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));
	    }
		sent_files[global_seq] = std::move(sf);
		/*SentFile& current_file = sent_files[global_seq];
		for(int i=0;i<total_fragments;i++){
			std::cout << "[SEND] seq=" << global_seq << " i=" << i << " packets.size=" << current_file.packets.size() << std::endl;
			
		    waiting_ACK = true;
		    sendto(client_socket,current_file.packets[i].data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));
		    auto starting = std::chrono::steady_clock::now();
		    while(waiting_ACK){
			    auto now = std::chrono::steady_clock::now();
			    auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-starting).count();
			    if(elapsed > 1000){
			        std::cout << "TIMEOUT -> retransmitting fragment " << i+1<< std::endl;
					sendto(client_socket,current_file.packets[i].data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));
			        starting = std::chrono::steady_clock::now();
			    }
			}
		}*/

		
	}

    void File_read(const std::string& buffer){
	    int pos = 0;
		
	    int total_fragments=std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;

	    int current_fragment=std::atoi(buffer.substr(pos,2).c_str());
	    pos += 2;
	
	    int global_seq=std::atoi(buffer.substr(pos,4).c_str());
	    pos += 4;
	
	    char type=buffer[pos++];
	    int size_dest=std::atoi(buffer.substr(pos,5).c_str());
	    pos += 5;
	
	    std::string destination=buffer.substr(pos,size_dest);
	    pos += size_dest;
	
	    int size_file_name=std::atoi(buffer.substr(pos,3).c_str());
	    pos += 3;
	
	    std::string file_name=buffer.substr(pos,size_file_name);
	    pos += size_file_name;
	
	    int size_origin=std::atoi(buffer.substr(pos,5).c_str());
	    pos += 5;
	
	    std::string origin=buffer.substr(pos,size_origin);
	    pos += size_origin;
	
	    long long fragment_seq=std::atoll(buffer.substr(pos,12).c_str());
	    pos += 12;
	
	    long long size_content=std::atoll(buffer.substr(pos,22).c_str());
	    pos += 22;
	
	    std::string content=buffer.substr(pos,size_content);
	    pos += size_content;
	
	
	    if(pending_files.find(global_seq)==pending_files.end()){
	        FileAssembly assembly;
	        assembly.total_fragments=total_fragments;
	        assembly.file_name=file_name;
	        assembly.origin=origin;
	
	        assembly.fragments.resize(total_fragments);
	        assembly.received.resize(total_fragments,false);
	
	        pending_files[global_seq]=std::move(assembly);
	    }
	
	    FileAssembly& assembly=pending_files[global_seq];
	
	    assembly.fragments[current_fragment-1]=content;
	    assembly.received[current_fragment-1]=true;
	    bool complete = true;
	
	    for(bool received :assembly.received){
	        if(!received){
	            complete = false;
	            break;
	        }
	    }
	
	    if(!complete){
	        return;
	    }
	
	    std::ofstream ofs("2_" + assembly.file_name,std::ios::binary);
	
	    for(auto& fragment: assembly.fragments){
	        ofs.write(fragment.data(),fragment.size());
	    }
	    ofs.close();
	
	    std::cout << "File received -> " << assembly.file_name << " from -> " << assembly.origin << " with the datagram format of" << std::endl;
		std::cout << buffer << std::endl;
		
	    pending_files.erase(global_seq);
	}

    void Cases_Client_UDP(char type,const std::string& buffer, int client_socket, sockaddr_in& server_addr) {
        switch (type) {
            case 'L': {
                Login(client_socket, server_addr);
                break;
            }
            case 'O': {
                char O = 'O';
                logging_status = false;
                running = false;
                sendto(client_socket, &O, 1, 0, (sockaddr*)&server_addr, sizeof(server_addr));
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
                char T = 'T';
                sendto(client_socket, &T, 1, 0, (sockaddr*)&server_addr, sizeof(server_addr));
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
				if(buffer.size()==1){
					std::cout << "All good OK " << std::endl;
	                if (logging_status == true) {
	                    logging_status = false;
	                    running = false;
	                } else {
	                    logging_status = true;
	                }
	                break;
				}
				std::cout << "[ACK] -> ";
				NACKACK_read(buffer,client_socket,server_addr,true,waiting_ACK,sent_files);
                break;
            }
			case 'k':{
				std::cout << "[NACK] -> ";
				NACKACK_read(buffer,client_socket,server_addr,false,waiting_ACK,sent_files);
				break;
			}
            case 'E': {
                Error(buffer);
                break;
            }
            case 'b': {
                Broadcast_react(buffer);
                break;
            }
            case 'u': {
                Unicast_react(buffer);
                break;
            }
            case 't': {
                JSON_react(buffer);
                break;
            }
            case 'f': {
                File_read(buffer);
                break;
            }
            default: {
                std::cout << "This protocol is not registered in Client :( " << std::endl;
                break;
            }
        }
    }
};
