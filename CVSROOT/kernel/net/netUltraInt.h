/*
 * netUltraInt.h --
 *
 *	Definitions for the UltraNet VME adapter.
 *
 * Copyright 1990 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $Header$ SPRITE (Berkeley)
 */

#ifndef _NETULTRAINT
#define _NETULTRAINT

#include <netInt.h>
#include <fs.h>

/*
 * Defined constants:
 *
 * NET_ULTRA_CONTROL_REG_ADDR	Address of the Ultranet control
 *				register.
 * NET_ULTRA_INTERRUPT_PRIORITY The interrupt priority generated by the card.
 *				This value derived from the Ultranet 
 *				documentation
 * NET_ULTRA_VME_REQUEST_LEVEL 	The level of requests to the VME bus.
 *				This value was derived from the Ultranet
 *				source.
 * NET_ULTRA_VME_ADDRESS_SPACE	Defines how the adapter uses the VME
 *				address space in the following manner:
 *				Bit 1 set => XRBs are in A32, A24 otherwise.
 *				Bit 2 set => buffers are in A32, A24 otherwise.
 *				Bit 3 set => Non-privileged access, 
 *					supervisor access otherwise.
 *				Bit 4 set => block mode DMA, word access
 *					otherwise.
 */

#ifdef sun3
#define NET_ULTRA_CONTROL_REG_ADDR		0xffff7740
#define NET_ULTRA_INTERRUPT_PRIORITY		3
#define NET_ULTRA_VME_REQUEST_LEVEL		3
#define NET_ULTRA_VME_ADDRESS_SPACE		0
#define NET_ULTRA_MAP_THRESHOLD			1024
#endif

#ifdef sun4
#define NET_ULTRA_CONTROL_REG_ADDR		0xffff7740
#define NET_ULTRA_INTERRUPT_PRIORITY		3
#define NET_ULTRA_VME_REQUEST_LEVEL		3
#define NET_ULTRA_VME_ADDRESS_SPACE		0
#define NET_ULTRA_MAP_THRESHOLD			0
#endif

typedef enum EventEnum {
    TO_ADAPTER_FILLED,
    INFO_NOT_PENDING,
    PROCESS_XRB,
    INTERRUPT,
    SEND_REQ,
    SEND_REQ_INFO,
    START_DSND,
    DSND,
    DSND_WAIT,
    DSND_STREAM,
} EventEnum;

typedef struct NetUltraTraceInfo {
    int				sequence;
    EventEnum			event;
    int				index;
    struct NetUltraXRBInfo	*infoPtr;
    Timer_Ticks			ticks;
} NetUltraTraceInfo;

#define TRACE_SIZE 100

#define NEXT_TRACE(statePtr, ptrPtr) { \
    NetUltraTraceInfo	*tmpPtr = (statePtr)->tracePtr;		\
    *(ptrPtr) = tmpPtr;						\
    tmpPtr++;							\
    if (tmpPtr == &((statePtr)->traceBuffer[TRACE_SIZE])) {	\
	tmpPtr = (statePtr)->traceBuffer;			\
    }								\
    (statePtr)->tracePtr = tmpPtr;				\
    (*(ptrPtr))->sequence = (statePtr)->traceSequence++;	\
}

/*
 * Structure to hold all state information associated with an Ultranet
 * vme adapter.
 */

