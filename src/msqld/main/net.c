/*
** Copyright (c) 1995-2001  Hughes Technologies Pty Ltd.  All rights
** reserved.  
**
** Terms under which this software may be used or copied are
** provided in the  specific license associated with this product.
**
** Hughes Technologies disclaims all warranties with regard to this 
** software, including all implied warranties of merchantability and 
** fitness, in no event shall Hughes Technologies be liable for any 
** special, indirect or consequential damages or any damages whatsoever 
** resulting from loss of use, data or profits, whether in an action of 
** contract, negligence or other tortious action, arising out of or in 
** connection with the use or performance of this software.
**
**
** $Id: net.c,v 1.10 2005/06/25 20:50:53 bambi Exp $
**
*/

/*
** Module	: main : net_server
** Purpose	: Server side of the network based IPC code
** Exports	: 
** Depends Upon	: 
*/



/**************************************************************************
** STANDARD INCLUDES
**************************************************************************/

#include <common/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef HAVE_STRING_H
#  include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif


/**************************************************************************
** MODULE SPECIFIC INCLUDES
**************************************************************************/

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>

#ifdef HAVE_STDARG_H
#  include <stdarg.h>
#else
#  include <varargs.h>
#endif

#include <common/msql_defs.h>
#include <common/debug/debug.h>
#include <msqld/index/index.h>
#include <msqld/includes/msqld.h>

/**************************************************************************
** GLOBAL VARIABLES
**************************************************************************/

int 	netCurrentSock = -1;


/**************************************************************************
** PRIVATE ROUTINES
**************************************************************************/


/**************************************************************************
** PUBLIC ROUTINES
**************************************************************************/


#ifndef EINTR
#  define EINTR 0
#endif

#if defined(_OS_WIN32)
#  include <winsock.h>
#  define NET_READ(fd,b,l) recv(fd,b,l,0)
#  define NET_WRITE(fd,b,l) send(fd,b,l,0)
#else
#  define NET_READ(fd,b,l) read(fd,b,l)
#  define NET_WRITE(fd,b,l) write(fd,b,l)
#endif


static 	u_char	packetBuf[PKT_LEN + 4];
static	int	readTimeout;
u_char	*packet = NULL;


static void intToBuf(cp,val)
        u_char  *cp;
        int     val;
{
        *cp++ = (unsigned int)(val & 0x000000ff);
        *cp++ = (unsigned int)(val & 0x0000ff00) >> 8;
        *cp++ = (unsigned int)(val & 0x00ff0000) >> 16;
        *cp++ = (unsigned int)(val & 0xff000000) >> 24;
}


static int bufToInt(cp)
        u_char  *cp;
{
        int val;
 
        val = 0;
        val = *cp++;
        val += ((int) *cp++) << 8 ;
        val += ((int) *cp++) << 16;
        val += ((int) *cp++) << 24;
        return(val);
}


#if defined(_OS_WIN32)
void initWinsock()
{	
	WORD	wVersion;
	WSADATA	wsaData;

	wVersion = MAKEWORD(1,1);
	WSAStartup(wVersion, &wsaData);
}
#endif


void netInitialise()
{
	packet = (u_char *)packetBuf + 4;
#if defined(_OS_WIN32)
	initWinsock();
#endif
}



int netWritePacket(fd)
	int	fd;
{
	int	len,
		offset,
		remain,
		numBytes;

	netCurrentSock = fd;
	len = strlen((char *)packet);
	intToBuf(packetBuf,len);
	offset = 0;
	remain = len+4;
	while(remain > 0)
	{
		numBytes = NET_WRITE(fd,packetBuf + offset, remain);
		if (numBytes == -1 && errno != EINTR)
		{
			return(-1);
		}
		offset += numBytes;
		remain -= numBytes;
	}
	return(0);
}



