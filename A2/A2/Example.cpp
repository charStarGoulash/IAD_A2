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

//#define SHOW_ACKS

int ServerPort = 1;
int ClientPort = 30001;
const int ProtocolId = 0x11223344;
const float DeltaTime = 1.0f / 30.0f;
const float SendRate = 1.0f / 30.0f;
const float TimeOut = 10.0f;
int PacketSize;

class FlowControl
{
public:
	
	FlowControl()
	{
		printf( "flow control initialized\n" );
		Reset();
	}
	
	void Reset()
	{
		mode = Bad;
		penalty_time = 4.0f;
		good_conditions_time = 0.0f;
		penalty_reduction_accumulator = 0.0f;
	}
	
	void Update( float deltaTime, float rtt )
	{
		const float RTT_Threshold = 250.0f;

		if ( mode == Good )
		{
			if ( rtt > RTT_Threshold )
			{
				printf( "*** dropping to bad mode ***\n" );
				mode = Bad;
				if ( good_conditions_time < 10.0f && penalty_time < 60.0f )
				{
					penalty_time *= 2.0f;
					if ( penalty_time > 60.0f )
						penalty_time = 60.0f;
					printf( "penalty time increased to %.1f\n", penalty_time );
				}
				good_conditions_time = 0.0f;
				penalty_reduction_accumulator = 0.0f;
				return;
			}
			
			good_conditions_time += deltaTime;
			penalty_reduction_accumulator += deltaTime;
			
			if ( penalty_reduction_accumulator > 10.0f && penalty_time > 1.0f )
			{
				penalty_time /= 2.0f;
				if ( penalty_time < 1.0f )
					penalty_time = 1.0f;
				printf( "penalty time reduced to %.1f\n", penalty_time );
				penalty_reduction_accumulator = 0.0f;
			}
		}
		
		if ( mode == Bad )
		{
			if ( rtt <= RTT_Threshold )
				good_conditions_time += deltaTime;
			else
				good_conditions_time = 0.0f;
				
			if ( good_conditions_time > penalty_time )
			{
				printf( "*** upgrading to good mode ***\n" );
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

int main( int argc, char * argv[] )
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
				
				if (sscanf(argv[i+1], "%d.%d.%d.%d", &a, &b, &c, &d))
				{
					mode = Client;

				}
			}

			// getting the File Name from user.
			if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-F") == 0)
			{
				fileName = argv[i + 1];
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


	/*
	if ( argc >= 2 )
	{
		int a,b,c,d;
		if ( sscanf( argv[1], "%d.%d.%d.%d", &a, &b, &c, &d ) )
		{
			if (!argc == 3)
			{
				printf("Error: please provide both ip address and filename");
				exit(1);
			}
			mode = Client;
			fileName = argv[2];
			address = theNet::Address(a,b,c,d,ServerPort);
		}
	}
	*/
	// initialize

	if ( !theNet::InitializeSockets() )
	{
		printf( "failed to initialize sockets\n" );
		return 1;
	}

	theNet::ReliableConnection connection( ProtocolId, TimeOut );
	
	const int port = mode == Server ? ServerPort : ClientPort;
	
	if ( !connection.Start( port ) )
	{
		printf( "could not start connection on port %d\n", port );
		return 1;
	}
	
	if ( mode == Client )
		connection.Connect( address );
	else
		connection.Listen();
	
	bool connected = false;
	float sendAccumulator = 0.0f;
	float statsAccumulator = 0.0f;
	
	FlowControl flowControl;
	
	while ( true )
	{
		// update flow control
		
		if ( connection.IsConnected() )
			flowControl.Update( DeltaTime, connection.GetReliabilitySystem().GetRoundTripTime() * 1000.0f );
		
		const float sendRate = flowControl.GetSendRate();

		// detect changes in connection state

		if ( mode == Server && connected && !connection.IsConnected() )
		{
			flowControl.Reset();
			printf( "reset flow control\n" );
			connected = false;
		}

		if ( !connected && connection.IsConnected() )
		{
			printf( "client connected to server\n" );
			connected = true;
		}
		
		if ( !connected && connection.ConnectFailed() )
		{
			printf( "connection failed\n" );
			break;
		}
		
		// send and receive packets
		
		sendAccumulator += DeltaTime;
		
		//BELOW IS WHERE THE FILE IS SENT FROM CLIENT--ATTILA-DIV COMMENT THIS IS THE ONLY PLACE I BELIEVE I CHANGED CODE
		while ( sendAccumulator > 1.0f / sendRate)
		{
			bool checker;
			unsigned char * packet;
			std::ifstream is(fileName, std::ifstream::binary);
			if (!is) 
			{
				//std::cout << "Error opening file to write from" << std::endl;
				break;
			}
			// get length of file:
			is.seekg(0, is.end);
			int length = is.tellg();
			is.seekg(0, is.beg);

			packet = new unsigned char[length];

			// read data as a block:
			is.read((char*)packet, length);
			//unsigned char packet[30000];
			//memset( packet, 1, sizeof( packet ) );
			/*PacketSize = length;
			int theSize = sizeof(packet);*/
			checker = connection.SendPacket( packet, length);
			sendAccumulator -= 1.0f / sendRate;
			//exit(2);
		}
		
		//BELOW IS WHERE THE SERVER RECIEVES WHAT THE CLIENT SENT---ATTILA-DIV COMMENT-ONLY CHANGED CODE HERE
		while ( true )
		{
			unsigned char packet[30000];
			int bytes_read = connection.ReceivePacket(packet, sizeof(packet));
			if (bytes_read == 0)
				break;
			std::ofstream outdata; // outdata is like cin

			outdata.open("testREC.txt"); // opens the file
			if (!outdata) 
			{ // file couldn't be opened
				std::cout << "Error: file could not be opened" << std::endl;
				break;
			}

			for (int i = 0; i < bytes_read; ++i)
			{
				outdata << packet[i];
			}
			outdata.close();
		
		}
		
		// show packets that were acked this frame
		
		#ifdef SHOW_ACKS
		unsigned int * acks = NULL;
		int ack_count = 0;
		connection.GetReliabilitySystem().GetAcks( &acks, ack_count );
		if ( ack_count > 0 )
		{
			printf( "acks: %d", acks[0] );
			for ( int i = 1; i < ack_count; ++i )
				printf( ",%d", acks[i] );
			printf( "\n" );
		}
		#endif

		// update connection
		
		connection.Update( DeltaTime );

		// show connection stats
		
		statsAccumulator += DeltaTime;

		while ( statsAccumulator >= 0.25f && connection.IsConnected() )
		{
			float rtt = connection.GetReliabilitySystem().GetRoundTripTime();
			
			unsigned int sent_packets = connection.GetReliabilitySystem().GetSentPackets();
			unsigned int acked_packets = connection.GetReliabilitySystem().GetAckedPackets();
			unsigned int lost_packets = connection.GetReliabilitySystem().GetLostPackets();
			
			float sent_bandwidth = connection.GetReliabilitySystem().GetSentBandwidth();
			float acked_bandwidth = connection.GetReliabilitySystem().GetAckedBandwidth();
			
			printf( "rtt %.1fms, sent %d, acked %d, lost %d (%.1f%%), sent bandwidth = %.1fkbps, acked bandwidth = %.1fkbps\n", 
				rtt * 1000.0f, sent_packets, acked_packets, lost_packets, 
				sent_packets > 0.0f ? (float) lost_packets / (float) sent_packets * 100.0f : 0.0f, 
				sent_bandwidth, acked_bandwidth );
			
			statsAccumulator -= 0.25f;
		}

		theNet::wait( DeltaTime );
	}
	
	theNet::ShutdownSockets();

	return 0;
}