typedef struct NetUltraState {
    int			magic;		/* Useful for debugging. */
    int			flags;		/* See below. */
    volatile int 	*intrReg;	/* Address of the adapter interrupt
					 * register. */
    volatile int 	*resetReg;	/* Address of the reset register. */
    struct NetUltraXRB	*firstToHostPtr;  /* First XRB in queue to host. */
    struct NetUltraXRB	*nextToHostPtr;	  /* Next XRB to be filled by
					   * the adapter. */
    struct NetUltraXRB	*lastToHostPtr;	  /* Last XRB in queue to host. */
    struct NetUltraXRB	*firstToAdapterPtr; /* First XRB in queue to adapter. */
    struct NetUltraXRB	*nextToAdapterPtr;  /* Next XRB to be filled by host. */
    struct NetUltraXRB	*lastToAdapterPtr;  /* Last XRB in queue to adapter. */
    int			adapterVersion;	/* Version of adapter software. */
    int			priority;	/* Interrupt priority. */
    int			vector;		/* Interrupt vector. */
    int			requestLevel;	/* VME bus request level. */
    int			addressSpace;	/* Address space for queues and
					 * buffers. */
    int			maxReadPending;	/* Maximum pending datagram
					 * reads on the interface. */
    int			numReadPending;	/* Number of pending reads. */
    int			readBufferSize;	/* Size of pending read buffers. */
    int			maxWritePending; /* Maximum pending datagram writes.*/
    int			numWritePending; /* Number of pending writes. */
    Address		buffersDVMA;	/* DVMA buffers for pending reads
					 * and writes. */
    Address		buffers;	/* Address of DVMA buffers in kernel
					 * address space. */
    int			bufferSize;	/* Size of a DVMA buffer. */
    int			numBuffers;	/* Number of DVMA buffers. */
    List_Links		freeBufferListHdr; /* List of free DVMA buffers*/
    List_Links		*freeBufferList; /* Ptr to list of free DVMA
					  * buffers*/
    Sync_Condition	bufferAvail; /* Condition to wait for a free
					  * DVMA buffer. */
    Sync_Condition	toAdapterAvail; /* Condition to wait for an XRB to
					 * the adapter to become available. */
    Boolean		queuesInit;	/* TRUE => XRB queues have been 
					 * allocated. */
    List_Links		pendingXRBInfoListHdr; /* List of info about pending 
						* XRBs.*/
    List_Links		*pendingXRBInfoList;   /* Pointer to list of info about
						* pending XRBs. */
    List_Links		freeXRBInfoListHdr;/* List of free XRB info 
					    * structures. */
    List_Links		*freeXRBInfoList;  /* Pointer to list of info about
					    * pending XRBs. */
    Net_Interface	*interPtr;	   /* Interface structure associated
					    * with this adapter. */
    NetUltraTraceInfo	*tracePtr;
    NetUltraTraceInfo	traceBuffer[TRACE_SIZE];
    int			traceSequence;
    Net_UltraStats	stats;
} NetUltraState;

#define	NET_ULTRA_STATE_MAGIC	0x3a5a7a9a

/*
 * Size of the XRB queues.
 */

#define NET_ULTRA_NUM_TO_HOST		5	/* Number of XRBs to host. */
#define NET_ULTRA_NUM_TO_ADAPTER	5	/* Number of XRBs to adapter. */

/*
 * The following are defined for the flags field of the NetUltraState
 * structure.
 */

#define NET_ULTRA_STATE_EXIST	0x1	/* The adapter exists and its control
					 * registers can be accessed. */
#define NET_ULTRA_STATE_LOAD	0x2	/* The ucode is being downloaded. */
#define	NET_ULTRA_STATE_GO	0x4	/* The go command has been sent. */
#define NET_ULTRA_STATE_INIT	0x8	/* The init command has been sent. */
#define NET_ULTRA_STATE_START  0x10	/* The start command has
					 * been sent and the adapter is
					 * online. */
#define NET_ULTRA_STATE_EPROM	0x20	/* A reset command has just been
					 * sent and the adapter is in
					 * EPROM mode. */
#define NET_ULTRA_STATE_ECHO	0x40	/* Received datagrams are sent
					 * back to sender. */
#define NET_ULTRA_STATE_DSND_TEST 0x80	/* A test of the datagram round-
					 * trip time is in progress. */
#define NET_ULTRA_STATE_SINK 0x100	/* Adapter will sink all received
					 * packets. */
#define NET_ULTRA_STATE_STATS 0x200	/* Collect statistics. */
#define NET_ULTRA_STATE_NORMAL 0x400	/* Adapter is in normal state
					 * (no tests being run). */


/*
 * When an XRB is sent to the adapter we queue up information about it
 * that is used for processing the completion of the XRB.
 */

#define NET_ULTRA_INFO_DATA_SIZE 128

typedef struct NetUltraXRBInfo {
    List_Links		links;		/* Used to queue these up. */
    int			flags;		/* See below. */
    void		(*doneProc)();	/* Called from interrupt handler
					 * when associated XRB is done. */
    ClientData		doneData;	/* Data for doneProc. */
    struct NetUltraXRB	*xrbPtr;	/* XRB associated with this 
					 * structure. */
    int			scatterLength;	/* Size of the scatter/gather array. */
    Net_ScatterGather	*scatterPtr;	/* Ptr to scatter/gather array that
					 * was passed to NetUltraSendReq. */
    int			requestSize;	/* Size of the original request. */
    union NetUltraRequest *requestPtr;	/* Ptr to the original request. */
    Address		buffer;		/* Address of original buffer
					 * if it was remapped. */
} NetUltraXRBInfo;

