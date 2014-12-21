/* *********************************************************************
 *
 * User level tester program to test Squeue driver
 *
 * Program Name:        SqTester
 * Target:              Intel Galileo Gen1
 * Architecture:		x86
 * Compiler:            i586-poky-linux-gcc
 * File version:        v1.0.0
 * Author:              Brahmesh S D Jain
 * Email Id:            Brahmesh.Jain@asu.edu
 **********************************************************************/

/* *************** INCLUDE DIRECTIVES FOR STANDARD HEADERS ************/
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* ***************** PREPROCESSOR DIRECTIVES **************************/

/*
 * LOG files are generated from each thread. If LOG files are not 
 * needed, then please comment the below line
 */
/* #define LOG */

/*
 * To enable printing the receiver output
 */
#define PRINT_ON
/*
 * This is the clock frequency at which the CPU is running. This is used
 * for converting cpu ticks to micro seconds.
 * It has been defined with the clock frequency of the Intel Galileo
 * Quark processor. If you are running on other processor please change
 * accordingy
 */
#define CPU_FREQ_MHZ 399.088
/*
 * Minimum sleeping time for each thread
 */
#define MINIMUM_SLEEPTIME   1000

/*
 * Maximum sleeping time for each thread
 */
#define MAXIMUM_SLEEPTIME   10000

/*
 * Total time for which this program should run
 */
#define TOTAL_RUN_TIME   10000000

/*
 * Byte position of Timestamp in the message
 */
#define TIMESTAMP_BYTE_POS   0

/*
 * Byte position of SenderId in the message
 */
#define SENDER_ID_BYTE_POS   8

/*
 * Byte position of Message Id in the message
 */
#define MESSAGE_ID_BYTE_POS   9

/*
 * Byte position of Receiver Id in the message
 */
#define RECIEVER_ID_BYTE_POS   11

/*
 * Byte position from which the string in the message start
 */
#define STRING_BYTE_POS   12

/*
 * Maximum length of the message
 */
#define TOTAL_MESSAGE_LENGTH   93

/*
 * Minimum length of the string in the message
 */
#define MINIMUM_STRING_LENGTH   10

/*
 * Maximum length of the string in the message
 */
#define MAXIMUM_STRING_LENGTH   80

/*
 * Length of the header in the message
 */
#define MESSAGE_HEADER_LENGTH   12

/*
 * Length of the NULL character which is added at the end of the message
 */
#define NULL_STRING_LENGTH   1

/*
 * String in the message are generated randomly.
 * ASCII value of the first character you would like to start from
 */
#define FIRST_PRINTABLE_CHAR   65

/*
 * ASCII value of the last character you would like to end
 */
#define LAST_PRINTABLE_CHAR   90

/*
 * First global sequence to start from
 */
#define FIRST_SEQUENCE_NUMBER   1


/*
 * Preprocessor directive to enable debugging mode for this program
 * If this line is uncommented, the program reports important events
 * on the console.
 */
/* #define DEBUGMODE */


/*
 * Structure that holds specific information send to each thread
 * from the main(or parent) thread
 */
typedef struct ThreadSpecificData_Tag
{
	unsigned char TimeoutFlag; /* this flag is set by the main thread when the child thread needs to stop processing messages */
	char SenderId; /* Sender Id/Reciever Id is sent through this variable to help receiver thread open the respective device files */
}ThreadSpecificDataType;

/*
 * Global Sequnece number that will be incremented when a sender
 * thread sends a message
 */
static unsigned int GlobalSequenceNumber = FIRST_SEQUENCE_NUMBER;

/*
 * Mutex to protect the above global variable
 */
pthread_mutex_t MsgIdMutex = PTHREAD_MUTEX_INITIALIZER;

/* *********************************************************************
 * NAME:             GenerateRandomMessage
 * CALLED BY:        SenderTask
 * DESCRIPTION:      Generates the random message using the random number
 *					 with some dummy logic and writes to the string part
 * INPUT PARAMETERS: Message:pointer to the first string in the message
 *                   RandomNumberReceived:Random number sent by sender
 * RETURN VALUES:    none
 ***********************************************************************/
