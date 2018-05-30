#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <utility>

#define MAXBUFLEN 2000
#define R_BUFF 100
#define RWS 1
#define CHUNKSIZE 1472

using namespace std;

int C_ACK = -1;
string received_data_buff[R_BUFF];
int seq_num_array[R_BUFF];

string create_ACK_packet(int c_ack_num){
	string msg("A");
	msg.append(" ");
	msg.append(to_string(c_ack_num));
	msg.append("\n");
	return msg;
}

// Cumulative ACK is the ACK until which the packets have been acknowledged
// The packet expected is C_ACK + 1
int update_cumulative_ack(){
	int num_elements = 0;
	int i;
	cout << "C_ACK: " << C_ACK;
	for( i = (C_ACK+1) % R_BUFF; i < R_BUFF; i++){
		if(seq_num_array[i] != -1)
			num_elements++;
		else
			break;
	}

	if(i == R_BUFF){
		// check only if circled back in the arrray
		for(int j = 0; j <= C_ACK; j++)
			if(seq_num_array[i] != -1)
				num_elements++;
			else
				break;
	}

	cout << "Total elements to be acknowledged " << num_elements << endl;
	cout << "new C_ACK: " << C_ACK + num_elements << endl;
	return C_ACK + num_elements;
}

pair<int, string> data_packet_handler(string msg){
	cout << "message received in dph: " << endl << msg << endl;
	std::istringstream iss;
	vector<std::string> result;
	iss.str(msg);
	int counter = 0;
	for (string s; iss>>s && counter != 2; ){
		counter++;
		result.push_back(s);
	}
	string segment_number = "";
	// data packet format --> D + segment_number + data
	if(result.size() ==2){
		segment_number = result[1];
	}
	else
		cout << "no seg " << endl;
	// cout << msg << endl;
	string data = msg.substr(3+segment_number.size());

	int seq_num = stoi(segment_number);
	// cout << "In handle DATA:" << data << endl;
	return make_pair(seq_num, data);
}

int received_handler(string msg, FILE* fp){

	pair<int, string> extract_packet = data_packet_handler(msg);
	int seq_num = extract_packet.first;
	string data = extract_packet.second;
	cout << " seq_num: " << seq_num << endl;
	cout << "data in received handler on extraction \n " << data << endl;

	int old_ack = C_ACK;
	cout<<"old_ack:"<<old_ack<<endl;
	int new_ack;

	int acceptable_seq_num = (C_ACK + R_BUFF) % (2*R_BUFF);
	cout<<"acceptable_seq_num:"<<acceptable_seq_num<<endl;
	// fill the data structures
	if(seq_num <= max(acceptable_seq_num, old_ack+1) && seq_num >= min(acceptable_seq_num, old_ack+1)){
		seq_num_array[seq_num % R_BUFF] = seq_num;
		received_data_buff[seq_num % R_BUFF] = data;
		new_ack = seq_num;
		if( (old_ack + 1) == seq_num){
			// new_ack = update_cumulative_ack();
			// Traverse buffer from old ack to new ack and write data to file
			int start_idx = (old_ack+1) % R_BUFF; // from expected to all filed
			int end_idx = new_ack % R_BUFF;

			if(start_idx < end_idx){
				for(int i = start_idx; i <= end_idx; i++){
					cout<<"fprinting :"<<received_data_buff[i]<<endl;
					fprintf(fp, "%s", received_data_buff[i].c_str());
					received_data_buff[i] = "";
					seq_num_array[i] = -1;
				}
			}
			else{
				for(int i = start_idx; i < R_BUFF; i++){
					cout<<"fprinting :"<<received_data_buff[i]<<endl;
					fprintf(fp, "%s", received_data_buff[i].c_str());
					received_data_buff[i] = "";
					seq_num_array[i] = -1;
				}
				for(int i = 0; i <= end_idx; i++ ){
					cout<<"fprinting :"<<received_data_buff[i]<<endl;
					fprintf(fp, "%s", received_data_buff[i].c_str());
					received_data_buff[i] = "";
					seq_num_array[i] = -1;
				}
			}
			C_ACK = new_ack % (2*R_BUFF); // TODO: check
		}
	}

	// send ACK out
	cout << "C_ACK: " << C_ACK << endl;
	return C_ACK;
}

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {

	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
	struct sockaddr_storage their_addr;
	char buf[MAXBUFLEN] = {0};
	socklen_t addr_len;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	char port[5];
	sprintf(port, "%d", myUDPport);

	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("listener: socket");
			continue;
		}

		if (::bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("listener: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "listener: failed to bind socket\n");
		return;
	}

	freeaddrinfo(servinfo);

	addr_len = sizeof their_addr;
	FILE* fp = fopen(destinationFile, "w");
	if(fp == NULL){
		cout << "ERROR OPENING FILE" << endl;
		exit(1);
	}

	while(1){
		if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
			(struct sockaddr *)&their_addr, &addr_len)) == -1) {
			perror("recvfrom");
			exit(1);
		}
		buf[numbytes] = '\0';
		// cout << "numbytes :" << numbytes << endl;
		// cout<<"DATA recv :"<<buf<<endl;
		// if(numbytes + 1 < MAXBUFLEN)
		// 	buf[numbytes+1] = '\0';
		// call the received_handler

		// check for FIN message
		if(buf[0] == 'F'){
			string msg = "F";
			if ((numbytes = sendto( sockfd, msg.c_str() , strlen(msg.c_str()), 0,
					 (struct sockaddr *)&their_addr, addr_len )) == -1) {
				perror("receiver: sendto");
				exit(1);
			}
			cout<<"Sent Fin ACk"<<endl;
			fflush(fp);
			fclose(fp);
			return;
		}
		else if (buf[0] == 'D')
		{
			string msg(buf);
			int ack = received_handler(msg, fp);
			// send ACK
			string ack_msg = create_ACK_packet(ack);
			if ((numbytes = sendto( sockfd, ack_msg.c_str() , strlen(ack_msg.c_str()), 0,
					 (struct sockaddr *)&their_addr, addr_len )) == -1) {
				perror("receiver: sendto");
				exit(1);
			}
			cout<<"Sent ACK"<<endl;
		}
	}

	close(sockfd);
}

int main(int argc, char** argv)
{
	unsigned short int udpPort;

	if(argc != 3)
	{
		fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
		exit(1);
	}

	udpPort = (unsigned short int)atoi(argv[1]);

	reliablyReceive(udpPort, argv[2]);
}