/*
 * Values for flag field of NetUltraXRBInfo.
 */
#define NET_ULTRA_INFO_PENDING	0x1	/* The XRB associated with the info
					 * is pending completion. */
#define NET_ULTRA_INFO_STD_BUFFER 0x2	/* Buffer is a 'standard' one, meaning
					 * it is in the buffer area pointed
					 * to by the state structure. */
#define NET_ULTRA_INFO_REMAP	0x4	/* If set then the buffer was
					 * remapped to make it contiguous
					 * with the data. */
/*
 * DMA information that controls DMA to and from the adapter.
 * This is part of the XRB.  The id and reference fields are used
 * for virtual circuits.  The adapter may return an XRB that is
 * a request to DMA more data, and the id and reference help the
 * host to set things up.  We don't do virtual circuits yet.
 *
 * The offset field is of unknown use and should be set to 0.
 */

typedef struct NetUltraDMAInfo {
    unsigned char	cmd;		/* The DMA command.  See below. */
    unsigned char	id;		/* Transaction id. Set to 0. */
    unsigned short	reference;	/* VC reference. Set to 0. */
    unsigned int	offset;		/* Buffer offset. Set to 0. */
    NetUltraXRBInfo	*infoPtr;	/* Unused. */
    unsigned int	length;		/* Length of the DMA (size of the
					 * XRB minus this structure). */
} NetUltraDMAInfo;

/* 
 * Definitions for the cmd field of the NetUltraDMA structure. 
 */
#define NET_ULTRA_DMA_CMD_MASK	0xc0 	/* Use this to mask off commands. */
#define	NET_ULTRA_DMA_CMD_XRB	0x80	/* Only XRB is being DMA'ed. */
#define NET_ULTRA_DMA_CMD_XRB_DATA 0xc0	/* An XRB and data is being DMA'ed. */

#define NET_ULTRA_DMA_CMD_FROM_ADAPTER 0x20 /* If this bit is set then the
					  * DMA word is from the adapter
					  * to the host. */
/*
 * The following commands are defined in the Ultranet source but appear
 * to be useful only for channel-based adapters.
 */

#define NET_ULTRA_DMA_CMD_DMA	0x00	/* Adapter asking for data. */
#define	NET_ULTRA_DMA_CMD_DMA_DATA 0x40	/* Adapter sending data to host. */

/*
 * Notes on above definitions.  If you are sending a datagram 
 * set the cmd field to NET_ULTRA_DMA_CMD_XRB_DATA. If you want to receive
 * a datagram set the cmd field to NET_ULTRA_DMA_CMD_XRB.  When the adapter
 * finishes processing an XRB the cmd field will be set to
 * (NET_ULTRA_DMA_CMD_XRB | NET_ULTRA_DMA_CMD_FROM_ADAPTER).  These notes
 * only apply to the Ultranet VME adapter card. Any other setting of
 * the cmd field consitutes an error for that card.
 */


/*
 * Following the DMA information in the XRB is what is referred to as
 * a "stdRB" in the Ultranet documentation. This is a bit confusing
 * because an stdRB is nested in an XRB which is nested in an RB.
 * We refer to the contents of an XRB as a "request" rather than a
 * "request block" to avoid confusion.
 *
 * This structure is request information that is common to all requests.
 */

typedef struct NetUltraRequestHdr {
    unsigned char	cmd;		/* The request command. See below. */
    unsigned char	status;		/* Status. See below. */
    unsigned short	reference;	/* Reference to datagram or
					 * virtual circuit. */
    NetUltraXRBInfo	*infoPtr;	/* Info associated with the XRB. */
    Address		buffer;		/* Data buffer in VME space. */
    int			size;		/* Size of the data buffer. */
} NetUltraRequestHdr;

/*
 * Values for the cmd field of the NetUltraRequestHdr.
 */

#define NET_ULTRA_START_REQ		(unsigned char ) (0x1)
#define NET_ULTRA_STOP_REQ		(unsigned char ) (0x2)
#define NET_ULTRA_NEW_START_REQ		(unsigned char ) (0x3)
#define NET_ULTRA_DGRAM_SEND_REQ	(unsigned char ) (0x80 | 17)
#define NET_ULTRA_DGRAM_RECV_REQ	(unsigned char ) (0x80 | 18)