void GenerateRandomMessage(char* Message, const unsigned int RandomNumberReceived, const unsigned char Length)
{
	unsigned int LoopIndex = 0;
	unsigned int RandomNumber = RandomNumberReceived;

	for (LoopIndex = 0 ; LoopIndex < Length; LoopIndex++)
	{
		/* Dummy logic that generates the characters between two ASCII values */
		*(Message + LoopIndex) = (RandomNumber % (LAST_PRINTABLE_CHAR - FIRST_PRINTABLE_CHAR)) + FIRST_PRINTABLE_CHAR;
		/* Random number is incremented to get another random character */
		RandomNumber = (RandomNumber +  (RandomNumber % Length) + LoopIndex);
	}
	/* NULL character is added at the end of the string */
	*(Message + LoopIndex) = '\0';
}

/* *********************************************************************
 * NAME:             SenderTask
 * CALLED BY:        main
 * DESCRIPTION:      Task called by sender thread, responsible for writing
 *					 to Bus_In_Q with directed to random senders
 * INPUT PARAMETERS: Param:Thread specific data sent by Main thread
 * RETURN VALUES:    none
 ***********************************************************************/
void* SenderTask(void *param)
{
	int FdInQ,res; /* File open status for InQ, Result variable */
	unsigned int RandNum = 0; /* Local copy of Random number */
	char MessageToBeSent[TOTAL_MESSAGE_LENGTH]; /* Local space for message preparation */
	unsigned char UniformStringLength = MINIMUM_STRING_LENGTH; /* For storing string length */
	unsigned short SequenceNumber = FIRST_SEQUENCE_NUMBER; /* For storing global sequence number */

#ifdef LOG
    char PrintMessage[2*TOTAL_MESSAGE_LENGTH]; /* To print to a log file */
	char LogFileName[20]; /* Log file name */
    FILE *SenderLogFile; /* File pointer to the log file */
#endif

#ifdef DEBUGMODE
	pthread_t myself_thread; /* to get the thread ID */
	myself_thread = pthread_self(); /* Fn call to get the thread ID*/
#endif

    /* Open the Bus In Q device */
	FdInQ = open("/dev/bus_in_q", O_WRONLY);

    /* Check if device opened successfully */
	if (FdInQ < 0 )
	{
		printf("Can not open bus_in_q device file by sender .\n");
		/* return from the thread */		
		return 0;
	}
	else
	{
#ifdef LOG
        /* Preparing sender's log file name */
        sprintf(LogFileName,"Sender%c.log",(((ThreadSpecificDataType *)param)->SenderId));
		/* Device file opened , create a log file */
		SenderLogFile = fopen(LogFileName,"w");
		/* Print the first line in the log file */
		fprintf(SenderLogFile,"SId \t RId \t MsgId \t AccumulatedTime(ms) \t Message \n");
#endif
	}
	
    /* Fill the timestamp */
    /* Sender will not put any timestamp for messages , hence set to 0(NULL char equivalent) */
	memset(&MessageToBeSent[TIMESTAMP_BYTE_POS],0,sizeof(unsigned long long));

	/* Fill Sender Id */
	MessageToBeSent[SENDER_ID_BYTE_POS] = ((ThreadSpecificDataType *)param)->SenderId ;
	
	/* Lock the mutex so that no other sender thread can increment the Sequence number */
    pthread_mutex_lock(&MsgIdMutex);
    /* Get the Sequence number and then increment it */
    SequenceNumber = GlobalSequenceNumber++;
    /* After incrementing the number unlock the mutex so that other sender threads can use it */
    pthread_mutex_unlock(&MsgIdMutex);

    /* Run untill the main() thread sets its flag, it is set after 10secs */
	while(0 == ((ThreadSpecificDataType *)param)->TimeoutFlag)
	{
		/*Fill Sequence number */
		memcpy(&MessageToBeSent[MESSAGE_ID_BYTE_POS],&SequenceNumber,sizeof(unsigned short));
		
		/* Generate a random number that will be used to generaterandom string,random receiver id */
		RandNum = rand();
		
		/* Fill random receiver id from 1-3 */
		MessageToBeSent[RECIEVER_ID_BYTE_POS] = ((RandNum % 3) + 1);

		/* Now generate the random message */
		GenerateRandomMessage(&MessageToBeSent[STRING_BYTE_POS],RandNum,UniformStringLength);
		
		/* Write the message to the driver */
        res = write(FdInQ,MessageToBeSent,(MESSAGE_HEADER_LENGTH + UniformStringLength + NULL_STRING_LENGTH));

        /* Check if the message was succesfully sent */
        if(0 == res)
        {
#ifdef LOG
            /* Message sent successfully, log the message */
			sprintf(PrintMessage,"SId=%c \t ->  RId=%d \t     MsgId=%d\t\t\t AccTime=%llu us\t\t\t\t Msg=%s \n",((ThreadSpecificDataType *)param)->SenderId,MessageToBeSent[RECIEVER_ID_BYTE_POS],SequenceNumber,(unsigned long long)0,(char *)&MessageToBeSent[STRING_BYTE_POS]);
			fprintf(SenderLogFile,"%s",(char*)(&PrintMessage));
#endif
            /* Increment the next global sequence number and increment it after that */
			pthread_mutex_lock(&MsgIdMutex);
			SequenceNumber = GlobalSequenceNumber++;
			pthread_mutex_unlock(&MsgIdMutex);
			/* Length of the string is uniformly distributed between 10 and 80 */
			UniformStringLength = (UniformStringLength >= 80) ? 10 : (UniformStringLength + 1);
		}
		else
		{
			/* Message was not written as the buffer was full report it if the debug was enabled */
#ifdef DEBUGMODE
			printf("String was not written, Buffer full \n ");
#endif
		}

#ifdef DEBUGMODE
		printf(" Sender %u Task going to sleep\n",(unsigned int)myself_thread);
#endif
        /* Sender going to sleep for random period from 1ms to 10ms */
        usleep(MINIMUM_SLEEPTIME + (RandNum % (MAXIMUM_SLEEPTIME - MINIMUM_SLEEPTIME)));
	}
	
#ifdef LOG
    /* Close the log file */
    fclose(SenderLogFile);
#endif

    /* Close the driver file */
	close(FdInQ);
#ifdef DEBUGMODE
	printf("Sender %u Task thread ended \n",(unsigned int)myself_thread);
#endif
	return NULL;
}


