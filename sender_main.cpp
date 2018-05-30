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
#include <string>
#include <ctime>
#include <fstream>
#include <cmath>
#include <mutex>
#include <sstream>
#include <vector>
#include <thread>

#define SWS 100
#define S_BUFF 1500
#define payload 1472
#define MAXBUFLEN 2000

//rtt
#define g 0.125
#define h 0.25

using namespace std;

mutex onlyLock;

string data_buffer[SWS]; // data buffered
// all get locked except data_buffer
int slideLen = SWS;
int seqNum = 0;
time_t sendTime[SWS];  // timestamps
time_t finTimes[3];
int lastACK = -1;

long totalAcks;
bool retransmit = false;
//RTT
double A, D, err, M, RTO;

//estimate current RTT
int get_RTT(){
	return 0;
}

//creates data packet to be sent
string create_DATA_packet(string data, int sequence_number){
	string msg("D");
	msg.append(" ");
	msg.append(to_string(sequence_number));
	msg.append(" ");
	msg.append(data);
	// msg.append("\n");
	return msg;
}

string create_ACK_packet(int ack_num){
	string msg("A");
	msg.append(" ");
	msg.append(to_string(ack_num));
	msg.append("\n");
	return msg;
}

bool checkTO(int i)
{
	int curr = time(NULL);
	if(difftime(curr, sendTime[i]) > RTO)
		return true;
	else
		return false;
}

void updateRTO(time_t last, time_t curr)
{
	M = difftime(curr, last);
	err = M - A;
	A = A + g*err;
	D = D + h*(abs(err)-D);
	RTO = A + 4*D;
}

void teardown(int sockfd, struct addrinfo *p)
{
  //send F three times incl timeout

  for(int i = 0; i<3; i++)
  {
		cout<<"i ="<<i<<endl;
    onlyLock.lock();
		cout<<"RTO is "<<RTO<<endl;
    string finMsg = "F";
		int numbytes;
  	if ((numbytes = sendto(sockfd, finMsg.c_str(), 1, 0,
  p->ai_addr, p->ai_addrlen)) == -1)
  	{
  		perror("talker: sendto");
  		exit(1);
  	}
    finTimes[i] = time(NULL);
    time_t curr = time(NULL);
    while(difftime(curr, finTimes[i]) < RTO && finTimes[i]!=-1)
    {
			cout<<"difftime is:"<<difftime(curr, finTimes[i])<<endl;
      curr = time(NULL);
			cout<<"teardown leaving lock "<<endl;
      onlyLock.unlock();
      onlyLock.lock();
			cout<<"teardown got lock "<<endl;


    }
		cout<<"Out of the check loop FIN"<<endl;

    if(finTimes[i]== -1)
    {
				cout<<"FIN ACK was received"<<endl;
				cout<<"teardown leaving lock 1"<<endl;
				onlyLock.unlock();
			  return;
		}
		cout<<"teardown leaving lock 2"<<endl;
		onlyLock.unlock();
  }
}

