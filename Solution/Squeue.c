/* *********************************************************************
 *
 * Kernel level char device program to implement shared queues
 *
 * Program Name:        Squeue
 * Target:              Intel Galileo Gen1
 * Architecture:		x86
 * Compiler:            i586-poky-linux-gcc
 * File version:        v1.0.0
 * Author:              Brahmesh S D Jain
 * Email Id:            Brahmesh.Jain@asu.edu
 **********************************************************************/
 
 /* *************** INCLUDE DIRECTIVES FOR STANDARD HEADERS ************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <asm/msr.h>

/* ***************** PREPROCESSOR DIRECTIVES **************************/

/*
 * Memory space for the message buffer are allocated at the runtime, depending
 * on the size of the message received by the device.
 * If this macro is uncommented, memory allocation takes place at compile time.
 * uncommenting this will result in a slighty better CPU times but consumes little
 * more ram space as we define fixes static memory
 */
/* #define STATIC */

/*
 * This constitues the number of ring pointers
 */
#define BUFFERENTRIES   10
/*
 * When using static memory for message buffers,this amount of bytes are allocated
 * for each buffer entry
 */
#define BUFFERENTRIYSIZE   100

/*
 * Length of the device name string
 */
#define DEVICE_NAME_LENGTH   20

/*
 * Number of devices that are need to be created by the driver at the end of the
 * driver initialization using udev
 */
#define NUMBER_OF_DEVICES   4

/*
 * driver name
 */
#define DEVICE_NAME    "squeue"

/*
 * Device name for Bus in Q
 */
#define DEVICE_NAME_IN_Q   "bus_in_q"

/*
 * Device name for Bus out Queues
 */
#define DEVICE_NAME_OUT_Q   "bus_out_q"

/*
 * Please uncomment this when debugging, this will print the
 * important events on the console
 */
/* #define DEBUGMODE */

/* uint8 and unsigned char are used interchangeably in the program */
typedef unsigned char uint8;

typedef struct SqueueDevTag
{
	struct cdev cdev; /* cdev structure */
	struct mutex BufferParamMutex; /* Mutex protex for all the indexes of the buffer */
	char name[DEVICE_NAME_LENGTH];   /* Driver Name */
	char* RingBufferPtrs[BUFFERENTRIES]; /* Ringbuffer with 10 pointers pointing to either statically or dynamically allocated buffer */
	unsigned char Length[BUFFERENTRIES]; /* Length of each entry in the buffer */
#ifdef STATIC
	char Buffers[BUFFERENTRIES][BUFFERENTRIYSIZE]; /* Space for ring buffer which is statically allocated */
#endif
	uint8 ReadIndex;  /* Used for ring buffer implementation */
	uint8 WriteIndex; /* Used for ring buffer implementation */
	uint8 BufferEmpty : 1; /* Buffer Empty flag */
}SqueueDevType;

/* Bus In Queue pointer */
static SqueueDevType *SqueueBus = NULL;

/* Device number alloted */
static dev_t SqueueDevNumber;

/* Create class and device which are required for udev */
struct class *SqueueDevClass;
static struct device *SqueueDevName;

/* *********************************************************************
 * NAME:             SqueueDevDriverOpen
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      copies the device structure pointer to the private  
 *                   data of the file pointer. 
 * INPUT PARAMETERS: inode pointer:pointer to the inode of the caller
 *                   filept:file pointer used by this inode
 * RETURN VALUES:    int : status - Fail/Pass(0)
 ***********************************************************************/
int SqueueDevDriverOpen(struct inode *inode, struct file *filept)
{
	SqueueDevType *dev; /* dev pointer for the present device */
	/* to get the device specific structure from cdev pointer */
	dev = container_of(inode->i_cdev, SqueueDevType, cdev);
	/* stored to private data so that next time filept can be directly used */
	filept->private_data = dev;
#ifdef DEBUGMODE
	/* Print that device has opened succesfully */
	printk("Device %s opened succesfully ! \n",(char *)&(dev->name));
#endif
   return 0;
}

/* *********************************************************************
 * NAME:             SqueueDevDriverRelease
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      Releases the file structure
 * INPUT PARAMETERS: inode pointer:pointer to the inode of the caller
 *                   filept:file pointer used by this inode
 * RETURN VALUES:    int : status - Fail/Pass(0)
 ***********************************************************************/
int SqueueDevDriverRelease(struct inode *inode, struct file *filept)
{
	SqueueDevType *dev = (SqueueDevType*)(filept->private_data);
	printk("\n%s is closing\n", dev->name);
	return 0;
}