/* *********************************************************************
 * NAME:             ReceiverTask
 * CALLED BY:        main
 * DESCRIPTION:      Task called by receiver thread, responsible for reading
 *					 from Bus_Out_Qs and print it on the console
 * INPUT PARAMETERS: Param:Thread specific data sent by Main thread
 * RETURN VALUES:    none
 ***********************************************************************/
void* ReceiverTask(void *param)
{
	int FdOutQ,res;/* File open stattus for OutQ, Result variable */
	unsigned int RandNum = 0; /* Random number used for generating sleeping time */
	char MessageReceived[TOTAL_MESSAGE_LENGTH]; /* Space to decode the message */
	char PrintMessage[2*TOTAL_MESSAGE_LENGTH]; /* Space for printable format message */
	unsigned char ReceiverId;/* Reciever Id of the received message */
	char SenderId; /* Sender Id of the received message */
	unsigned short MessageId; /* Global sequence number of the received message */
	unsigned long long TimeStamp; /* For storing the total accumulate time */
	char DeviceFile[20]; /* string to save device file name which this thread opens */
	char ExitReceiverTask = 0; /* Flag to decide whether receiver thread should end now*/
#ifdef LOG
	char LogFileName[20]; /* file name of the lof file*/
	FILE *ReceiverLogFile; /* Receiver log file pointer */
#endif

#ifdef DEBUGMODE
	pthread_t myself_thread;/* to get the thread ID */
	myself_thread = pthread_self(); /* Fn call to get the thread ID */
#endif
    /* prepare the file name of the device which needs to be opened */
    sprintf(DeviceFile,"/dev/bus_out_q%c",(((ThreadSpecificDataType *)param)->SenderId));

    /* Open the out device file */
	FdOutQ = open(DeviceFile, O_RDONLY);

	if (FdOutQ < 0 )
	{
		/* Device file could not be opened */
		printf("Can not open %s device file by Receiver .\n",DeviceFile);		
		return 0;
	}
	else
	{
#ifdef LOG
        /* prepare the file name of the log file */
        sprintf(LogFileName,"Receiver%c.log",(((ThreadSpecificDataType *)param)->SenderId));
		/* Device file opened , create a log file*/
		ReceiverLogFile = fopen(LogFileName,"w");
		/* First line in the log file */
		fprintf(ReceiverLogFile,"RId \t SId \t MsgId \t AccumulateTime(ms) \t Message \n");
#endif
	}

	do
	{
		/* open the output device for reading */
		res = read(FdOutQ,MessageReceived,TOTAL_MESSAGE_LENGTH);
		if (res < 0)
		{
			/* Since Buffer is empty , check whether Bus Daemon has stopped sending message
			 * if Bus Daemon has stopped sending, then receiver has no role to play now
			 */
			ExitReceiverTask = (1 == ((ThreadSpecificDataType *)param)->TimeoutFlag) ? (1) : 0;
			/* Buffer is empty , so receiver should take a nap */
		    /* Generate a randum number that will be used for sleep time */
		    RandNum = rand();
#ifdef DEBUGMODE
	    	printf(" Receiver %u Task going to sleep\n",(unsigned int)myself_thread);
#endif
            /* Receiver task ging to sleep for random period when the out device is empty */
            usleep(MINIMUM_SLEEPTIME + (RandNum % (MAXIMUM_SLEEPTIME - MINIMUM_SLEEPTIME)));
		}
		else
		{
			/* Receiver has received 'res number of Bytes' */
			/* Decode the message */
			ReceiverId = MessageReceived[RECIEVER_ID_BYTE_POS];
			SenderId = MessageReceived[SENDER_ID_BYTE_POS];
			memcpy(&MessageId,&MessageReceived[MESSAGE_ID_BYTE_POS],sizeof(MessageId));
			memcpy(&TimeStamp,&MessageReceived[TIMESTAMP_BYTE_POS],sizeof(TimeStamp));
			/* prepare a message with Receiver Id, Sender Id, Sequence Number , Accumulated time , Message*/
			sprintf(PrintMessage,"Rid=%d \t <-  Sid=%c \t     MsgId=%d\t\t\t Acctime=%llu us\t\t\t\t Msg=%s \n",ReceiverId,SenderId,MessageId,(unsigned long long)(TimeStamp/CPU_FREQ_MHZ),(char *)&MessageReceived[STRING_BYTE_POS]);
#ifdef PRINT_ON
	        /* print it on the console */
			printf("%s",(char*)(&PrintMessage));
#endif
#ifdef LOG
            /* print it to the log file if the logging is enabled */
			fprintf(ReceiverLogFile,"%s",(char*)(&PrintMessage));
#endif
		}
	}while(0 == ExitReceiverTask);
    /* close the reciver bus out device file */
	close(FdOutQ);
#ifdef LOG
    /* close the receiver log file */
	fclose(ReceiverLogFile);
#endif
#ifdef DEBUGMODE
	printf("Receiver %u thread ended \n",(unsigned int)myself_thread);
#endif
	return NULL;
}