int netReadPacket(fd)
	int	fd;
{
	u_char	 buf[4];
	int	len,
		remain,
		offset,
		numBytes;
	fd_set	fdset;
	struct	timeval timeout;

	netCurrentSock = fd;
	numBytes = 0;
	remain = 4;
	offset = 0;
	readTimeout = 0;
	while(remain > 0)
	{
		FD_ZERO(&fdset);
		FD_SET(fd, &fdset);
		timeout.tv_sec = 10;
		timeout.tv_usec = 0;
		if (select(fd+1, &fdset,NULL,NULL,&timeout) == 0)
			return(-1);
		if (!readTimeout)
		{
			numBytes = NET_READ(fd,buf + offset,remain);
			if (numBytes < 0 && errno != EINTR)
			{
				fprintf(stderr,"Socket read on %d for length failed : ",fd);
				perror("");
			}
			if (numBytes <= 0)
         			return(-1);
		}
		if (readTimeout)
			break;
		remain -= numBytes;
		offset += numBytes;
		
	}
	len = bufToInt(buf);
	if (len > PKT_LEN)
	{
		fprintf(stderr,"Packet too large (%d)\n", len);
		return(-1);
	}
	if (len < 0)
	{
		fprintf(stderr,"Malformed packet\n");
		return(-1);
	}
	remain = len;
	offset = 0;
	while(remain > 0)
	{
		FD_ZERO(&fdset);
		FD_SET(fd, &fdset);
		timeout.tv_sec = 10;
		timeout.tv_usec = 0;
		if (select(fd+1, &fdset,NULL,NULL,&timeout) == 0)
			return(-1);
		numBytes = NET_READ(fd,packet+offset,remain);
		if (numBytes <= 0)
		{
         		return(-1);
		}
		remain -= numBytes;
		offset += numBytes;
	}
	*(packet + len) = 0;
        return(len);
}


void netOK(sock)
	int	sock;
{
	msqlDebug1(MOD_GENERAL,"Sending OK on sock %d\n",sock);
	strcpy((char*)packet,"1:\n");
	netWritePacket(sock);
}



#ifdef HAVE_STDARG_H
void netError(int sock, ...)
#else
void netError(va_alist)
	va_dcl
#endif
{
	va_list	args;
	char	*fmt;

#ifdef HAVE_STDARG_H
	va_start(args, sock);
#else
	int	sock;

	va_start(args);
	sock = (int) va_arg(args,int);
#endif
	fmt = (char *)va_arg(args,char *);

	strcpy((char*)packet,"-1:");
	vsprintf((char*)(packet+3),fmt,args);
	va_end(args);
	msqlDebug2(MOD_GENERAL,"Sending Error on sock %d '%s'\n",sock,packet+3);
	netWritePacket(sock);
}

void netEndOfList(sock)
	int	sock;
{
	strcpy((char*)packet,"-100:\n");
	netWritePacket(sock);
}


#ifdef NOT_DEF

/***********************************************************************
 * 
 * This section of code contains machine-specific code for handling integers.
 * mSQL supports 32 bit 2's complement integers.  To hide the details of
 * converting integers for specific machines, the routines packInt32() and
 * unpackInt32() were written.  If you have a machine that has native ints
 * other than 32 bit 2's complement, you must either write your own versions
 * of packInt32() and unpackInt32(), or modify the supplied ones.  For any
 * machine using 2's complement ints, simple changes to the
 * BYTES_PER_INT, HIGH_BITS, HIGH_BITS_MASK, and SIGN_BIT_MASK macros should
 * make your code work.  If you have something else, you're on your own ...
 *
 ************************************************************************/

#if _CRAY
#define BYTES_PER_INT	8
#endif

#ifndef BYTES_PER_INT
#define BYTES_PER_INT	4		/* default.  most boxes fit here */
#endif

#if BYTES_PER_INT == 8
#define BIG_INTS	1
#define HIGH_BITS	32			/* bits-per-int minus 32 */
#define HIGH_BITS_MASK	0xffffffff00000000	/* mask of the high bits */
#define SIGN_BIT_MASK	0x0000000080000000      /* mask of your sign bit */
#endif

#if BYTES_PER_INT == 4
#define BIG_INTS      	0
#endif

/*
 * Pack a native integer into a character buffer.  The buffer is assumed
 * to be at least 4 bytes long.
 */

static int packInt32(num, buf)
	int	num;
	char	*buf;
{
#if BIG_INTS
	num <<= HIGH_BITS;
#endif

	bcopy4((char *)&num, buf);
	return 0;
}

/*
 * Extract a native integer from a character buffer.  The buffer is assumed
 * to have been formatted using the packInt32() routine.
 */

static int unpackInt32(buf)
	char	*buf;
{
	int	num;

	bcopy4(buf, (char *)&num);

#if BIG_INTS
	num >>= HIGH_BITS;

	if (num & SIGN_BIT_MASK) {
		num |= HIGH_BITS_MASK;
	}
#endif

	return num;
}

#endif