/* *********************************************************************
 * NAME:             SqueueDevDriverWrite
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      writes chunk of data to the message buffer
 * INPUT PARAMETERS: filept:file pointer used by this inode
 *                   buf : pointer to the user data
 *                   count : no of bytes to be copied to the msg buffer
 *                   offp: offset from which the string to be written
 *                         (not used)
 * RETURN VALUES:    ssize_t : remaining number of bytes that are to be
 *                             written
 ***********************************************************************/
ssize_t SqueueDevDriverWrite(struct file *filept, const char *buf,size_t count, loff_t *offp)
{
	SqueueDevType *dev = (SqueueDevType*)(filept->private_data); /* get the device data structure*/
	unsigned long long TimeAccumulated = 0; /* local variable used for processing the timestamp part of the msg */
	unsigned long long CurrentCounter = 0; /* Counter that gets the current cpu time ticks */
	ssize_t RetValue = -EINVAL; /* Error code sent when the buffer is full */
#ifdef DEBUGMODE
    unsigned char *sender_id; /* used for debugging, sender id printed */
     sender_id= kmalloc(9 * sizeof(char), GFP_KERNEL);
	if (copy_from_user(sender_id,buf,(9 *sizeof(char))))
	{
		printk(" \nError copying from user space");
	}
	else
	{
	    printk(" Driver Written by thread %d  !! \n ",(unsigned int)(*(sender_id + 8)));
    }
	kfree(sender_id);
#endif
   /* Lock the mutex , so that it is protected by reading from bus deamon */
   mutex_lock(&dev->BufferParamMutex);

   if ((dev->ReadIndex != dev->WriteIndex) || /* Buffer is not full */
       ((dev->ReadIndex == dev->WriteIndex) && (1 == dev->BufferEmpty)) /* Buffer is Empty */
      )
   {
#ifndef STATIC
       /* Dynamic allocation of memory when STATIC keyword is not defined */
       (dev->RingBufferPtrs[dev->WriteIndex]) = kmalloc(count, GFP_KERNEL);
       if (NULL == (dev->RingBufferPtrs[dev->WriteIndex]))
       {
		   /*Kmalloc failed to allocate memory */
          printk("\n Kmalloc failed to allocate memory");

          /* unlock the mutex and return */
          mutex_unlock(&dev->BufferParamMutex);
          return RetValue;
	   }
#endif
       /* Get the data from user space */
	   if (copy_from_user((dev->RingBufferPtrs[dev->WriteIndex]),buf,count))
	   {
          printk("\n Buffer writting failed");
	   }
	   else
	   {
		   /* Copy the length of the message */
		   dev->Length[dev->WriteIndex] = count;
		   /* copy the previous accumulated time from the message to a local variable */
		   memcpy(&TimeAccumulated,dev->RingBufferPtrs[dev->WriteIndex],(sizeof(unsigned long long)));
		   /* Get the current counter */
		   rdtscll(CurrentCounter);
		   /* subtract the accumulated time from the jiffies and store back to the same variable */
		   TimeAccumulated = (CurrentCounter - TimeAccumulated);
		   /* Write back to the message buffer */
		   memcpy(dev->RingBufferPtrs[dev->WriteIndex],&TimeAccumulated,(sizeof(unsigned long long)));
           /* After Writting the one entry to the buffer, write Index is move to the next index of the buffer
            * If the Index has reached the end of the buffer, then the index is moved to the start of the buffer
            */
           dev->WriteIndex = ((BUFFERENTRIES - 1) == dev->WriteIndex) ? (0) : (dev->WriteIndex + 1) ;
           /* buffer  is no more empty */
           dev->BufferEmpty = 0;
           /* Return value is the number of bytes remaining for writting, which is zero */
           RetValue = 0;
		}
   }
   /* Now bus deamon can read and write to the buffer */
   mutex_unlock(&dev->BufferParamMutex);
   return RetValue;
}

/* *********************************************************************
 * NAME:             SqueueDevDriverRead
 * CALLED BY:        User App through kernel
 * DESCRIPTION:      reads chunk of data from the message buffer
 * INPUT PARAMETERS: filept:file pointer used by this inode
 *                   buf : pointer to the user data
 *                   count : no of bytes to be copied to the user buffer
 *                   offp: offset from which the string to be read
 *                         (not used)
 * RETURN VALUES:    ssize_t : number of bytes written to the user space
 ***********************************************************************/