void* BusDaemonTask(void *param)
{
	int FdInQ,FdOutQ1,FdOutQ2,FdOutQ3,ResI,ResO;/* file open statuses and result variables */
	unsigned int RandNum = 0; /* Random number */
    char MessageReceived[TOTAL_MESSAGE_LENGTH];/* local variable to store the recieved message */
	char ExitBusDaemonTask = 0; /* status flag to combine the thread exit condition */
	unsigned char ReceiverId; /* Receiver Id local variable */
#ifdef LOG
	char PrintMessage[2*TOTAL_MESSAGE_LENGTH]; /* Required for preparing the message to be printed on the log file */
	char SenderId; /* need to decode the sender id only during logging is enabled*/
	unsigned short MessageId; /* need to decode the message id only during logging is enabled */
	unsigned long long TimeStamp;/* time stamp for same reason */
	char LogInFileName[20];/* log file name which hold the log file name for received messages by bus deamon*/
    FILE *BusDaemonReadLogFile;/* file pointer that logs the messages that are read by the bus deamon */
	char LogOutFileName[20];/* log file name which hold the log file name for sent messages by bus deamon*/
    FILE *BusDaemonWroteLogFile;/* file pointer that logs the messages that are sent to the receiver devices */
#endif
#ifdef DEBUGMODE
	pthread_t myself_thread; /* to retrieve the thread id, for debugging purpose */
	myself_thread = pthread_self();/* retrieving the thread id */
#endif
    /* try  opening bus in Q */
	FdInQ = open("/dev/bus_in_q",O_RDONLY);
	if (FdInQ < 0)
	{
		/* device open fail */
		printf("Can not open bus_in_q device file by BusDaemon .\n");		
		return 0;
	}
	/* try opening bus out Q1 */
	FdOutQ1 = open("/dev/bus_out_q1",O_WRONLY);
	if (FdOutQ1 < 0)
	{
		/* device open fail */
		printf("Can not open bus_out_q1 device file by BusDaemon .\n");		
		return 0;
	}
	/* try opening bus out Q2 */
	FdOutQ2 = open("/dev/bus_out_q2",O_WRONLY);
	if (FdOutQ2 < 0)
	{
		/* device open fail */
		printf("Can not open bus_out_q2 device file by BusDaemon .\n");		
		return 0;
	}
	/* try opening bus out Q3 */
	FdOutQ3 = open("/dev/bus_out_q3",O_WRONLY);
	if (FdOutQ3 < 0)
	{
		/* device open fail */
		printf("Can not open bus_out_q3 device file by BusDaemon .\n");		
		return 0;
	}
#ifdef LOG
    /* prepare log file names */
    sprintf(LogInFileName,"BusDaemonRead.log");
    sprintf(LogOutFileName,"BusDaemonWrote.log");
    /* open log files in write mode and print the headings */
	BusDaemonReadLogFile = fopen(LogInFileName,"w");
	fprintf(BusDaemonReadLogFile,"SId \t RId \t MsgId \t AccumulatedTime(ms) \t Message \n");
	BusDaemonWroteLogFile = fopen(LogOutFileName,"w");
	fprintf(BusDaemonWroteLogFile,"RId \t SId \t MsgId \t AccumulatedTime(ms) \t Message \n");
#endif

    /* Prepare the message for reading from the InQ bus*/
	memset(&MessageReceived[TIMESTAMP_BYTE_POS],0,TOTAL_MESSAGE_LENGTH);
	
	do
	{
		/* Read the message from bus in Q */
		ResI = read(FdInQ,MessageReceived,TOTAL_MESSAGE_LENGTH);
		if (ResI < 0)
		{
#ifdef LOG
            /* Buffer is empty, log the event */
		    fprintf(BusDaemonReadLogFile," Buffer Empty \n");
#endif
			/* Since Buffer is empty , check whether senders have stopped sending message
			 * if senders have stopped sending, then Bus Deamon has no role to play now
			 */
			ExitBusDaemonTask = (1 == ((ThreadSpecificDataType *)param)->TimeoutFlag) ? (1) : 0;
			/* Buffer is empty , so receiver should take a nap */
		    /* Generate a randum number that will be used for sleep time */
		    RandNum = rand();
#ifdef DEBUGMODE
	    	printf(" BusDaemon %u Task going to sleep\n",(unsigned int)myself_thread);
#endif
            /* Sleep for a random period so that some messages can appear on the bus in Q */
            usleep(MINIMUM_SLEEPTIME + (RandNum % (MAXIMUM_SLEEPTIME - MINIMUM_SLEEPTIME)));
		}
		else
		{
			/* BusDaemon has received 'res number of Bytes' */
			/* Decode the Receiver ID */
			ReceiverId = MessageReceived[RECIEVER_ID_BYTE_POS];
#ifdef LOG
            /* decode the other part of the message necessary for the log */
		    SenderId = MessageReceived[SENDER_ID_BYTE_POS];
		    memcpy(&MessageId,&MessageReceived[MESSAGE_ID_BYTE_POS],sizeof(MessageId));
		    memcpy(&TimeStamp,&MessageReceived[TIMESTAMP_BYTE_POS],sizeof(TimeStamp));
		    /* Receiver Id, Sender Id, Sequence Number , Accumulated time , Message*/
		    sprintf(PrintMessage,"SId=%c \t ->  RId=%d \t     MsgId=%x\t\t\t AccTime=%llu us\t\t\t\t Msg=%s \n",SenderId,ReceiverId,MessageId,TimeStamp,(char *)&MessageReceived[STRING_BYTE_POS]);
		    fprintf(BusDaemonReadLogFile,"%s",(char*)(&PrintMessage));
#endif 
            /*  Check if the message is to be sent to the reciver1/receiver2/receiver3 by checking the receiver id in the message */
            if (1 == ReceiverId)
            {
				/* Write the message to the Receiver */
				/* If the write buffer is full, retry after a nap */
				do
				{
				   /* Call bus out Q write function */
                   ResO = write(FdOutQ1,MessageReceived,ResI);
                   if (ResO < 0)
                   {
		              /* Generate a randum number that will be used for sleep time */
		              RandNum = rand();
#ifdef DEBUGMODE
	    	          printf(" BusDaemon %u Task going to sleep\n",(unsigned int)myself_thread);
#endif
                      /* sleep for a random period so that reciever devices gets some space to accomodate this message */
                      usleep(MINIMUM_SLEEPTIME + (RandNum % (MAXIMUM_SLEEPTIME - MINIMUM_SLEEPTIME)));
				   }
			    }while(ResO < 0);
			}
			else if (2 == ReceiverId)
            {
				/* Write the message to the Receiver */
				/* If the write buffer is full, retry after a nap */
				do
				{
				   /* Call bus out Q write function */
                   ResO = write(FdOutQ2,MessageReceived,ResI);
                   if (ResO < 0)
                   {
		              /* Generate a randum number that will be used for sleep time */
		              RandNum = rand();
#ifdef DEBUGMODE
	    	          printf(" BusDaemon %u Task going to sleep\n",(unsigned int)myself_thread);
#endif
                      /* sleep for a random period so that reciever devices gets some space to accomodate this message */
                      usleep(MINIMUM_SLEEPTIME + (RandNum % (MAXIMUM_SLEEPTIME - MINIMUM_SLEEPTIME)));
				   }
			    }while(ResO < 0);
			}
			else if (3 == ReceiverId)
            {
				/* Write the message to the Receiver */
				/* If the write buffer is full, retry after a nap */
				do
				{
				   /* Call bus out Q write function */
                   ResO = write(FdOutQ3,MessageReceived,ResI);
                   if (ResO < 0)
                   {
		              /* Generate a randum number that will be used for sleep time */
		              RandNum = rand();
#ifdef DEBUGMODE
	    	          printf(" BusDaemon %u Task going to sleep\n",(unsigned int)myself_thread);
#endif
                      /* sleep for a random period so that reciever devices gets some space to accomodate this message */
                      usleep(MINIMUM_SLEEPTIME + (RandNum % (MAXIMUM_SLEEPTIME - MINIMUM_SLEEPTIME)));
				   }
			    }while(ResO < 0);
			}
			else
			{
				/* Invalid Reciever Id, Ignore the message*/
#ifdef DEBUGMODE
                printf("\n Invalid Receiver Id detected by BusDaemon !! \n");
#endif
			}
#ifdef LOG
            /* if log is enabled print the sent message in the BusDeamon's wrote log file */
			sprintf(PrintMessage,"RId=%d \t <-  SId=%c \t     MsgId=%x\t\t\t AccTime=%llu ms\t\t\t\t Msg=%s \n",ReceiverId,SenderId,MessageId,TimeStamp,(char *)&MessageReceived[STRING_BYTE_POS]);
			fprintf(BusDaemonWroteLogFile,"%s",(char*)(&PrintMessage));
#endif
		}
	}while(0 == ExitBusDaemonTask);
#ifdef LOG
    /* close all the log files */
    fclose(BusDaemonReadLogFile);
    fclose(BusDaemonWroteLogFile);
#endif
    /* close all the device files opened by bus daemon */
	close(FdInQ);
	close(FdOutQ1);
	close(FdOutQ2);
	close(FdOutQ3);
#ifdef DEBUGMODE
	printf("Bus Daemon %u thread ended \n",(unsigned int)myself_thread);
#endif
	return NULL;
}
int main()
{
	ThreadSpecificDataType Sender1 = {0,'1'},Sender2 = {0,'2'},Sender3 = {0,'3'},
	                       Receiver1 = {0,'1'},Receiver2 = {0,'2'},Receiver3 = {0,'3'},
	                       BusDaemon = {0,'B'} ;/* Fill thread specific data */	
	pthread_t Sender1ThreadID,Sender2ThreadID,Sender3ThreadID,
	          Receiver1ThreadID,Receiver2ThreadID,Receiver3ThreadID,
	          BusDaemonThreadID; /* declare thread Ids for all the child threads */

    /* Create all sender threads */
	pthread_create(&Sender1ThreadID, NULL, &SenderTask, &Sender1);
	printf("Sender1 Task thread Created \n");
	pthread_create(&Sender2ThreadID, NULL, &SenderTask, &Sender2);
	printf("Sender2 Task thread Created \n");
	pthread_create(&Sender3ThreadID, NULL, &SenderTask, &Sender3);
	printf("Sender3 Task thread Created \n");
    
    /* All senders whould have started sending messages to the bus in Q now
     * now create the Bus Daemon thread
     */
	pthread_create(&BusDaemonThreadID, NULL, &BusDaemonTask, &BusDaemon);
	printf("BusDeamon Task thread Created \n");

    /* Now the receiver threads are created */
	pthread_create(&Receiver1ThreadID, NULL, &ReceiverTask, &Receiver1);
	printf("Receiver1 Task thread Created \n");
	pthread_create(&Receiver2ThreadID, NULL, &ReceiverTask, &Receiver2);
	printf("Receiver2 Task thread Created \n");
	pthread_create(&Receiver3ThreadID, NULL, &ReceiverTask, &Receiver3);
	printf("Receiver3 Task thread Created \n");

    /* Main thread doing to sleep for 10 seconds*/
    printf("\nParent thread is going to sleep for 10secs now\n");
	usleep(TOTAL_RUN_TIME);
	
	/* 10 seconds elapsed now all communications has to be stopped in the proper sequence */
	/* Signal Sender threads to stop sending */
	Sender1.TimeoutFlag = 1;
	Sender2.TimeoutFlag = 1;
    Sender3.TimeoutFlag = 1;
	printf("\nWaiting for Sender1 to stop sending message \n");
	pthread_join(Sender1ThreadID, NULL);
	printf("\nWaiting for Sender2 to stop sending message \n");
    pthread_join(Sender2ThreadID, NULL);
	printf("\nWaiting for Sender3 to stop sending message \n");
    pthread_join(Sender3ThreadID, NULL);

    /* Signalling bus daemon to stop translating the message (it stops only
     * after the BusInQ becomes empty after this event)
     */
    BusDaemon.TimeoutFlag = 1;
	printf("\nWaiting for BusDaemon to finish its thread \n");
    pthread_join(Sender3ThreadID, NULL);

    /* Signal all receiver threads to stop receiving the messages.
     * they stop receiving the messages only after emptying all the bus out Q
     * so that no messages are missed
     */
	Receiver1.TimeoutFlag = 1;
	Receiver2.TimeoutFlag = 1;
    Receiver3.TimeoutFlag = 1;
	printf("\nWaiting for Receiver1 to stop Receiving message \n");
	pthread_join(Receiver1ThreadID, NULL);
	printf("\nWaiting for Receiver2 to stop Receiving message \n");
    pthread_join(Receiver2ThreadID, NULL);
	printf("\nWaiting for Receiver3 to stop Receiving message \n");
    pthread_join(Receiver3ThreadID, NULL);

    /* All threads would have joined by now, safely ending the main thread now */
	printf("\nMain thread Ended \n");

	return 0;
}