/*
 * Values for the status field of a NetUltraRequestHdr. These are set
 * by the adapter after it processes a command. Some of them have
 * unknown meaning. The first group (odd values) are considered 
 * successful completion of the command.
 */

#define NET_ULTRA_STATUS_OK				1
#define NET_ULTRA_STATUS_OK_EOM				3
#define NET_ULTRA_STATUS_OK_DECIDE			5
#define NET_ULTRA_STATUS_OK_CLOSED			7
#define NET_ULTRA_STATUS_OK_WITHDRAWN			9
#define NET_ULTRA_STATUS_OK_REJECT_CONNECTION		11
#define NET_ULTRA_STATUS_OK_CONN_REQ			13
#define NET_ULTRA_STATUS_OK_CLOSING			15
#define NET_ULTRA_STATUS_OK_TIMEDOUT			17
#define NET_ULTRA_STATUS_OK_OUT_OF_SEQ			19

#define NET_ULTRA_STATUS_FAIL_INVALID_REQ		2
#define NET_ULTRA_STATUS_FAIL_NO_RESOURCES		4
#define NET_ULTRA_STATUS_FAIL_UNKNOWN_REF		6
#define NET_ULTRA_STATUS_FAIL_BUF_TOO_SMALL		8
#define NET_ULTRA_STATUS_FAIL_BUF_TOO_LONG		10
#define NET_ULTRA_STATUS_FAIL_ILLEGAL_REQ		12
#define NET_ULTRA_STATUS_FAIL_REM_ABORT			14
#define NET_ULTRA_STATUS_FAIL_LOC_TIMEOUT		16
#define NET_ULTRA_STATUS_FAIL_UNKNOWN_CONN_CLASS	18
#define NET_ULTRA_STATUS_FAIL_DUP_REQ			20
#define NET_ULTRA_STATUS_FAIL_CONN_REJECT		22
#define NET_ULTRA_STATUS_FAIL_NEGOT_FAILED		24
#define NET_ULTRA_STATUS_FAIL_ILLEGAL_ADDRESS		26
#define NET_ULTRA_STATUS_FAIL_NETWORK_ERROR		28
#define NET_ULTRA_STATUS_FAIL_PROTOCOL_ERROR		30
#define NET_ULTRA_STATUS_FAIL_ILLEGAL_RB_LENGTH		32
#define NET_ULTRA_STATUS_FAIL_UNKNOWN_SAP_ID		34
#define NET_ULTRA_STATUS_FAIL_ZERO_RB_ID		36
#define NET_ULTRA_STATUS_FAIL_ADAPTER_DOWN		38

/*
 * The following are the formats for request block (RB) that are sent
 * to the adapter. We only use a few of them.
 */

/*
 * The request format for a datagram request.  This structure has the
 * same format as a Net_UltraHeader so the two can be interchanged.
 */

typedef struct NetUltraDatagramRequest {
    NetUltraRequestHdr	hdr;		/* Request header. */
    Net_UltraTLAddress	remoteAddress;	/* Address of remote host. */
    Net_UltraTLAddress	localAddress;	/* Address of local host. */
    unsigned short	options;	/* Unused. */
    unsigned short	quality;	/* Unused. */
    unsigned int	pad3;
} NetUltraDatagramRequest;

/*
 * Define the number and pending reads and writes on an interface.
 */

#define NET_ULTRA_PENDING_READS		NET_ULTRA_NUM_TO_HOST
#define NET_ULTRA_PENDING_WRITES	NET_ULTRA_NUM_TO_ADAPTER	

/* 
 * Request format for a start (NEW-START in Ultra doc) command.
 */