ssize_t SqueueDevDriverRead(struct file *filept, char *buf,size_t count, loff_t *offp)
{
	SqueueDevType *dev = (SqueueDevType*)(filept->private_data);/* get the device data structure*/
	unsigned long long TimeAccumulated = 0;/* local variable used for processing the timestamp part of the msg */
	unsigned long long CurrentCounter = 0;/* Counter that gets the current cpu time ticks */
	ssize_t RetValue = -EINVAL; /* When buffer is empty, this error code is reported */

   /* Lock the mutex , so that it is protected by writing from bus deamon */
   mutex_lock(&dev->BufferParamMutex);

   if((dev->ReadIndex != dev->WriteIndex) ||/* Buffer has some elements*/
      ((dev->ReadIndex == dev->WriteIndex) && (0 == dev->BufferEmpty)) /* When bufferis full*/)
   {
	   /* Add the accululated time to the timestamp */
	   memcpy(&TimeAccumulated,dev->RingBufferPtrs[dev->ReadIndex],(sizeof(unsigned long long)));
	   /* Get present counter value*/
	    rdtscll(CurrentCounter);
	   /* Total accumulated time is the current time minus the time that was written during write operation */
	   TimeAccumulated = (CurrentCounter - TimeAccumulated);
	   /* Write back */
	   memcpy(dev->RingBufferPtrs[dev->ReadIndex],&TimeAccumulated,(sizeof(unsigned long long)));

       /* Copy to the user space*/
       if(copy_to_user(buf,dev->RingBufferPtrs[dev->ReadIndex],(dev->Length[dev->ReadIndex])))
       {
#ifdef DEBUGMODE
          printk("\n Buffer Reading failed ");
#endif
	   }
	   else
	   {
		   /* copy the number of bytes copied that was copied to the user space */
		   RetValue = (dev->Length[dev->ReadIndex]);
#ifndef STATIC
		   /* Free the memory if dynamic allocation memory was done */
		   kfree(dev->RingBufferPtrs[dev->ReadIndex]);
		   /* assign NULL_PTR to the ring pointer now */
		   dev->RingBufferPtrs[dev->ReadIndex] = NULL;
		   /* Assign zero length to the Length of this pointer object */
		   dev->Length[dev->ReadIndex] = 0;
#endif
           /* Increment the ReadIndex and  */
		   dev->ReadIndex = ((BUFFERENTRIES - 1) == dev->ReadIndex) ? (0) : (dev->ReadIndex + 1) ;
		   /* when read pointer reaches write pointer it is the condition of buffer empty */
		   dev->BufferEmpty = (dev->ReadIndex == dev->WriteIndex) ? (1) : (0) ;
	   }
   }

   /* Now bus deamon/receiver can read and/or write to the buffer */
   mutex_unlock(&dev->BufferParamMutex);

   return RetValue;
}


/* Assigning operations to file operation structure */
static struct file_operations SqueueFops = {
    .owner = THIS_MODULE, /* Owner */
    .open = SqueueDevDriverOpen, /* Open method */
    .release = SqueueDevDriverRelease, /* Release method */
    .write = SqueueDevDriverWrite, /* Write method */
    .read = SqueueDevDriverRead, /* Read method */
};

/* *********************************************************************
 * NAME:             SqueueDevDriverInit
 * CALLED BY:        By system when the driver is installed
 * DESCRIPTION:      Initializes the driver and creates the 4 devices
 * INPUT PARAMETERS: None
 * RETURN VALUES:    int : initialization status
 ***********************************************************************/
