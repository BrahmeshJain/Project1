/***********************************************************************************************************
*******************Shared Queue Implementation and user space tester program to test it*********************
***********************************************************************************************************/
Author: Brahmesh Jain
EmailId: Brahmesh.Jain@asu.edu
Document version : 1.0.0

This zip file contains two source files.

1) Squeue.c is the linux kernel driver implementing the shared queue.
2) main_1.c is the user level program that is used for testin the program.

Other than the Assignment's requirement, the following are some of the things that Driver requires :

1) Driver can work in two modes : static memory mode or dynamic memory mode.
	Static memory limits the length of the message to be not more that 100bytes in length
	Dynamic memory can take any length of the message unless kernel fails to allocate that much memory
	
2) Dynamic memory is enabled by default. To enable static mode , uncomment #define STATIC line in Squeue.c 
   and recompile the program.
   
3) Driver reports the accumulated time in the buffer interms of cpu ticks. For this purpose user application
   has to leave first 8 bytes of the messages. Driver is implemented in such that it requires only one 8 bytes
   allocated for the accumulated time. Driver does not care about the remaining part of the message
    _______________________________________________________________________________      ________
   |                         |                                                     .....         |
   |    Accumulated Time     |         Other part of the message                                 |
   |_________________________|_____________________________________________________......________|
    <----8 Bytes------------>
   
4) For testing purpose, SqTester defines the message format as below:
    _______________________________________________________________________________________     __________
   |                         |         |           |           |                           .....         |
   |    Accumulated Time     |Source Id|   MsgId   |Receiver Id|            String                       |
   |_________________________|_________|___________|___________|___________________________......________|
    <----8 Bytes------------> <-1Byte-> <-2bytes--> <--1byte---> <---------String------------------------>
    
5) By default user application prints the receives messages from the receiver on the console. If you needed to
   turn off printing on the console, please comment out #include PRINT_ON in the main_1.c file
  
6) SqTester(main_1.c) can also be configured to print the messages sent and received from the bus to a log file.
   to enable this uncommment #define LOG in the main_1.c file.
  
7) Finally steps to run the program on Intel Galielo Board :
   a) Load the SDK source of galileo y running : "source ~/SDK/environment-setup-i586-poky-linux"
   b) open terminal with root permission , run the command "make all" to compile the driver
   c) Compile the tester(user application) program, "$CC main_1.c -o SqTester -lpthread"
   d) Transfer two file : Squeue.ko and SqTester to the galielo board
   e) Open Galileo's terminal and Install the driver by running the command "insmod Squeue.ko"
   f) run the user application with the command "./SqTester"
   g) to clean up the log files generated transfer the same make file and run "make cleanlog"
   h) to cleanup the generated files, run "make clean"