typedef struct NetUltraStartRequest {
    NetUltraRequestHdr	hdr;		/* Request header. */

    /* Host to adapter fields. */
    unsigned short	sequence;	/* Sequence number. */
    unsigned short	softwareRel;	/* Software release. */
    unsigned long	flags;		/* See below. */
    unsigned short	hostType;	/* Type of host. See below. */
    unsigned short	hostOS;		/* Host operating system. See below. */
    unsigned short	hostHW;		/* Host hardware.  See below. */
    unsigned char	sendTimeout;	/* Send timeout in seconds. */
    unsigned char	recvTimeout;	/* Recv timeout in seconds. */
    unsigned char	beatPeriod;	/* Seconds between heartbeats
					 * (0 = no heartbeats) */
    unsigned char	hostMaxBytes;	/* Maximum packet size allowed by
					 * host (as a power of 2). */
    unsigned char	pad1[14];	/* Padding. */
    unsigned char	netAddressSize;	/* sizeof(Net_Address).*/
    unsigned char	netAddressBuf[sizeof(Net_Address)]; /* Net address 
					 * (station address). */
    unsigned char	pad2[15 - sizeof(Net_Address)]; /* Allow a
					 * net address to grow. */
    /* Adapter to host fields. */
    unsigned short	ucodeRel;	/* Micro-code release */
    unsigned short	adapterType;	/* Type of adapter. See below. */
    unsigned short	adapterHW;	/* Adapter hardware info. See below. */
    unsigned short	maxVC;		/* Maximum virtual circuits. */
    unsigned char	maxRECV;	/* Maximum pending RECVs. */
    unsigned char	maxDRCV;	/* Maximum pending DRCVs. */
    unsigned char	maxERCV;	/* Maximum pending ERCVs. */
    unsigned char	maxSEND;	/* Maximum pending SEND/SEOMs. */
    unsigned char	maxDSND;	/* Maximum pending DSNDs. */
    unsigned char	maxESND;	/* Maximum pending ESNDs. */
    unsigned char	slot;		/* Slot number in 1000 hub. */
    unsigned char	speed;		/* Line speed (MBytes/sec). */
    unsigned char	adapterMaxBytes;/* Maximum packet size allowed by
					 * adapter (as a power of 2). */
    unsigned char	pad3[31];
} NetUltraStartRequest;

/*
 * Request format for a stop command.
 */
typedef struct NetUltraStopRequest {
    NetUltraRequestHdr	hdr;		/* Request header. */
    /*
     * Nothing here. 
     */
} NetUltraStopRequest;


/*
 * Values for the flags field of a NetUltraStartRequest.
 */
#define NET_ULTRA_START_FLAGS_COPYDATA	1  /* Run in copydata mode, good
					    * only for HSX adapters. */
/* 
 * Values for the hostType field of a NetUltraStartRequest.
 */

#define NET_ULTRA_START_HOSTTYPE_FB		1  /* Ultra Frame Buffer. */
#define NET_ULTRA_START_HOSTTYPE_CRAYXMP	2  /* Cray X-MP. */
#define NET_ULTRA_START_HOSTTYPE_CRAY2		3  /* Cray 2 */
#define NET_ULTRA_START_HOSTTYPE_ALL2		4  /* Alliant w/ 2 MB VME */
#define NET_ULTRA_START_HOSTTYPE_ALL20		5  /* Alliant w/ 20 MB VME */
#define NET_ULTRA_START_HOSTTYPE_CONVEX		6  /* Convex. */
#define NET_ULTRA_START_HOSTTYPE_SUN		7  /* Sun. */
#define NET_ULTRA_START_HOSTTYPE_IBM		8  /* Some sort of IBM */
#define NET_ULTRA_START_HOSTTYPE_SGI		9  /* SGI Iris */
#define NET_ULTRA_START_HOSTTYPE_MIPS		10 /* MIPS */
#define NET_ULTRA_START_HOSTTYPE_UCRAY		11 /* MicroCray */

/*
 * Values for the adapterType field of a NetUltraStartRequest.
 */

#define NET_ULTRA_START_ADAPTYPE_HSX	1	/* HSX host adapter. */
#define NET_ULTRA_START_ADAPTYPE_FB	2	/* Ultra Frame Buffer. */
#define NET_ULTRA_START_ADAPTYPE_VME	3	/* VME host adapter. */
#define NET_ULTRA_START_ADAPTYPE_LINK	4	/* Link adapter. */
#define NET_ULTRA_START_ADAPTYPE_HSC	5	/* HSC host adapter. */
#define NET_ULTRA_START_ADAPTYPE_LSX	6	/* LSX host adapter. */
#define NET_ULTRA_START_ADAPTYPE_BMC	7	/* BMC host adapter. */

/*
 * Union of all the different requests.
 */
