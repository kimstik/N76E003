/**
*  N76flash utility by Wiliam Kaster
*  Visit https://github.com/wkaster/N76E003 for more info
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, version 3.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* General Public License for more details.
*
*
*  Version 1.0: 2022-02-09
*       First working version
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <dirent.h>

// Linux headers
#include <fcntl.h> // Contains file controls like O_RDWR
#include <errno.h> // Error integer and strerror() function
#include <termios.h> // Contains POSIX terminal control definitions
#include <unistd.h> // write(), read(), close()


#define BLOCK_SIZE      16
#define CMD_SOH			0x01    // [SOH] Start of Heading
#define CMD_STX			0x02	// [STX] Start of Text
#define CMD_ETX			0x03	// [ETX] End of Text
#define CMD_EOT         0x04    // [EOT] End of Transmission
#define CMD_ACK			0x06	// [ACK] Acknowledge
#define CMD_NACK		0x15	// [NAK] Negative Acknowledge
#define CMD_SUB         0x1A    // [SUB] Substitute
#define CMD_DEL         0x7F    // [DEL] Delete


unsigned char dallas_crc8(unsigned char*, unsigned char);

void help();

int main(int argc, char** argv)
{
    int nReturnCode = 0;
    bool bFixPort = false;
    bool bFlag = true;
    char szTemp[4];
    char szFileName[256] = "";
    FILE * pFile = NULL;
    long lFileSize;
    int nFileBlocks;
    DIR *dir;
    struct dirent *list;
    char szPortSearch[10] = "ttyUSB";
    char szDefaultPort[10];
    char szFullPortName[15];
    int nPortsFound=0; // number of serial ports found
    int nTries=25;
    unsigned char byWriteBuffer[128];
    unsigned char byReadBuffer;             // just 1 byte needed (usually ack)
    unsigned char byFileBuffer[BLOCK_SIZE];
    unsigned char byCRC;
    int nBytesRead = 0;
    int serial_port;
    int i;
    struct timespec ts;


    if (argc > 1) {
        for(i=1 ; i<argc ; i++) {
            if(strcmp("-search", argv[i]) == 0 || strcmp("-s", argv[i]) == 0) {
                strcpy(szPortSearch,argv[i+1]);
            }
            if(strcmp("-file", argv[i]) == 0 || strcmp("-f", argv[i]) == 0) {
                strcpy(szFileName,argv[i+1]);
            }
            if(strcmp("-tries", argv[i]) == 0 || strcmp("-t", argv[i]) == 0) {
                strcpy(szTemp,argv[i+1]);
                if(isdigit(szTemp[0])) {
                    nTries = atoi(szTemp);
                }
                else {
                    printf("Error: tries parameter is not a number.\n");
                    nReturnCode = 1;
                    goto error;
                }
            }
            if(strcmp("-port", argv[i]) == 0 || strcmp("-p", argv[i]) == 0) {
                bFixPort = true;
                strcpy(szDefaultPort,argv[i+1]);
            }
            if(strcmp("-help", argv[i]) == 0 || strcmp("-h", argv[i]) == 0) {
                help();
                exit(0);
            }
        }
    }
    else {
        help();
        exit(0);
    }

    if(strlen(szFileName)==0) {
        printf("Error: no input file specified.\n");
        nReturnCode = 1;
        goto error;
    }

    if(!bFixPort) {
        dir = opendir("/dev/");
        if(dir) {
            while((list = readdir(dir)) != NULL) {
                if(list->d_type==DT_CHR && strncmp(list->d_name, szPortSearch, strlen(szPortSearch)) == 0) {
                    if(bFlag) {
                        printf("Port(s) Found:\n");
                        bFlag = false;
                    }
                    printf("%s\n", list->d_name);
                    strcpy(szDefaultPort, list->d_name);
                    nPortsFound++;
                }
            }
            closedir(dir);
        }
        if(nPortsFound == 0) {
            printf("Error: no avaliable serial port found.\n");
            nReturnCode = 1;
            goto error;
        }

        if(nPortsFound > 1) {
            printf("Enter port name: ");
            scanf("%s", &szDefaultPort[0]);
        }
    }

    printf("Default Port: %s\n", szDefaultPort);

    strcpy(szFullPortName,"/dev/");
    strcat(szFullPortName,szDefaultPort);

    pFile = fopen(szFileName,"rb");
    if (pFile==NULL) {
        printf("Error %i openening the specified file: %s.\n", errno, strerror(errno));
        exit(1);
    }

    fseek(pFile, 0, SEEK_END);
    lFileSize = ftell(pFile);
    nFileBlocks = (int) lFileSize / BLOCK_SIZE;
    rewind(pFile);

    printf("File: %s (size: %ld bytes)\n", szFileName, lFileSize);

     /* https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/ */
     /* Thanks to gbmhunter */


    // Open the serial port. Change device path as needed (currently set to an standard FTDI USB-UART cable type device)
    serial_port = open(szFullPortName, O_RDWR | O_NOCTTY);

    // Create new termios struc, we call it 'tty' for convention
    struct termios tty;

    // Read in existing settings, and handle any error
    if(tcgetattr(serial_port, &tty) != 0)
    {
        printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
        printf("Sometimes these errors are caused by permissions in /dev/ttyXXX ports!\n");
        printf("Try this command first: sudo chmod o+rw %s\n", szFullPortName);
        nReturnCode = 1;
        goto error;
    }

    tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
    tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
    tty.c_cflag &= ~CSIZE; // Clear all bits that set the data size
    tty.c_cflag |= CS8; // 8 bits per byte (most common)
    tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
    tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO; // Disable echo
    tty.c_lflag &= ~ECHOE; // Disable erasure
    tty.c_lflag &= ~ECHONL; // Disable new-line echo
    tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes

    tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
    tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed


    tty.c_cc[VTIME] = 20;    // Wait for up to 1s (20 deciseconds), returning as soon as any data is received.
    tty.c_cc[VMIN] = 0;

    // Set in/out baud rate to be 19200
    cfsetispeed(&tty, B19200);
    cfsetospeed(&tty, B19200);

    // Save tty settings, also checking for error
    if (tcsetattr(serial_port, TCSANOW, &tty) != 0)
    {
        printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
        nReturnCode = 1;
        goto error;
    }

    byWriteBuffer[0] = CMD_SOH;    // [SOH] Start of Heading: Are you there?

    byReadBuffer=0x00;

    printf("Connecting, please RESET the microcontroller...\n");

    for(i=0;i<=nTries;i++) {

        write(serial_port, byWriteBuffer, 1);

        nBytesRead = read(serial_port, &byReadBuffer, 1);

        if(byReadBuffer==CMD_ACK) {
            printf("Handshake OK!\n");
            byReadBuffer=0x00;
            break;
        }
        else {
            printf("Try %d... %x", i, byReadBuffer);
            if(byReadBuffer != 0x00) {
                byReadBuffer=0x00;              // returning garbage or application data
                ts.tv_sec = 200 / 1000;
                ts.tv_nsec = (200 % 1000) * 1000000;
                nanosleep(&ts, &ts);
                tcflush(serial_port, TCIFLUSH); // clearing for next try
            }
            if(i==nTries) {
                printf ("Give up!\n");
                nReturnCode=1;
            }
            else printf("\r");
        }
    }

    if(nReturnCode==1) goto error;  // gave up connecting

    tty.c_cc[VTIME] = 0;    // Wait for up to 1s (200 deciseconds), returning as soon as any data is received.
    tty.c_cc[VMIN] = 1;


    // Save tty settings, also checking for error
    if (tcsetattr(serial_port, TCSANOW, &tty) != 0)
    {
        printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
        nReturnCode = 1;
        goto error;
    }

    printf("Erasing... ");

    byReadBuffer=0x00;

    byWriteBuffer[0] = CMD_SUB;
    byWriteBuffer[1] = CMD_DEL;

    write(serial_port, &byWriteBuffer, 2);

    nBytesRead = read(serial_port, &byReadBuffer, 1);

    if(byReadBuffer==CMD_ACK) {
        printf("Done!\n");
        byReadBuffer=0x00;
    }
    else {
        printf("Error erasing chip %x.\n", byReadBuffer);
        nReturnCode = 1;
        goto error;
    }

    i=0;
    while(!feof(pFile)) {
        nBytesRead = fread(&byFileBuffer[0], sizeof(unsigned char), BLOCK_SIZE, pFile);

        // fill with 0xFF if needed
        if(nBytesRead < BLOCK_SIZE) {
            memset(&byFileBuffer[nBytesRead], (unsigned char) 0xFF, BLOCK_SIZE - nBytesRead);
            nFileBlocks = i;  // end of file reached: percentage round
        }

        byCRC = dallas_crc8(&byFileBuffer[0],BLOCK_SIZE);

        /* The package */
        byWriteBuffer[0] = CMD_STX;
        memcpy(&byWriteBuffer[1], &byFileBuffer[0], BLOCK_SIZE);
        byWriteBuffer[BLOCK_SIZE + 1] = byCRC;
        byWriteBuffer[BLOCK_SIZE + 2] = CMD_ETX;

        write(serial_port, byWriteBuffer, BLOCK_SIZE + 3);

        nBytesRead = read(serial_port, &byReadBuffer, 1);

        if(byReadBuffer==CMD_ACK) {
            printf("Writing: %d%%", (i*100)/nFileBlocks);
            if(i==nFileBlocks) printf("\n"); else printf("\r");
            byReadBuffer=0x00;
            i++;
        }

        if(byReadBuffer==CMD_NACK) {
            printf("error!\n");
            break;
        }

    }

    printf("Soft reset... ");

    byWriteBuffer[0] = CMD_EOT;

    write(serial_port, byWriteBuffer, 1);

    nBytesRead = read(serial_port, &byReadBuffer, 1);

    if(byReadBuffer==CMD_ACK) {
        printf("Done!\n");
        byReadBuffer=0x00;
    }
    else {
        printf("Error reseting.\n");
        nReturnCode = 1;
        goto error;
    }

    error:

    if(pFile != NULL) fclose(pFile);        //Closing binary file
    close(serial_port);

    return nReturnCode;
}

unsigned char dallas_crc8(unsigned char* data, unsigned char size)
{
    unsigned char crc = 0;
    for ( unsigned char i = 0; i < size; ++i )
    {
        unsigned char inbyte = data[i];
        for ( unsigned char j = 0; j < 8; ++j )
        {
            unsigned char mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if ( mix ) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

void help() {
    printf("Nuvoton N76E003 / MS51xx flash utility V1.0 - Linux version\n");
    printf("Visit https://github.com/wkaster/N76E003 for more info.\n");
    printf("Options:\n");
    printf(" -file [-f] binary file to flash\n");
    printf(" -search [-s] serial port type to search (Default: ttyUSB)\n");
    printf(" -port [-p] fix serial port to use\n");
    printf(" -tries [-t] number of connecting tries. Default is 25 tries of 200ms each\n");
    printf(" -help [-h] this screen\n");
}