int __init SqueueDevDriverInit(void)
{
	int Ret; /* return variable */
	unsigned char LoopIndex = 0; /* for looping through all devices */
	unsigned char BufferLoopIndex = 0;/* for looping through all buffer pointers */

	/* Allocate device major number dynamically */
	if (alloc_chrdev_region(&SqueueDevNumber, 0, NUMBER_OF_DEVICES, DEVICE_NAME) < 0)
	{
         printk("Device could not acquire a major number ! \n");
         return -1;
	}
	
	/* Populate sysfs entries */
	SqueueDevClass = class_create(THIS_MODULE, DEVICE_NAME);
   
    /* Allocate memory for all the devices */
    SqueueBus = (SqueueDevType*)kmalloc(((sizeof(SqueueDevType)) * NUMBER_OF_DEVICES), GFP_KERNEL);
    
    /* Check if memory was allocated properly */
   	if (NULL == SqueueBus)
	{
       printk("Kmalloc Fail \n");

	   /* Remove the device class that was created earlier */
	   class_destroy(SqueueDevClass);
       /* Unregister devices */
	   unregister_chrdev_region(MKDEV(MAJOR(SqueueDevNumber), 0), NUMBER_OF_DEVICES);
       return -ENOMEM;
	} 
     
    /* Device Creation */ 
    for (LoopIndex = 0; LoopIndex < NUMBER_OF_DEVICES; LoopIndex++)
    {
		/*Initializemutex for each of the devices */
		mutex_init(&((&SqueueBus[LoopIndex])->BufferParamMutex));
        /* Copy the respective device name */
        if (0 == LoopIndex)
        {
	       sprintf(((&SqueueBus[LoopIndex])->name), DEVICE_NAME_IN_Q);
		}
		else
		{
		   sprintf(((&SqueueBus[LoopIndex])->name), DEVICE_NAME_OUT_Q "%d",LoopIndex);
		}

#ifdef STATIC
	    /* Set all the entries in the ring buffer to zero */
	    memset((&SqueueBus[LoopIndex])->Buffers, 0, (BUFFERENTRIES * BUFFERENTRIYSIZE));
#endif  
	    /* Assign the ring of pointers to point to the above buffer entries or to NULL incase of dynamic */
	    for (BufferLoopIndex = 0; BufferLoopIndex < BUFFERENTRIES; BufferLoopIndex++)
	    {
#ifdef STATIC
			(&SqueueBus[LoopIndex])->RingBufferPtrs[BufferLoopIndex] = &((&SqueueBus[LoopIndex])->Buffers[BufferLoopIndex][0]);
#else
            (&SqueueBus[LoopIndex])->RingBufferPtrs[BufferLoopIndex] = NULL;
#endif
			(&SqueueBus[LoopIndex])->Length[BufferLoopIndex] = 0;
		}
   
	    /* Set the read index and write index to the first buffer entry */
	    (&SqueueBus[LoopIndex])->ReadIndex = 0;
	    (&SqueueBus[LoopIndex])->WriteIndex = 0;
	    (&SqueueBus[LoopIndex])->BufferEmpty = 1;

	    /* Connect the file operations with the cdev */
	    cdev_init(&((&SqueueBus[LoopIndex])->cdev), &SqueueFops);
	    (&SqueueBus[LoopIndex])->cdev.owner = THIS_MODULE;

	    /* Connect the major/minor number to the cdev */
	    Ret = cdev_add(&((&SqueueBus[LoopIndex])->cdev),(MKDEV(MAJOR(SqueueDevNumber),LoopIndex)),1);
	    if (Ret)
	    {
		   printk("Bad cdev\n");
		   return Ret;
	    }
	    if (0 == LoopIndex)
	    {
			/* For In bus */
			SqueueDevName = device_create(SqueueDevClass, NULL,(MKDEV(MAJOR(SqueueDevNumber),LoopIndex)), NULL, DEVICE_NAME_IN_Q);
		}
		else
		{
			/* For out buses */
			SqueueDevName = device_create(SqueueDevClass, NULL,(MKDEV(MAJOR(SqueueDevNumber),LoopIndex)), NULL, DEVICE_NAME_OUT_Q "%d", LoopIndex);
		}
	}
	
    /* Driver is initialized */
	printk(" Squeue Driver is initialized \n");
	
	return 0;
}


/*
 * Driver Deinitialization
 */
void __exit SqueueDevDriverExit(void)
{
	unsigned char LoopIndex = 0;/* for looping through all the devices */

	for (LoopIndex = 0; LoopIndex < NUMBER_OF_DEVICES; LoopIndex++)
	{
	   /* Destroy the devices first */
	   device_destroy(SqueueDevClass, (MKDEV(MAJOR(SqueueDevNumber),LoopIndex)));
	   /* Delete each of the cdevs */
	   cdev_del(&((&(SqueueBus[LoopIndex]))->cdev));
	}
	/* Free up the allocated memory for all of the device */
	 kfree(SqueueBus);
	/* Remove the device class that was created earlier */
	class_destroy(SqueueDevClass);
	
	/* Unregister devices */
	unregister_chrdev_region(MKDEV(MAJOR(SqueueDevNumber), 0), NUMBER_OF_DEVICES);
	
	printk(" All Squeue devices and driver are removed ! ");
}

module_init(SqueueDevDriverInit);
module_exit(SqueueDevDriverExit);
MODULE_LICENSE("GPL v2");