typedef union {
    NetUltraDatagramRequest	dgram;	/* Datagram request. */
    NetUltraStartRequest	start;	/* Start request. */
    NetUltraStopRequest		stop;	/* Stop request. */
} NetUltraRequest;


/*
 * Format of the Request Blocks that are sent to the adapter. 
 * These are referred to as XRBs (Transmitted Request Blocks) in the
 * Ultranet documentation.  Note that there is an extra field (filled)
 * at the beginning of our XRB.  The Ultranet source has an extra
 * structure (rbqe) that is allocated in dma space and consists of their XRB and
 * the filled flag.  The XRB is set up elsewhere and copied into the rbqe.
 * We avoid that by putting the filled flag in the XRB itself.
 */

typedef	struct NetUltraXRB{
    Boolean		filled;	/* TRUE means the XRB has valid contents. */
    NetUltraDMAInfo	dma;	/* The DMA information. */
    NetUltraRequest	request; /* The request itself. */
} NetUltraXRB;


/*
 * The following are a list of the opcodes for the various commands
 * that can be sent to the Ultranet adapter.  The commands are different
 * from the requests in that they are not enqueued.  The adapter is
 * pointed at them directly and modifies their contents (when appropriate)
 * directly.  In general the commands can only be sent after a reset
 * (while in EPROM mode).
 */

#define NET_ULTRA_INIT_OPCODE		1	/* Initialize */
#define NET_ULTRA_LOAD_OPCODE		2	/* Load ucode. */
#define NET_ULTRA_GO_OPCODE		3	/* Start execution. */
#define NET_ULTRA_DIAG_OPCODE		6	/* Diagnostics. */
#define NET_ULTRA_EXT_DIAG_OPCODE	7	/* Extended diagnostics. */
#define NET_ULTRA_INFO_OPCODE		8	/* Get adapter info */

/*
 * Command block for the initialization command. 
 */
typedef struct	NetUltraInitCommand {
    int		opcode;		/*  NET_ULTRA_INIT_OPCODE*/
    int		reply;		/* Returned by adapter. See below. */
    int		version;	/* Adapter software version number. */
    int		toAdapterAddr;	/* Queue of XRBs that are sent to the adapter.*/
    int		toAdapterNum;	/* Number of elements in the queue. */
    int		toHostAddr;	/* Queue of XRBs that are sent to the host.*/
    int		toHostNum;	/* Number of elements in the queue. */
    int		XRBSize;	/* Size of an XRB. */
    char	priority;	/* Interrupt priority. */
    char	vector;		/* Interrupt vector number. */
    char	requestLevel;	/* VME bus request level. */
    char	addressSpace;	/* VME address space for queues and buffers. */
    char	pad[28];	/* Needs to be 64 bytes long. */
} NetUltraInitCommand;

/*
 * Values for the reply field of the NetUltraInitCommand.  The adapter
 * sets this after it receives the init command.
 */

#define NET_ULTRA_INIT_OK	1	/* Adapter initialized correctly. */
#define NET_ULTRA_BAD_SIZE	2	/* The XRBSize is wrong. */

/*
 * Command block for the diagnostic command. 
 */

typedef struct NetUltraDiagCommand {
    int	opcode;				/* NET_ULTRA_DIAG_OPCODE. */
    int	reply;				/* Returned by adapter. See below. */
    unsigned long 	version;	/* Firmware version. */
    unsigned long	error;		/* Error code. 0 if all tests passed,
					 * number of test that failed 
					 * otherwise. */
    unsigned long	hwModel;	/* Hardware model. */
    unsigned long	hwVersion;	/* Hardware version. */
    unsigned long	hwRevision;	/* Hardware revision. */
    unsigned long	hwOption;	/* Hardware option. */
    unsigned long	hwSerial;	/* Hardware serial number. */
} NetUltraDiagCommand;

/*
 * Values for the reply field of the NetUltraDiagCommand.  The adapter
 * sets this after it receives the command.
 */

#define NET_ULTRA_DIAG_OK	1	/* Command completed ok. */


/*
 * Command block for the command to get the adapter info. For now this is
 * the same structure as the diagnostic command (the difference between
 * the two commands is that this one doesn't cause the diagnostics to
 * be run.
 */

typedef NetUltraDiagCommand NetUltraInfoCommand;

/*
 * Values for the reply field of the NetUltraInfoCommand.  The adapter
 * sets this after it receives the command.
 */