void receiveACK (int sockfd, struct addrinfo *p)
{
	int numbytes;
	char buf[MAXBUFLEN];
	while(1)
	{
		if((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
        p->ai_addr, &(p->ai_addrlen))) == -1)
		{
			perror("listener: socket");
            exit(0);
		}
		onlyLock.lock();
		cout<<"ACKthread got lock "<<endl;


		cout<<"RECEIVED: "<<buf<<endl;

		time_t curr = time(NULL);
		time_t latestSent;
		std::istringstream iss;
		vector<std::string> result;
		string msg(buf);
		iss.str(msg);
		for (string s; iss>>s; )
				result.push_back(s);
		// data packet format --> A + sequence number

		string type = result[0];
		if(type=="F")
		{
			finTimes[0] = -1;
			finTimes[1] = -1;
			finTimes[2] = -1;
			cout<<"ACK leaving lock "<<endl;
			onlyLock.unlock();
			return;
		}
		int ACKseqNum = stoi(result[1]);
		if(type=="A")
		{
			cout<<"type A"<<endl;
			if(totalAcks == 0)
			{
				cout<<"ACK leaving lock "<<endl;

				onlyLock.unlock();

				continue;
			}
			int startIdx = (lastACK+1)%(2*SWS);
			int endIdx = (lastACK + SWS)%(2*SWS);
			if(ACKseqNum >= min(startIdx, endIdx) && ACKseqNum<=max(startIdx,endIdx))
			{
				cout<<"in range"<<endl;
				if(ACKseqNum >= startIdx)
				{
					for(int i = startIdx; i<= ACKseqNum; i++)
					{
						totalAcks--;
						if(totalAcks == 0)
						{
							cout<<"totalAcks is now 0"<<endl;

							slideLen = 0;
							break;
						}
						slideLen++;
						latestSent = sendTime[i%SWS];
						data_buffer[i%SWS] = "";
						sendTime[i%SWS] = -1;
						lastACK = i;
					}
				}
				else
				{
					for(int i = startIdx; i<(2*SWS); i++)
					{
						totalAcks--;
						if(totalAcks == 0)
						{
							slideLen = 0;
							break;
						}
						slideLen++;
						data_buffer[i%SWS] = "";
						sendTime[i%SWS] = -1;
						lastACK = i;
					}
					for(int i = 0; i<= ACKseqNum; i++)
					{
						totalAcks--;
						if(totalAcks == 0)
						{
							slideLen = 0;
							break;
						}
						slideLen++;
						data_buffer[i%SWS] = "";
						latestSent = sendTime[i%SWS];
						sendTime[i%SWS] = -1;
						lastACK = i;
					}
				}
				if(!retransmit && totalAcks!=0)
					updateRTO(latestSent, curr);
				else
					retransmit = false;
			}
		}
		cout<<"ACK leaving lock "<<endl;

		onlyLock.unlock();

	}
}
void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {

	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	char port[5];
	sprintf(port, "%d", hostUDPport);

	if ((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return;
	}

	// loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("talker: socket");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "talker: failed to create socket\n");
		return;
	}

//-----------------------------------------------------------------------------------------------------------
	//initialize RTT;
	A = 2;
	D = 1;
	RTO = A + 4*D;
	int start;
	//open file
	FILE *fp = fopen(filename, "r");
    if(fp == NULL){
        printf("File open error\n");
        exit(0);
    }
    // fseek(fp,0,bytesToTransfer);
		// cout<<ftell(fp)<<endl;
    totalAcks = 1 + (bytesToTransfer-1)/payload;
    // fseek(fp,0,SEEK_SET);
		cout << "totalAcks: " << totalAcks << endl;
    int bytesToWrite;

		onlyLock.lock();
		cout<<"SEND got lock "<<endl;

		if(totalAcks < slideLen)
			slideLen = totalAcks;
		cout<<"SEND leaving lock "<<endl;

		onlyLock.unlock();

    std::thread ACKthread(receiveACK,sockfd, p);
    while(1)
    {
    	onlyLock.lock();
			cout<<"SEND got lock "<<endl;

    	start = (lastACK + 1)%SWS;
    	int i = (start + SWS - slideLen)%SWS;


    	while(slideLen)
    	{
				cout << "slideLen: " << slideLen << endl;
    		//if data empty
    			//read each chunk
    			//add TS
    			//create packet with seq number
    		//send packet
    		//check TO of expected
    			//if timedout - go back N
    		//inc i%sws
    		//decrement slideLen
    		//increment seqNum %2SWS
				string data;
				int len_sqnum;
    		if(data_buffer[i].empty())
    		{
    			  char buff[payload] = {0};
        		bytesToWrite = fread(buff,1,min(bytesToTransfer, (long long unsigned int)payload),fp);
						// cout << " bytesToWrite: " << bytesToWrite << endl;
						if(bytesToTransfer > payload)
							bytesToTransfer -= payload;
        		if(bytesToWrite > 0)
        		{
							string msg(buff);
							// cout << "creating message : " << msg << endl;
        			data = create_DATA_packet(msg, seqNum);
							// cout << "data packet created: " << data << endl;
        			data_buffer[i] = data;
							len_sqnum = data.size() - msg.size();
							// cout << "len_sqnum: " << len_sqnum << endl;
    				}
    		}

    		//sending packet
				bytesToWrite += len_sqnum;
    		int bytesWritten = 0;
        while(bytesWritten != bytesToWrite)
        {
	        // int sent;
	        sendTime[i] = time(NULL);
					cout<<"SENDING:\n"<<data_buffer[i].substr(bytesWritten,string::npos).c_str()<<endl;
					// send D + seqnum first
					// cout << "bytesToWrite-bytesWritten: " <<bytesToWrite-bytesWritten << endl;
	        if ((numbytes = sendto(sockfd, data_buffer[i].substr(bytesWritten,string::npos).c_str(), (bytesToWrite)-bytesWritten, 0,
							 p->ai_addr, p->ai_addrlen)) == -1)
	        {
						perror("talker: sendto");
						exit(1);
					}
	        if(numbytes == -1)
	        {
	            printf("Error sending file\n");
	            exit(0);
	        }
	        bytesWritten += numbytes;
				}
				//checkTO
				if(checkTO((lastACK + 1)%SWS))
				{
					cout<<"timing out"<<endl;
					retransmit = true;
					RTO *=2;
					slideLen = min((long int)SWS, totalAcks);
					seqNum = (lastACK + 1)%(2*SWS);
					start = (lastACK + 1)%SWS;
					i = (start + SWS - slideLen)%SWS;
				}
				else
				{
					slideLen--;
					seqNum = (seqNum + 1)%(2*SWS);
					i = (i+1)%SWS;
				}
				cout<<"SEND leaving lock "<<endl;

					onlyLock.unlock();
					onlyLock.lock();
					cout<<"SEND got lock and slideLen is "<<slideLen<<endl;
	    	}
				cout<<"slide len is 0 and totalAcks is "<<totalAcks<<"and RTO is :"<<RTO<<endl;
      	if(totalAcks!= 0 && checkTO((lastACK + 1)%SWS))
				{
					cout<<"timing out"<<endl;
					retransmit = true;
					RTO *=2;
					slideLen = min((long int)SWS, totalAcks);
					seqNum = (lastACK + 1)%(2*SWS);
					start = (lastACK + 1)%SWS;
					i = (start + SWS - slideLen)%SWS;
					continue;
				}

        // error checking
        if(bytesToWrite < payload){
					cout<<"in err checking"<<endl;
            if(feof(fp) || totalAcks == 0)
            {
                printf("End of file\n");
								cout<<"totalAcks:"<<totalAcks<<endl;
                if(totalAcks == 0)
                {		cout<<"we are done"<<endl;
									cout<<"SEND leaving lock "<<endl;
									onlyLock.unlock();
										break;
								}
						}
            if(ferror(fp))
            {
                printf("Error in reading of file\n");
            	exit(0);
            }

        }
				cout<<"SEND leaving lock "<<endl;

        onlyLock.unlock();
    }
		//send FIN message
		cout<<"creating fin message"<<endl;
		teardown(sockfd, p);
		cout<<"teardown done"<<endl;
// 		string finMsg = "F";
// 		if ((numbytes = sendto(sockfd, finMsg.c_str(), 1, 0,
// p->ai_addr, p->ai_addrlen)) == -1)
// 		{
// 			perror("talker: sendto");
// 			exit(1);
// 		}
		// ACKthread.join();

//-----------------------------------------------------------------------------------------------------------
	freeaddrinfo(servinfo);
	cout<<"here"<<endl;
	close(sockfd);
	cout<<"her2e"<<endl;
	fclose(fp);
	ACKthread.join();
	cout<<"her2e"<<endl;
	return;
}

int main(int argc, char** argv) {
	unsigned short int udpPort;
	unsigned long long int numBytes;

	if(argc != 5)
	{
		fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
		exit(1);
	}
	udpPort = (unsigned short int)atoi(argv[2]);
	numBytes = atoll(argv[4]); // bytes to transfer

	cout << "udpPort: " << udpPort << endl;
	cout << "argv[1] " << argv[1] << endl;
	reliablyTransfer(argv[1], udpPort, argv[3], numBytes);
	cout<<"h3ere"<<endl;

	return 0;
}
