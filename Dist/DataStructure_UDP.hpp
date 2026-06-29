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
#define TIMEOUT_TIME 1000


/* Function 01: to map a number to his corresponding bytes, for example 2 bytes for 5 is 05*/
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

/* Function 02: checksum with 1 byte with offset in range [0-8]?*/
char Calculate_Checksum(std::string content){
    int sum = 0;
    for(unsigned char c : content){
        sum += c;
	}

    return static_cast<char>(sum % 9) + '0';
}

/* Function 03: Get the format with IP:Port for the string name of the client (I USED THE ONE FROM THE TEACHER :D)*/
std::string GetSenderKey(sockaddr_in& addr){
    return std::string(inet_ntoa(addr.sin_addr)) + ":" + std::to_string(ntohs(addr.sin_port));
}


/* Struct 01: struct basic in datagram level with all logic for the fragments and times*/
struct SentFile{
    int total_fragments;
	long long matrix_size;
    std::vector<std::string> packets;
	std::vector<std::string> payloads;
    std::vector<bool> acked;
	std::vector<bool> retransmitted;
	std::vector<int> retries;
    std::vector<std::chrono::steady_clock::time_point> last_activity;

};

/* Struct 02: struct basic in client level with all logic for the datagrams and times*/
struct ClientInfo{
	sockaddr_in addr;
	std::map<int,SentFile> client_datagrams;
	bool first_rtt_sample = true;
	double EstRTT = 1000.0;
	double Dev = 0.0;
	double Timeout = 1000.0;
	double delta=1.0/8.0;
	double mu = 1.0;
	double fi = 4.0;
};