#define NET_ULTRA_INFO_OK	1	/* Command completed ok. */

/*
 * Command block for the Extended Diagnostics command. 
 */

typedef struct NetUltraExtDiagCommand {
    int			opcode;		/* NET_ULTRA_EXT_DIAG_OPCODE. */
    int			reply;		/* Returned by adapter. See below. */
    unsigned long	version;	/* Firmware version. */
    unsigned long	error;		/* Error code. 0 if all tests passed,
					 * otherwise the number of test that
					 * failed. */ 
    unsigned long	externalLoopback; /* 0 => do internal loopback,
					 * otherwise do external loopback */
    unsigned long	bufferAddress;	/* VME address of 8k buffer in which
					 * to run tests. */
} NetUltraExtDiagCommand;

/*
 * Reply values for the NetUltraExtDiagCommand.
 */

#define NET_ULTRA_EXT_DIAG_OK	1	/* Command completed ok. */

/*
 * Size of the buffer for the extended diagnostics. 
 */

#define NET_ULTRA_EXT_DIAG_BUF_SIZE 	0x2000	/* 8KB. */

/*
 * Command block for the command to download micro-code.
 */

typedef struct NetUltraLoadCommand {
    int			opcode;		/* NET_ULTRA_LOAD_OPCODE. */
    int			reply;		/* Returned by adapter. See below. */
    unsigned long	dataAddress;	/* VME address of the ucode data. */
    unsigned long	loadAddress;	/* Address at which adapter should
					 * load the ucode. */
    unsigned long	length;		/* Length of the ucode data. */
    char		*unused;	/* NOT USED. */
} NetUltraLoadCommand;

/*
 * Reply values for the NetUltraLoadCommand.
 */

#define NET_ULTRA_LOAD_OK	1	/* Command completed ok. */

/*
 * Command block for the go command.
 */

typedef struct NetUltraGoCommand {
    int			opcode;		/* NET_ULTRA_GO_OPCODE. */
    int			reply;		/* Returned by adapter. See below. */
    unsigned long	startAddress;	/* Start of execution. */
} NetUltraGoCommand;

/*
 * Reply values for the NetUltraGoCommand.
 */

#define NET_ULTRA_GO_OK		1 	/* Command completed ok. */

/*
 * Amount of time (in microseconds) to wait for the adapter to reset. 
 */

#define NET_ULTRA_RESET_DELAY 5 * 1000 * 1000

/*
 * Amount of time (in microseconds) to wait for the adapter to process a
 * command.
 */


#define NET_ULTRA_DELAY	 15 * 1000 * 1000 /* Some things take a long time. */

/*
 * General routines.
 */

extern ReturnStatus NetUltraInit _ARGS_((Net_Interface *interPtr));
extern void NetUltraIntr _ARGS_((Net_Interface *interPtr, Boolean polling));
extern ReturnStatus NetUltraSendCmd _ARGS_((NetUltraState *statePtr, int ok, 
			int size, Address cmdPtr));
extern void NetUltraOutput _ARGS_((Net_Interface *interPtr, 
			Net_UltraHeader *ultraHdrPtr, 
			Net_ScatterGather *scatterGatherPtr, 
			int scatterGatherLength));
extern ReturnStatus NetUltraIOControl _ARGS_((Net_Interface *interPtr, 
			Fs_IOCParam *ioctlPtr, Fs_IOReply *replyPtr));
extern void Net_UltraReset _ARGS_((Net_Interface *interPtr));
extern void Net_UltraHardReset _ARGS_((Net_Interface *interPtr));
extern ReturnStatus NetUltraPendingRead _ARGS_((Net_Interface *interPtr, 
			int size, Address buffer));
extern ReturnStatus NetUltraInfo _ARGS_((NetUltraState *statePtr,
			NetUltraInfoCommand *cmdPtr));
extern ReturnStatus NetUltraExtDiag _ARGS_((NetUltraState *statePtr, 
			Boolean external, char buffer[], 
			NetUltraExtDiagCommand *cmdPtr));
extern ReturnStatus NetUltraSendReq _ARGS_((NetUltraState *statePtr, 
			void (*doneProc)(), ClientData data, int scatterLength,
			Net_ScatterGather *scatterPtr, int requestSize, 
			NetUltraRequest *requestPtr));
#endif /* _NETULTRAINT */

