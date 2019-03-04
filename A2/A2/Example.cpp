/*
	Reliability and Flow Control Example
	From "Networking for Game Programmers" - http://www.gaffer.org/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <string>

#include "TheNetwork.h"
#include "CRC.h"

//#define SHOW_ACKS

int ServerPort = 1;
int ClientPort = 30001;
const int ProtocolId = 0x11223344;
const float DeltaTime = 1.0f / 30.0f;
const float SendRate = 1.0f / 30.0f;
const float TimeOut = 10.0f;
int PacketSize;
bool initialMessage = false;
unsigned char * filePacket;
int fileLength;

int sendBytesCheck = 256;
int recBytesCheck = 256;

unsigned char* packetRec;
bool fileDone = false;

/////////////////////////////////////////CHANGED BELOW FEB 25 2019 //////////////////////////////////////
struct fileInfo
{

	int thePacketSize;
	int theTotalBytes;
	std::uint32_t crc;
	std::string filename;
};
/////////////////////////////////////////CHANGED BELOW FEB 25 2019 //////////////////////////////////////
fileInfo firstMessage;



class FlowControl
{
public:


	FlowControl()
	{
		printf("flow control initialized\n");
		Reset();
	}

	void Reset()
	{
		mode = Bad;
		penalty_time = 4.0f;
		good_conditions_time = 0.0f;
		penalty_reduction_accumulator = 0.0f;
	}

	void Update(float deltaTime, float rtt)
	{
		const float RTT_Threshold = 250.0f;

		if (mode == Good)
		{
			if (rtt > RTT_Threshold)
			{
				printf("*** dropping to bad mode ***\n");
				mode = Bad;
				if (good_conditions_time < 10.0f && penalty_time < 60.0f)
				{
					penalty_time *= 2.0f;
					if (penalty_time > 60.0f)
						penalty_time = 60.0f;
					printf("penalty time increased to %.1f\n", penalty_time);
				}
				good_conditions_time = 0.0f;
				penalty_reduction_accumulator = 0.0f;
				return;
			}

			good_conditions_time += deltaTime;
			penalty_reduction_accumulator += deltaTime;

			if (penalty_reduction_accumulator > 10.0f && penalty_time > 1.0f)
			{
				penalty_time /= 2.0f;
				if (penalty_time < 1.0f)
					penalty_time = 1.0f;
				printf("penalty time reduced to %.1f\n", penalty_time);
				penalty_reduction_accumulator = 0.0f;
			}
		}

		if (mode == Bad)
		{
			if (rtt <= RTT_Threshold)
				good_conditions_time += deltaTime;
			else
				good_conditions_time = 0.0f;

			if (good_conditions_time > penalty_time)
			{
				printf("*** upgrading to good mode ***\n");
				good_conditions_time = 0.0f;
				penalty_reduction_accumulator = 0.0f;
				mode = Good;
				return;
			}
		}
	}

	float GetSendRate()
	{
		return mode == Good ? 30.0f : 10.0f;
	}

private:

	enum Mode
	{
		Good,
		Bad
	};

	Mode mode;
	float penalty_time;
	float good_conditions_time;
	float penalty_reduction_accumulator;
};

// ----------------------------------------------

int main(int argc, char * argv[])
{
	enum Mode
	{
		Client,
		Server
	};

	Mode mode = Server;
	theNet::Address address;
	std::string fileName;
	int a, b, c, d;		// to store the IP
	//////////////////////////////////////////////////////////////////////


	if (argc == 3)
	{
		mode = Server;
		initialMessage = true;
		if (strcmp(argv[1], "-p") == 0 || strcmp(argv[1], "-P") == 0)
		{
			if (!(ServerPort = std::stoi(argv[2])))
			{
				printf("Please provide Port Number for server Correctly");
			}
		}
	}
	else if (argc == 7)
	{
		initialMessage = true;

		for (int i = 0; i < argc; i++)
		{
			// getting the port number for the client 
			if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "-P") == 0)
			{
				if (!(ServerPort = std::stoi(argv[i + 1])))
				{
					printf("Please provide Port Number Correctly");
					return 0;
				}
			}

			// getting the ip address of the server for client to connect
			if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "-A") == 0)
			{

				if (sscanf(argv[i + 1], "%d.%d.%d.%d", &a, &b, &c, &d))
				{
					mode = Client;

				}
			}
			/////////////////////////////////////////CHANGED BELOW FEB 25 2019 //////////////////////////////////////
			// getting the File Name from user.//CHANGED TO OPEN FILE AND LOAD STRUCT HERE
			if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-F") == 0)
			{
				fileName = argv[i + 1];
				firstMessage.filename = fileName;

				std::ifstream is(fileName, std::ifstream::binary);
				if (!is)
				{
					//std::cout << "Error opening file to write from" << std::endl;
					break;
				}
				// get length of file:
				is.seekg(0, is.end);
				fileLength = is.tellg();
				is.seekg(0, is.beg);

				filePacket = new unsigned char[fileLength];

				// read data as a block:
				is.read((char*)filePacket, fileLength);
				firstMessage.theTotalBytes = fileLength;
				firstMessage.thePacketSize = fileLength / 50000;
				firstMessage.crc = CRC::Calculate(filePacket, firstMessage.theTotalBytes, CRC::CRC_32());
			}

		}
		address = theNet::Address(a, b, c, d, ServerPort);
	}
	else
	{
		printf("Please provide all the paremeter");
		return 0;
	}

	/////////////////////////////////////////////////////////////////////////
	// parse command line

	// initialize

	if (!theNet::InitializeSockets())
	{
		printf("failed to initialize sockets\n");
		return 1;
	}

	theNet::ReliableConnection connection(ProtocolId, TimeOut);

	const int port = mode == Server ? ServerPort : ClientPort;

	if (!connection.Start(port))
	{
		printf("could not start connection on port %d\n", port);
		return 1;
	}

	if (mode == Client)
		connection.Connect(address);
	else
		connection.Listen();

	bool connected = false;
	float sendAccumulator = 0.0f;
	float statsAccumulator = 0.0f;

	FlowControl flowControl;

	while (true)
	{
		// update flow control

		if (connection.IsConnected())
			flowControl.Update(DeltaTime, connection.GetReliabilitySystem().GetRoundTripTime() * 1000.0f);

		const float sendRate = flowControl.GetSendRate();

		// detect changes in connection state

		if (mode == Server && connected && !connection.IsConnected())
		{
			flowControl.Reset();
			printf("reset flow control\n");
			connected = false;
		}

		if (!connected && connection.IsConnected())
		{
			printf("client connected to server\n");
			connected = true;
		}

		if (!connected && connection.ConnectFailed())
		{
			printf("connection failed\n");
			break;
		}

		// send and receive packets

		sendAccumulator += DeltaTime;
		/////////////////////////////////////////CHANGED BELOW FEB 25 2019 //////////////////////////////////////
		//BELOW IS WHERE THE FILE IS SENT FROM CLIENT--ATTILA-DIV COMMENT THIS IS THE ONLY PLACE I BELIEVE I CHANGED CODE
		while (sendAccumulator > 1.0f / sendRate)
		{
			if (mode == Client && initialMessage)
			{
				//BELOW I AM MAKING AN INITIAL MESSAGE TO BE SENT WITH ALL THE DATA FROM THE FIRST MESSAGE STRUCT
				bool checker;
				std::string temp = firstMessage.filename + "-" + std::to_string(firstMessage.theTotalBytes) + "-" + std::to_string(firstMessage.thePacketSize) + "-" + std::to_string(firstMessage.crc);

				unsigned char* packet = (unsigned char*)temp.c_str();

				checker = connection.SendPacket(packet, fileLength);

				initialMessage = false;
			}
			/////////////////////////////////////////CHANGED BELOW FEB 25 2019 //////////////////////////////////////
			else if (mode == Client && !initialMessage)
			{
				bool checker;

				if (sendBytesCheck < firstMessage.theTotalBytes)
				{
					if ((firstMessage.theTotalBytes - sendBytesCheck) > 256)
					{
						checker = connection.SendPacket(filePacket, firstMessage.thePacketSize);
						sendBytesCheck += 256;
						sendAccumulator -= 1.0f / sendRate;
					}
					else
					{
						int tempSize = firstMessage.theTotalBytes - sendBytesCheck;
						checker = connection.SendPacket(filePacket, tempSize);
						sendBytesCheck = 256;
						sendAccumulator -= 1.0f / sendRate;
						exit(2);
					}
				}

			}
			else
			{
				unsigned char packet[30000];
				memset(packet, 1, sizeof(packet));
				connection.SendPacket(packet, sizeof(packet));
				sendAccumulator -= 1.0f / sendRate;
			}
		}

		//BELOW IS WHERE THE SERVER RECIEVES WHAT THE CLIENT SENT---ATTILA-DIV COMMENT-ONLY CHANGED CODE HERE
		while (true)
		{
			unsigned char packet[30000];
			if (initialMessage)
			{
				int bytes_read = connection.ReceivePacket(packet, sizeof(packet));
				if (bytes_read == 0)
					break;
			}

			/////////////////////////////////////////CHANGED BELOW FEB 25 2019 //////////////////////////////////////
			
			if (mode == Server && !initialMessage)
			{
				if (recBytesCheck < firstMessage.theTotalBytes)
				{
					if ((firstMessage.theTotalBytes - recBytesCheck) > 256)
					{
						int bytes_read = connection.ReceivePacket(packetRec, firstMessage.thePacketSize);
						if (bytes_read == 0)
							break;
					}
					else
					{
						int tempSize = firstMessage.theTotalBytes - recBytesCheck;
						int bytes_read = connection.ReceivePacket(packetRec, tempSize);
						if (bytes_read == 0)
							break;
						fileDone = true;
					}
				}
				if (fileDone)
				{
					int crcCheck = CRC::Calculate(packet, firstMessage.theTotalBytes, CRC::CRC_32());
					if (crcCheck == firstMessage.crc)
					{
						std::cout << "FILE CONFIRMED" << std::endl;
						///////////////////////////////////////////////////////TEST///////////Displaying speed after reciving file
						float rtt = connection.GetReliabilitySystem().GetRoundTripTime();

						unsigned int sent_packets = connection.GetReliabilitySystem().GetSentPackets();
						unsigned int acked_packets = connection.GetReliabilitySystem().GetAckedPackets();
						unsigned int lost_packets = connection.GetReliabilitySystem().GetLostPackets();

						float sent_bandwidth = connection.GetReliabilitySystem().GetSentBandwidth();
						float acked_bandwidth = connection.GetReliabilitySystem().GetAckedBandwidth();

						printf("rtt %.1fms, sent %d, acked %d, lost %d (%.1f%%), sent bandwidth = %.1fkbps, acked bandwidth = %.1fkbps\n",
							rtt * 1000.0f, sent_packets, acked_packets, lost_packets,
							sent_packets > 0.0f ? (float)lost_packets / (float)sent_packets * 100.0f : 0.0f,
							sent_bandwidth, acked_bandwidth);
						////////////////////////////////////////////////////////////////////////////////////
						std::ofstream outdata; // outdata is like cin

						outdata.open(firstMessage.filename); // opens the file
						if (!outdata)
						{ // file couldn't be opened
							std::cout << "Error: file could not be opened" << std::endl;
							break;
						}

						for (int i = 0; i < firstMessage.theTotalBytes; ++i)
						{
							outdata << packet[i];
						}
						outdata.close();
						connection.KillLoop(1);
						break;
					}
					else
					{
						std::cout << "Not Confirms, will try again" << std::endl;
					}
				}
			}
			/////////////////////////////////////////CHANGED BELOW FEB 25 2019 //////////////////////////////////////
			else if (mode == Server && initialMessage)
			{

				std::string TEMPtheTotalBytes;
				std::string TEMPthePacketSize;

				std::string TEMPcrc;

				int check = 0;
				/////////////////////////////////////////CHANGED BELOW FEB 25 2019 //////////////////////////////////////
				//BELOW I AM ITTERATING THROUGH THE ARRAY I RECEIVED AND POPULATING THE STRUCT 
				for (int i = 0; i < strlen((char*)packet); i++)
				{
					// getting the port number for the client 
					if (packet[i] == '-')
					{
						i++;
						check++;
					}

					if (check == 0)
					{
						firstMessage.filename += packet[i];
					}

					if (check == 1)
					{
						TEMPtheTotalBytes += packet[i];
					}

					if (check == 2)
					{
						TEMPthePacketSize += packet[i];
					}

					if (check == 3)
					{
						TEMPcrc += packet[i];
					}

				}
				/////////////////////////////////////////CHANGED BELOW FEB 25 2019 //////////////////////////////////////
				firstMessage.theTotalBytes = std::stoi(TEMPtheTotalBytes);
				firstMessage.thePacketSize = std::stoi(TEMPthePacketSize);
				firstMessage.crc = std::stoll(TEMPcrc);
				initialMessage = false;
				packetRec = new unsigned char[firstMessage.theTotalBytes];
			}

		}

		// show packets that were acked this frame

#ifdef SHOW_ACKS
		unsigned int * acks = NULL;
		int ack_count = 0;
		connection.GetReliabilitySystem().GetAcks(&acks, ack_count);
		if (ack_count > 0)
		{
			printf("acks: %d", acks[0]);
			for (int i = 1; i < ack_count; ++i)
				printf(",%d", acks[i]);
			printf("\n");
		}
#endif

		// update connection

		connection.Update(DeltaTime);

		// show connection stats

		statsAccumulator += DeltaTime;

		while (statsAccumulator >= 0.25f && connection.IsConnected())
		{
			float rtt = connection.GetReliabilitySystem().GetRoundTripTime();

			unsigned int sent_packets = connection.GetReliabilitySystem().GetSentPackets();
			unsigned int acked_packets = connection.GetReliabilitySystem().GetAckedPackets();
			unsigned int lost_packets = connection.GetReliabilitySystem().GetLostPackets();

			float sent_bandwidth = connection.GetReliabilitySystem().GetSentBandwidth();
			float acked_bandwidth = connection.GetReliabilitySystem().GetAckedBandwidth();

			printf("rtt %.1fms, sent %d, acked %d, lost %d (%.1f%%), sent bandwidth = %.1fkbps, acked bandwidth = %.1fkbps\n",
				rtt * 1000.0f, sent_packets, acked_packets, lost_packets,
				sent_packets > 0.0f ? (float)lost_packets / (float)sent_packets * 100.0f : 0.0f,
				sent_bandwidth, acked_bandwidth);

			statsAccumulator -= 0.25f;
		}

		theNet::wait(DeltaTime);
	}

	theNet::ShutdownSockets();

	return 0;
}