/* Struct 03: struct for the protocol with construction and parsing of the datagrams*/
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
	char Calculate_Checksum_Fragments(const std::string& packet){
		return Calculate_Checksum(packet.substr(HEADER_SIZE,packet.size()-HEADER_SIZE));
	}

	bool ParseProtocol(const std::string& buffer,bool &checksum_error,char check_proto){
		int pos = 0;
		this->hash=buffer[0];
	   	pos += 1;
		this->datagram_id = std::atoi(buffer.substr(pos,4).c_str());
		pos += 4;
		this->total_packets = std::atoi(buffer.substr(pos,4).c_str());
		pos += 4;
		this->seq_number = std::atoi(buffer.substr(pos,5).c_str());
		pos += 5;

		char calculated = Calculate_Checksum_Fragments(buffer);
	    if(this->hash != calculated){
			std::string error_msg = "[WARNING] CHECKSUM";
			std::cout << error_msg << std::endl;
			checksum_error=true;
			return false;
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

/* Function 04: funciton to send ack to their destination*/
void Send_ACK(const ProtocolFormat &proto,int socket,sockaddr_in& addr){
 	ProtocolFormat protocol{'0',proto.datagram_id,1,proto.seq_number,'A',0,"",0,""};
	std::string packet = protocol.ConstructDatagram();
	while((int)packet.size() < DATAGRAM_SIZE){
		packet.push_back('#');
	}
	packet[0]=protocol.hash=protocol.Calculate_Checksum_Fragments(packet);
	sendto(socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&addr,sizeof(addr));
	std::cout << "[SENDING ACK] confirmation for datagram: " << protocol.datagram_id << " and fragment: " << protocol.seq_number << std::endl;
}

/* Function 05: function to send nack to their destination*/
void Send_NACK(const ProtocolFormat &proto,int socket,sockaddr_in& addr){
 	ProtocolFormat protocol{'0',proto.datagram_id,1,proto.seq_number,'N',0,"",0,""};
	std::string packet = protocol.ConstructDatagram();
	while((int)packet.size() < DATAGRAM_SIZE){
		packet.push_back('#');
	}
	packet[0]=protocol.hash=protocol.Calculate_Checksum_Fragments(packet);
	sendto(socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&addr,sizeof(addr));
	std::cout << "[SENDING NACK] for datagram: " << protocol.datagram_id << " and fragment: " << protocol.seq_number << std::endl;
}

/* Function 06: function to parse the ack to their destination and also update dynamically update the Timeout*/
void Parse_ACK(const std::string &buffer, ClientInfo& ci,std::mutex &mtx){
	std::lock_guard<std::mutex> lock(mtx);
 	ProtocolFormat protocol;
	bool checksum_error=false;
	if(!protocol.ParseProtocol(buffer,checksum_error,'A')){
		std::cout << "Datagram failed in parsing" << std::endl;
		return;
	}
	auto it = ci.client_datagrams.find(protocol.datagram_id);
	if(it == ci.client_datagrams.end()){
		std::cout << "Datagram failed in searching datagram existence" << std::endl;
		return;
	}
	auto now = std::chrono::steady_clock::now();
	if(!it->second.retransmitted[protocol.seq_number]){
		double SampleRTT =std::chrono::duration_cast<std::chrono::milliseconds>(now-it->second.last_activity[protocol.seq_number]).count();
		
		if(ci.first_rtt_sample){
			ci.EstRTT= SampleRTT;
			ci.Dev=SampleRTT/2.0;
			ci.first_rtt_sample=false;	
		}else{
			double Diff = SampleRTT - ci.EstRTT;
			ci.EstRTT= ci.EstRTT + ci.delta*Diff;
			ci.Dev=ci.Dev+ci.delta*(std::abs(Diff)-ci.Dev);
		}
		ci.Timeout = ci.mu*ci.EstRTT+ci.fi*ci.Dev;
		std::cout << "[DYNAMIC TIMEOUT] Updated to " << ci.Timeout << std::endl;
	}
	it->second.retransmitted[protocol.seq_number] = false;
	if(protocol.seq_number >= 0 && protocol.seq_number < (int)it->second.acked.size()){
		//std::cout << "[ACK] confirmed :D for datagram: " << protocol.datagram_id << " and fragment: " << protocol.seq_number << std::endl;
 		it->second.acked[protocol.seq_number] = true;
	}

	bool all =
    std::all_of(it->second.acked.begin(),it->second.acked.end(),[](bool b){ return b; });
    if(all){
		std::cout << "[ACK] confirmed all ARRIVED: " << protocol.datagram_id << std::endl;
        ci.client_datagrams.erase(it);
    }
}

/* Function 07: function to parse the nack to their destination and also retransmit the datagram*/
void Parse_NACK(const std::string &buffer, ClientInfo& ci,int &socket,sockaddr_in& addr,std::mutex &mtx){
 	std::lock_guard<std::mutex> lock(mtx);
	ProtocolFormat protocol;
	bool checksum_error=false;
	if(!protocol.ParseProtocol(buffer,checksum_error,'N')){
		return;
	}
	auto it = ci.client_datagrams.find(protocol.datagram_id);
	if(it == ci.client_datagrams.end()){
		return;
	}
	SentFile& file = it->second;
	if(protocol.seq_number < 0 || protocol.seq_number >= (int)file.packets.size()){
	   	return;
	}
	file.retries[protocol.seq_number]++;

    if(file.retries[protocol.seq_number] > 5){
        std::cout << "[ERROR] Max retries reached on packet " << protocol.seq_number << std::endl;
        return;
    }
    std::cout << "[RETRANSMIT NACK] Datagram " << protocol.datagram_id << " packet " << protocol.seq_number << std::endl;
	file.retransmitted[protocol.seq_number] = true;
	file.last_activity[protocol.seq_number] =std::chrono::steady_clock::now();
	sendto(socket,file.packets[protocol.seq_number].data(),DATAGRAM_SIZE,0,(sockaddr*)&addr,sizeof(addr));
}

/* Function 08: function to send ok xd*/
void Send_OK(int socket,sockaddr_in& addr){
	ProtocolFormat protocol{'0',0,1,0,'K',0,"",0,""};
    std::string packet = protocol.ConstructDatagram();

    while(packet.size() < DATAGRAM_SIZE){
        packet.push_back('#');
	}

    packet[0] = protocol.Calculate_Checksum_Fragments(packet);

    sendto(socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&addr,sizeof(addr));
}

/* Function 09: function to send error xd*/
void Send_Error(int socket,sockaddr_in& addr,const std::string& msg){
	ProtocolFormat protocol{'0',0,1,0,'E',0,"",(int)msg.size(),msg};

    std::string packet = protocol.ConstructDatagram();

    while(packet.size() < DATAGRAM_SIZE){
        packet.push_back('#');
	}

    packet[0] = protocol.Calculate_Checksum_Fragments(packet);

    sendto(socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&addr,sizeof(addr));
}

/* Function 10: function to check all timeout logic PER CLIENT and also check if the datagram was retransmitted more than 5 times descarded*/
void CheckTimeouts(ClientInfo& ci,int socket,sockaddr_in& addr){
    auto now = std::chrono::steady_clock::now();
	int amount_retransmitted=0;
    for(auto& datagram : ci.client_datagrams){
        SentFile& file = datagram.second;
        for(int i=0;i<(int)file.acked.size();i++){
            if(file.acked[i]){
                continue;
            }
			auto elapsed =std::chrono::duration_cast<std::chrono::milliseconds>(now - file.last_activity[i]).count();
			//std::cout << "Elapsed time -> " << elapsed << std::endl;
			std::cout << "[DEBUG] Paquete " << i << " | Tiempo transcurrido: " << elapsed << " ms"
			<< " | Tiempo configurado: " << ci.Timeout << " ms" << std::endl;
			double effective_timeout=ci.Timeout;
			if(ci.first_rtt_sample){
				effective_timeout=ci.Timeout*(1+file.retries[i]);
			}
			if(elapsed < effective_timeout){
				continue;
			}
            file.retries[i]++;
			file.retransmitted[i] = true;
			std::cout<< "[TIMEOUT] Datagram "<< datagram.first<< " fragment "<< i << std::endl;
			//std::cout << "RESENDING " << file.packets[i] << " with size: " << file.packets[i].size() << std::endl;
            sendto(socket,file.packets[i].data(),DATAGRAM_SIZE,0,(sockaddr*)&addr,sizeof(addr));
			file.last_activity[i] = now;
			amount_retransmitted++;
			if(amount_retransmitted >= 20){
				return;
			}
			std::this_thread::sleep_for(std::chrono::microseconds(300));
        }

    }
}

/* Function 11: print :D*/
void print(const std::unordered_map<std::string,sockaddr_in>& clients){
	std::cout << "================================" << std::endl;
	for(const auto& client : clients){
	    std::cout << "ID: " << client.first << std::endl;
	}
	std::cout << "================================" << std::endl;
}

/* Class 01: class for all the server protocols that is read the csv, login,logout, receive response of matrix from clients*/
class Server_Protocols_UDP {
public:
	std::unordered_map<std::string,sockaddr_in> client_map; // mapping the IP:Port to the user sockaddr_in
	std::unordered_map<std::string, ClientInfo> pending_transfers; // All the transfers done by the multiple clients
	std::atomic<int> actual_datagram_id=0; // global counter for datagrams
	std::mutex mtx;
public:

    // Read and divide the csv
	void Raw_Matrix_file(int server_socket){
		std::string weights_path = "weights.csv";
		std::string data_path = "dataset.csv";

		//std::cout << "Give me the path to csv -> ";
		//std::getline(std::cin,path);

		std::ifstream weights_reader(weights_path);
		if(!weights_reader.is_open()){
			std::cout << "Couldn't open file\n";
			return;
		}
		std::stringstream weights_buffer;
        weights_buffer << weights_reader.rdbuf();
        std::string weights = weights_buffer.str();
        weights_reader.close();


		std::ifstream data_reader(data_path);
		if(!data_reader.is_open()){
			std::cout << "Couldn't open file\n";
			return;
		}
		std::vector<std::string> lines;
		std::string line;
		while(std::getline(data_reader,line)){
			lines.push_back(line + '\n');
		}
		data_reader.close();




		int total_lines = lines.size();
		int num_clients = client_map.size();

		if(num_clients == 0) {
			std::cout << "No clients connected to distribute the matrix\n";
			return;
		}

		std::cout << "Total matrix lines: " << total_lines << " | Distributing among " << num_clients << " clients." << std::endl;

		// Division
		int lines_per_client = total_lines / num_clients;
		int extra_lines = total_lines % num_clients;

		int current_line_idx = 0;
		int client_idx = 0;

		for(const auto& client : client_map){
			int assigned_lines = lines_per_client + (client_idx < extra_lines ? 1 : 0);

			std::string client_matrix_batch = "";
			for(int i = 0; i < assigned_lines; ++i) {
				client_matrix_batch += lines[current_line_idx++];
			}

			std::string total_payload =  weights + "|" + client_matrix_batch;

			std::cout << "Client [" << client.first << "] gets " << assigned_lines
              << " lines + Weights. Total Payload size -> " << total_payload.size() << " bytes." << std::endl;

			int datagram_id = actual_datagram_id++;
			int seq = 0;
			long long header = HEADER_SIZE + 3 + 0 + 20;
			long long max_content = DATAGRAM_SIZE - header;
			long long total_fragments = (total_payload.size() + max_content - 1) / max_content;
			int start = 0;

			std::string sender = GetSenderKey(const_cast<sockaddr_in&>(client.second));
			pending_transfers[sender].addr = client.second;
			auto& file = pending_transfers[sender].client_datagrams[datagram_id];

			file.total_fragments = total_fragments;
			file.matrix_size = total_payload.size();
			file.packets.resize(total_fragments);
			file.payloads.resize(total_fragments);
			file.acked.resize(total_fragments, false);
			file.retransmitted.resize(total_fragments, false);
			file.retries.resize(total_fragments, 0);
			file.last_activity.resize(total_fragments);

			for(int i = 0; i < total_fragments; i++){
				long long frag_size = std::min(max_content, (long long)total_payload.size() - start);
				std::string fragment = total_payload.substr(start, frag_size);
				ProtocolFormat protocol{'0', datagram_id, (int)total_fragments, seq++, 'M', 0, "", (long long)total_payload.size(), fragment};
				std::string packet = protocol.ConstructDatagram();

				while(packet.size() < DATAGRAM_SIZE){
					packet.push_back('#');
				}

				packet[0] = protocol.Calculate_Checksum_Fragments(packet);
				file.packets[i]  = packet;
				file.payloads[i] = fragment;
				file.last_activity[i] = std::chrono::steady_clock::now();

				sendto(server_socket, packet.data(), DATAGRAM_SIZE, 0, (sockaddr*)&client.second, sizeof(client.second));
				std::this_thread::sleep_for(std::chrono::microseconds(200));
				start += frag_size;
			}

			client_idx++;
		}
	}

	// For the login of the user
    std::string Login(const std::string& buffer, int server_socket, sockaddr_in& client_addr) {
		ProtocolFormat proto;
		bool checksum_error=false;
		if(!proto.ParseProtocol(buffer,checksum_error,'L')){
			if(checksum_error){
				Send_NACK(proto,server_socket,client_addr);
			}
			return "";
		}

		std::lock_guard<std::mutex> lock(mtx);
		std::string sender = GetSenderKey(client_addr);
		pending_transfers[sender].addr = client_addr;
        if(pending_transfers[sender].client_datagrams.find(proto.datagram_id) == pending_transfers[sender].client_datagrams.end()){
            pending_transfers[sender].client_datagrams[proto.datagram_id].total_fragments = proto.total_packets;
            pending_transfers[sender].client_datagrams[proto.datagram_id].matrix_size = proto.nickname_size;
            pending_transfers[sender].client_datagrams[proto.datagram_id].packets.resize(proto.total_packets);
			pending_transfers[sender].client_datagrams[proto.datagram_id].payloads.resize(proto.total_packets);
            pending_transfers[sender].client_datagrams[proto.datagram_id].acked.resize(proto.total_packets,false);
			pending_transfers[sender].client_datagrams[proto.datagram_id].retransmitted.resize(proto.total_packets,false);
			pending_transfers[sender].client_datagrams[proto.datagram_id].retries.resize(proto.total_packets,0);
			pending_transfers[sender].client_datagrams[proto.datagram_id].last_activity.resize(proto.total_packets);
        }

		pending_transfers[sender].client_datagrams[proto.datagram_id].packets[proto.seq_number]=buffer;
		pending_transfers[sender].client_datagrams[proto.datagram_id].payloads[proto.seq_number]=proto.nickname;
		pending_transfers[sender].client_datagrams[proto.datagram_id].acked[proto.seq_number]=true;
		pending_transfers[sender].client_datagrams[proto.datagram_id].last_activity[proto.seq_number]=std::chrono::steady_clock::now();



	   	std::cout << "===================================================================" << std::endl;
	   	std::cout << "Server received datagram # " << proto.seq_number << " with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;

		SentFile &file = pending_transfers[sender].client_datagrams[proto.datagram_id];
		bool all_acked = std::all_of(file.acked.begin(), file.acked.end(), [](bool b){ return b; });
		Send_ACK(proto,server_socket,client_addr);
		if(all_acked){
			std::string assembled;
            long long written = 0;
            for(const auto &frag : file.payloads){
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
			Send_OK(server_socket, client_addr);
            print(client_map);
		}

        return proto.nickname;
    }

	// For the logout
	void Logout(const std::string& buffer,int server_socket,sockaddr_in& client_addr){
	    ProtocolFormat proto;
		bool checksum_error=false;
		if(!proto.ParseProtocol(buffer,checksum_error,'O')){
			if(checksum_error){
				Send_NACK(proto,server_socket,client_addr);
			}
			return;
		}
		std::lock_guard<std::mutex> lock(mtx);
		std::string sender = GetSenderKey(client_addr);
		pending_transfers[sender].addr = client_addr;
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
		Send_ACK(proto,server_socket,client_addr);
		Send_OK(server_socket, client_addr);
	    print(client_map);
	}

	// To process all the fragments received from the multples slaves
	void Processed_Matrix(const std::string& buffer,int server_socket,sockaddr_in& client_addr){
		ProtocolFormat proto;
		bool checksum_error=false;
		if(!proto.ParseProtocol(buffer,checksum_error,'P')){
			if(checksum_error){
				Send_NACK(proto,server_socket,client_addr);
			}
			return;
		}

		std::lock_guard<std::mutex> lock(mtx);
		std::string sender =GetSenderKey(client_addr);
		pending_transfers[sender].addr = client_addr;
		if(pending_transfers[sender].client_datagrams.find(proto.datagram_id)==pending_transfers[sender].client_datagrams.end()){
			pending_transfers[sender].client_datagrams[proto.datagram_id].total_fragments = proto.total_packets;
			pending_transfers[sender].client_datagrams[proto.datagram_id].matrix_size = proto.matrix_size;
			pending_transfers[sender].client_datagrams[proto.datagram_id].packets.resize(proto.total_packets);
			pending_transfers[sender].client_datagrams[proto.datagram_id].payloads.resize(proto.total_packets);
			pending_transfers[sender].client_datagrams[proto.datagram_id].acked.resize(proto.total_packets,false);
			pending_transfers[sender].client_datagrams[proto.datagram_id].retransmitted.resize(proto.total_packets,false);
			pending_transfers[sender].client_datagrams[proto.datagram_id].retries.resize(proto.total_packets,0);
			pending_transfers[sender].client_datagrams[proto.datagram_id].last_activity.resize(proto.total_packets);
		}

		auto& file =pending_transfers[sender].client_datagrams[proto.datagram_id];
		file.packets[proto.seq_number] =buffer;
		file.payloads[proto.seq_number] =proto.matrixcontent;
		file.acked[proto.seq_number] = true;
		file.last_activity[proto.seq_number] = std::chrono::steady_clock::now();
		Send_ACK(proto,server_socket,client_addr);

		int received_count=std::count(file.acked.begin(),file.acked.end(),true);
		std::cout << "[PROGRESS] Client " << sender << " datagram " << proto.datagram_id
		<< " -> fragment " << proto.seq_number << " | " << received_count << "/" << file.total_fragments
		<< " received" << std::endl; 

		bool all =std::all_of(file.acked.begin(),file.acked.end(),[](bool b){ return b; });
		if(!all){
			return;
		}

		std::string result;
		long long written = 0;

		for(const auto& frag : file.payloads){
			long long remaining =file.matrix_size - written;
			long long take =std::min((long long)frag.size(),remaining);

			result += frag.substr(0,take);
			written += take;
		}

		std::cout<< "===================================="<< std::endl;
		std::cout << "RESULT FROM -> "<< sender << std::endl;
		std::cout << result << std::endl;
		std::cout << "====================================" << std::endl;




		std::string client_final_name = "unknown_client";

		for (const auto& pair : client_map) {
			if (pair.second.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
			    pair.second.sin_port == client_addr.sin_port) {
				client_final_name = pair.first;
				break;
			}
		}

		std::string output_filename = "returned_" + client_final_name + "_weight_received.csv";

		std::ofstream csv_file(output_filename);
		if (csv_file.is_open()) {
			csv_file << result;
			csv_file.close();
			std::cout << "[SUCCESS] Saved final client weights into: " << output_filename << std::endl;
		} else {
			std::cout << "[ERROR] Could not open or create file: " << output_filename << std::endl;
		}


		pending_transfers[sender].client_datagrams.erase(proto.datagram_id);
	}

	// Dedicated timeout checking for the thread server
	void TimeoutThread_Server(Server_Protocols_UDP* sv, int socket) {
	    while (true) {
			// Some sleep to prevent instatineous in the first fragment
	        std::this_thread::sleep_for(std::chrono::milliseconds(50));
	        std::lock_guard<std::mutex> lock(sv->mtx);
	        for(auto& pair : sv->pending_transfers){
			    ClientInfo& ci = pair.second;

			    CheckTimeouts(ci, socket, ci.addr);
			}
	    }
	}


	// All the possible cases to facilitate the parsing
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
			case 'P':{
				Processed_Matrix(buffer,server_socket,client_addr);
				break;
			}
			case 'A':{
				Parse_ACK(buffer,pending_transfers[GetSenderKey(client_addr)],mtx);
				break;
			}
			case 'N':{
				Parse_NACK(buffer,pending_transfers[GetSenderKey(client_addr)],server_socket,client_addr,mtx);
				break;
			}
            default: {
				//std::cout << " Got sent -> " << buffer << std::endl;
                //std::cout << "This protocol is not registered in Server :( " << std::endl;
                break;
            }
        }
    }
};

/* Class 02: Class for all the client protocols like error parsing, login, logout and reciving the datagrams and functions to resend to master*/
class Client_Protocols_UDP {
public:
    std::atomic<bool> running; // For running the program
	std::atomic<bool> logging_status; // For logging
	std::atomic<int> actual_datagram_id = 0; // Global to know the datagram id
	std::string final_name,pending_name; // Logging name logic
	std::mutex mtx; // Mutex
	ClientInfo pending_transfers; // Transfers for a specific client
	ClientInfo received_transfers;
	bool received = false;

public:
	// Error logic
    void Error(const std::string& buffer) {
        ProtocolFormat proto;
		bool checksum_error=false;
		if(!proto.ParseProtocol(buffer,checksum_error,'E')){
			return;
		}


		std::cout << "===================================================================" << std::endl;
	   	std::cout << "Client received error with the content | " << buffer << std::endl;
	   	std::cout << "===================================================================" << std::endl;

	    std::cout<< "ERROR -> "<< proto.matrixcontent << std::endl;
    }

	// Logging logic
    void Login(int client_socket, sockaddr_in& server_addr) {
		std::string name;
        std::cout << "Give me your nickname to send -> ";
        std::getline(std::cin, name);
		pending_name=name;

		int seq_numbers{0};

	    // First fragment
		int header= HEADER_SIZE+3+pending_name.size()+20+0;
		int remaining_size_first= DATAGRAM_SIZE-header;
		int current_size = std::min(remaining_size_first,(int)pending_name.size());

	    int total_remaining = (int)pending_name.size() - current_size;
	    int max_content = DATAGRAM_SIZE - header;
	    int extra_fragments = (total_remaining + max_content - 1) / max_content;
	    int total_fragments = 1 + extra_fragments;


		ProtocolFormat protocol{'0',actual_datagram_id,total_fragments,seq_numbers++,'L',(int)pending_name.size(),pending_name.substr(0,current_size),0,""};

		std::string packet=protocol.ConstructDatagram();

		while((int)packet.size() < DATAGRAM_SIZE){
			packet.push_back('#');
		}
		packet[0]=protocol.hash=protocol.Calculate_Checksum_Fragments(packet);

		auto& file = pending_transfers.client_datagrams[actual_datagram_id];
		file.total_fragments = total_fragments;
		file.matrix_size = pending_name.size();
		file.packets.resize(total_fragments);
		file.payloads.resize(total_fragments);
		file.acked.resize(total_fragments,false);
		file.retransmitted.resize(total_fragments,false);
		file.retries.resize(total_fragments,0);
		file.last_activity.resize(total_fragments);

		std::cout << "=======================================================" << std::endl;
		std::cout << "Client Sending from -> " << protocol.nickname << " to server with the datagram format of" << std::endl;
		std::cout << packet << std::endl;
		std::cout << "=======================================================" << std::endl;

		file.packets[0] = packet;
		file.last_activity[0] = std::chrono::steady_clock::now();
		sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

		int start = current_size;
	    for(int i=1;i<total_fragments;i++){
	        int frag_size =std::min(max_content,(int)pending_name.size()-start);
	        std::string fragment =pending_name.substr(start,frag_size);

			ProtocolFormat protocol_normal{'0',actual_datagram_id,total_fragments,seq_numbers++,'L',(int)pending_name.size(),fragment,0,""};

			std::string packet_2=protocol_normal.ConstructDatagram();
			while((int)packet_2.size() < DATAGRAM_SIZE){
				packet_2.push_back('#');
			}

	        packet_2[0]=protocol_normal.hash=protocol_normal.Calculate_Checksum_Fragments(packet_2);

			std::cout << "=======================================================" << std::endl;
			std::cout << "Client Sending ----> Fragment #" << i+1 << " | " << packet_2 << std::endl;
			std::cout << "=======================================================" << std::endl;
			file.packets[i] = packet_2;
			file.payloads[i] = fragment;
			file.last_activity[i] = std::chrono::steady_clock::now();
	        sendto(client_socket,packet_2.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

			start += frag_size;
	    }
        actual_datagram_id++;
    }

	// To response doing something and resending to the master
	void Broadcast_Response(const std::string& filename,int client_socket,sockaddr_in& server_addr){
	    std::ifstream file_reader(filename);
		if (!file_reader.is_open()) {
			std::cout << "[ERROR] Broadcast_Response: Could not open file " << filename << std::endl;
			Send_Error(client_socket, server_addr, "ERROR: Client file could not be opened");
			return;
		}

		std::stringstream buffer_stream;
		buffer_stream << file_reader.rdbuf();
		std::string result = buffer_stream.str();
		file_reader.close();


		int seq = 0;

		long long header = HEADER_SIZE+3+0+20;
		long long max_content = DATAGRAM_SIZE - header;
		long long total_fragments =(result.size()+max_content-1)/max_content;

		int start = 0;
		auto& file = pending_transfers.client_datagrams[actual_datagram_id];
		file.total_fragments = total_fragments;
		file.matrix_size = result.size();
		file.packets.resize(total_fragments);
		file.payloads.resize(total_fragments);
		file.acked.resize(total_fragments,false);
		file.retransmitted.resize(total_fragments,false);
		file.retries.resize(total_fragments,0);
		file.last_activity.resize(total_fragments);

		for(int i=0;i<total_fragments;i++){
			long long frag_size =std::min(max_content,(long long)result.size()-start);
			std::string fragment =result.substr(start,frag_size);
			std::cout << "=======================================================" << std::endl;
			std::cout << "Client Sending Matrix ----> Fragment #" << i+1 << " | " << fragment << std::endl;
			std::cout << "=======================================================" << std::endl;
			ProtocolFormat protocol{'0',actual_datagram_id,(int)total_fragments,seq++,'P',0,"",(long long)result.size(),fragment};
			std::string packet =protocol.ConstructDatagram();

			while(packet.size() < DATAGRAM_SIZE){
				packet.push_back('#');
			}

			packet[0] =protocol.Calculate_Checksum_Fragments(packet);
			file.packets[i] = packet;
			file.payloads[i] = fragment;
			file.last_activity[i] = std::chrono::steady_clock::now();
			sendto(client_socket,packet.data(),DATAGRAM_SIZE,0,(sockaddr*)&server_addr,sizeof(server_addr));

			start += frag_size;
		}
		actual_datagram_id++;
	}

	// Receiving the matrix from the master
	void Matrix_react(const std::string& buffer,int client_socket,sockaddr_in& server_addr){
		/*static bool first_time=true;
		if(first_time){
			std::cout << "SLEEPING ZZZZ" << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(2000));
			first_time=false;
		}*/
		std::lock_guard<std::mutex>lock(mtx);
		ProtocolFormat proto;
		bool checksum_error=false;
		if(!proto.ParseProtocol(buffer,checksum_error,'M')){
			if(checksum_error){
				Send_NACK(proto,client_socket,server_addr);
			}
			return;
		}
		if(received_transfers.client_datagrams.find(proto.datagram_id)== received_transfers.client_datagrams.end()){
			received_transfers.client_datagrams[proto.datagram_id].total_fragments = proto.total_packets;
			received_transfers.client_datagrams[proto.datagram_id].matrix_size = proto.matrix_size;
			received_transfers.client_datagrams[proto.datagram_id].packets.resize(proto.total_packets);
			received_transfers.client_datagrams[proto.datagram_id].payloads.resize(proto.total_packets);
			received_transfers.client_datagrams[proto.datagram_id].acked.resize(proto.total_packets,false);
			received_transfers.client_datagrams[proto.datagram_id].retransmitted.resize(proto.total_packets,false);
			received_transfers.client_datagrams[proto.datagram_id].retries.resize(proto.total_packets,0);
			received_transfers.client_datagrams[proto.datagram_id].last_activity.resize(proto.total_packets);
		}

		std::cout << "=======================================================" << std::endl;
		std::cout << "Client Receiving Matrix ----> Fragment #" << proto.seq_number << " | " << buffer << std::endl;
		std::cout << "=======================================================" << std::endl;

		auto& file =received_transfers.client_datagrams[proto.datagram_id];
		file.packets[proto.seq_number] =buffer;
		file.payloads[proto.seq_number] =proto.matrixcontent;
		file.acked[proto.seq_number] = true;
		file.last_activity[proto.seq_number] = std::chrono::steady_clock::now();

		Send_ACK(proto,client_socket,server_addr);
		int received_count=std::count(file.acked.begin(),file.acked.end(),true);
		std::cout << "[PROGRESS] Client datagram " << proto.datagram_id
		<< " -> fragment " << proto.seq_number << " | " << received_count << "/" << file.total_fragments
		<< " received" << std::endl; 

		bool all =std::all_of(file.acked.begin(),file.acked.end(),[](bool b){ return b; });

		if(!all){
			return;
		}


		std::string total_payload;

		long long written = 0;

		for(const auto& frag : file.payloads){
			long long remaining = file.matrix_size - written;
			long long take = std::min((long long)frag.size(),remaining);
			total_payload += frag.substr(0,take);
			written += take;
		}



        size_t first_pipe = total_payload.find('|');
        if (first_pipe == std::string::npos) {
            std::cout << "[ERROR] Formato de payload inválido." << std::endl;
            return;
        }

        // El string de pesos es todo lo que está antes del primer '|'
        std::string weights_text = total_payload.substr(0, first_pipe);
        // El lote de la matriz empieza justo después del primer '|'
        std::string matrix_text = total_payload.substr(first_pipe + 1);


        std::cout << "=======================================================" << std::endl;
        std::cout << "Clasificador (Pesos) recibidos con éxito: " << weights_text << std::endl;
        std::cout << "Tamaño de la Matriz del Cliente: " << matrix_text.size() << " bytes." << std::endl;
        std::cout << "=======================================================" << std::endl;


        std::string batch_filename = final_name + "_batch_sc.csv";
        std::ofstream matrix_writer(batch_filename);
        if (matrix_writer.is_open()) {
            matrix_writer << matrix_text;
            matrix_writer.close();
        }

        std::string weight_filename = final_name + "_weight_sc.csv";
        std::ofstream weight_writer(weight_filename);
        if (weight_writer.is_open()) {
            weight_writer << weights_text;
            weight_writer.close();
        }

        received_transfers.client_datagrams.erase(proto.datagram_id);

        received = true;

        std::cout << "Cliente recibió: " << weight_filename << " y " << batch_filename << "\n";

		//std::string result = matrix_text;

		//Broadcast_Response(weight_filename,client_socket,server_addr);
	}

	// Timeout exclusively for the client thread
	void TimeoutThread_Client(Client_Protocols_UDP* cl, int socket, sockaddr_in server_addr) {
	    while (cl->running) {
	        std::this_thread::sleep_for(std::chrono::milliseconds(50));
	        std::lock_guard<std::mutex> lock(cl->mtx);
	        CheckTimeouts(cl->pending_transfers, socket, server_addr);
	    }
	}

	// All the possible cases to facilitate the parsing
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
			case 'M':{
				Matrix_react(buffer,client_socket,server_addr);
				break;
			}
			case 'A':{
				Parse_ACK(buffer,pending_transfers,mtx);
				break;
			}
			case 'N':{
				Parse_NACK(buffer,pending_transfers,client_socket,server_addr,mtx);
				break;
			}
            default: {
                //std::cout << "This protocol is not registered in Client :( " << std::endl;
                break;
            }
        }
    }
};
