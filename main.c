#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <termios.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include "verbose.h"
#include "mbus.h"

#define MAX_RX_SIZE 255

#if defined(__APPLE__)
#define DEFAULT_SERIAL_DEVICE "/dev/cu.usbserial-AC01H0TM"
#else
#define DEFAULT_SERIAL_DEVICE "/dev/ttyAMA0"
#endif

char mbus_device[60];
#define RECEIVE_VTIME 1 // 1/10 sekunden

int fd;
uint8_t mbus_tg[MAX_RX_SIZE];
int mbus_bytes = 0;
int wakeup_size = 722; // 722 x 0x55 bei 2400 baud 8N1 = 3,0 sek.
int wakeup_pause = 350; // ms

unsigned char prim_adr = 0xFE; // für interaktives Menü
unsigned char prim_adr_list[10]; // für READOUT_ALL, HISTORY_ALL
int prim_adr_count = 0;

int mbus_speed = B2400;
unsigned char frame = 0x00;
int baud = 2400;
int human = 1;
int unit = 1;

enum
{
	SND_NKE = 0, REQ_UD, REQ_UD2, REQ_A6, SET_PRIM_ADR, SET_BAUDRATE, SET_FRAME, READOUT, READOUT_ALL, HISTORY, HISTORY_ALL, WAKEUP, TEST, NONE
};

void print_request(char* text, unsigned char* data, int length);
void print_history(int primary_adr, history_entry* h);
int request_history(unsigned char primary_adr, history_entry* history);
int request_history_frame(unsigned char primary_adr, unsigned char frame, history_entry* history);
void interactive();


void anleitung()
{
	printf("\n_________________________________\n");
	printf("COMMANDS with wakeup sequence:\n");
	printf("\t0: SND_NKE\n");
	printf("\t1: REQ_UD\n");
	printf("\t2: REQ_UD2\n");
	printf("\t3: REQ_A6\n");
	printf("\t4: SET_PRIM_ADR\n");
	printf("\t5: SET_BAUDRATE\n");
	printf("\t6: SND_UD with 1 byte frame data\n");
	printf("\t7: READOUT\n");
	printf("\t8: READOUT from all\n");
	printf("\t9: HISTORY\n");
	printf("\t^: HISTORY from all\n\n");

	printf("COMMANDS without wakeup sequence:\n");
	printf("\tq: wakeup sequence\n");
	printf("\tw: SND_NKE\n");
	printf("\te: REQ_UD\n");
	printf("\tr: REQ_UD2\n");
	printf("\tt: Request history data from prim_adr\n");
	printf("\tz: Request history frame from prim_adr\n");
	printf("\tu: SET_PRIM_ADR\n");
	printf("\ti: SET_BAUDRATE\n\n");

	printf("Settings:\n");
	printf("\t+: Wakeup + 10 chars\t%d\n", wakeup_size);
	printf("\t-: Wakeup - 10 chars\n");

	printf("\ta: Pause + 10 ms\t%d\n", wakeup_pause);
	printf("\ty: Pause - 10 ms\n");

	printf("\ts: Prim.Adr + 1\t\t0x%02X\n", prim_adr);
	printf("\tx: Prim.Adr - 1\n");

	printf("\td: frame + 1\t\t0x%02X\n", frame);
	printf("\tc: frame - 1\n");
	printf("\tf: next baudrate\t%d\n", baud);
	printf("\tv: verbose level\n");
	printf("\tQ: Beenden\n");
	printf("_________________________________\n");

}

int usage(char *progname, int quit)
{
	printf("MBUS Testprogramm für optische Module\n");
	printf("getestet mit Allmess (ITRON) Wärmemengenzählern\n\n");
	printf("usage:\t%s\n", progname);
	printf("\t%s [ -a address ] [ -b baudrate ] [ -d device ] [ -v ] [ -m ] [ -u ] { -i | -n | -r | -R | -y | -Y | -t }\n", progname);
	printf("-a\tprimary address (multiple -a for a list, max. 10) (default 254=0xFE), ex.: \"-a 1 -a 2 -a 3\"\n");
	printf("-b\tset baud rate (300, 1200, 2400, 9600) (default 2400)\n");
	printf("-d\tserial device\n");
	printf("-h\tthis help\n");
	printf("-i\tinteractive mode (default if no args)\n");
	printf("-v\tset verbosity (two v->more verbose)\n");
	printf("-m\tmachine parsable output\n");
	printf("-u\twithout units\n");
	printf("-n\tsend SND_NKE\n");
	printf("-r\tsend REQ_UD2 (READOUT all data with frame = 0)\n");
	printf("-R\tsend REQ_UD2 to all(READOUT all data with frame = 0)\n");
	printf("-y\trequest history data\n");
	printf("-Y\trequest history data from all\n");
	printf("-t\ttest mode - send infinite 0x55\n");

	if (quit)
		exit(1);
	return 0;
}

int set_interface_attribs(int speed, int parity, int bits)
{
	struct termios tty;
	memset(&tty, 0, sizeof tty);
	if (tcgetattr(fd, &tty) != 0)
	{
		fprintf(stderr, "error %d from tcgetattr: %s\n", errno, strerror(errno));
		return -1;
	}

	cfsetospeed(&tty, speed);
	cfsetispeed(&tty, speed);

	tty.c_cflag &= ~CSIZE;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~(PARENB | PARODD);
	tty.c_cflag |= bits | parity | CREAD | CLOCAL;
	/* CREAD            characters may be read */
	/* CLOCAL         ignore modem state, local connection */

	tty.c_iflag = 0;
//	tty.c_iflag &= ~(IXON | IXOFF | IXANY);
//	tty.c_iflag &= ~(ICANON | ECHO | ECHOE | ISIG);
		tty.c_iflag |= INPCK;
	//tty.c_iflag |= PARMRK; //(IGNPAR);

	tty.c_oflag = 0;
	tty.c_lflag = 0;

	tty.c_cc[VMIN] = 0;		// mindestens n Zeichen empfangen
	tty.c_cc[VTIME] = RECEIVE_VTIME;		// Wartezeit von 1/10 s = 0,2 s
	
	if (tcsetattr(fd, TCSANOW, &tty) != 0)
	{
		fprintf(stderr, "error %d from tcsetattr\n", errno);
		return -1;
	}
	return 0;
}

/*void set_blocking(int should_block)
 {
 struct termios tty;
 memset(&tty, 0, sizeof(tty));
 if (tcgetattr(fd, &tty) != 0)
 {
 printf("error %d from tggetattr: %s\n", errno, strerror(errno));
 return;
 }

 tty.c_cc[VMIN] = should_block ? 1 : 0;
 tty.c_cc[VTIME] = 20;            // 2 seconds read timeout

 if (tcsetattr(fd, TCSANOW, &tty) != 0)
 printf("error %d setting term attributes\n", errno);
 }*/

void mbus_sleep(unsigned long m)
{
	// sleep millisekunden
	struct timespec sleeptime =
	{ .tv_sec = 0, .tv_nsec = m * 1000000 };

	if (nanosleep(&sleeptime, NULL) != 0)
		fprintf(stderr, "Error in nanosleep: %d: %s\n", errno, strerror(errno));
	else
	{
		verbose(2, "nanosleep: %ld\n", m);
	}

}

int wakeup()
{
	size_t ret_out;
	int tx = 0;
	unsigned char seq[8];

	memset(seq, MBUS_WAKEUP_CHAR, sizeof(seq));

	// Umschalten auf 8E1
	verbose(1, "Port Modus 8N1\n");
	set_interface_attribs(mbus_speed, 0, CS8);

	verbose(1, "wakeup(): Sende Aufwachsequenz\n");

	while (tx < wakeup_size)
	{
		ret_out = write(fd, seq, sizeof(seq));
		if (ret_out == -1)
		{
			if (errno == EINTR)
				continue;

			fprintf(stderr, "Error writing data: %d: %s\n", errno, strerror(errno));
			return -1;
		}
		tx += ret_out;
	}

	verbose(1, "Pause %d ms\n", wakeup_pause);
	long pause_gesamt = ((int) (((float) wakeup_size) * 4.1666)) + wakeup_pause;

	int sekunden = pause_gesamt / 1000;
	unsigned long millisekunden = pause_gesamt - (sekunden * 1000);

	struct timespec sleeptime =
	{ .tv_sec = sekunden, .tv_nsec = millisekunden * 1000000 };

	if (nanosleep(&sleeptime, NULL) != 0)
		fprintf(stderr, "Error in nanosleep: %d: %s\n", errno, strerror(errno));

	// Umschalten auf 8E1
	verbose(1, "Port Modus 8E1\n");
	set_interface_attribs(mbus_speed, PARENB, CS8);

//	printf("Leere rx buffer\n");
//	tcflush(fd, TCIFLUSH); /* Discards old data in the and rx buffer */

	return tx;
}

int snd_nke(unsigned char address)
{
	size_t ret_out;
	unsigned char seq[] =
	{ 0x10, 0x40, address, 0x00, 0x16 };

	// Checksumme berechnen
	for (int i = 1; i < sizeof(seq) - 2; i++)
		seq[sizeof(seq) - 2] += seq[i];

	print_request("snd_nke(): Sende Initialisierung\n", seq, sizeof(seq));

	ret_out = write(fd, seq, sizeof(seq));  // send snd_nke
	if (ret_out == -1)
	{
		if (errno == EINTR)
			snd_nke(address); // repeat write call
		else
		{
			fprintf(stderr, "Error writing data: %d: %s\n", errno, strerror(errno));
			return -1;
		}
	}

	return ret_out;
}

int snd_ud(unsigned char address)
{
	size_t ret_out;
	unsigned char seq[] =
	{ 0x68, 0x03, 0x03, 0x68, 0x53, address, 0xA6, 0x00, 0x16 };

	// Checksumme berechnen
	for (int i = 4; i < sizeof(seq) - 2; i++)
		seq[sizeof(seq) - 2] += seq[i];

	print_request("snd_ud(): Sende Anwenderdaten\n", seq, sizeof(seq));

	ret_out = write(fd, seq, sizeof(seq));  // send snd_ud
	if (ret_out == -1)
	{
		if (errno == EINTR)
			snd_ud(address); // repeat write call
		else
		{
			fprintf(stderr, "Error writing data: %d: %s\n", errno, strerror(errno));
			return -1;
		}
	}

	return ret_out;
}

int snd_ud_frame(unsigned char address, unsigned char frame)
{
	size_t ret_out;
	unsigned char seq[] =
	{ 0x68, 0x04, 0x04, 0x68, 0x53, address, 0x50, frame, 0x00, 0x16 };

	// Checksumme berechnen
	for (int i = 4; i < sizeof(seq) - 2; i++)
		seq[sizeof(seq) - 2] += seq[i];

	verbose(1, "snd_ud_frame(): Setze Datenrahmen: [%02X]\n", frame);
	print_request(0, seq, sizeof(seq));

	ret_out = write(fd, seq, sizeof(seq));  // send snd_ud
	if (ret_out == -1)
	{
		if (errno == EINTR)
			snd_ud_frame(address, frame); // repeat write call
		else
		{
			fprintf(stderr, "Error writing data: %d: %s\n", errno, strerror(errno));
			return -1;
		}
	}

	return ret_out;
}

int req_ud2(unsigned char address)
{
	size_t ret_out;
	unsigned char seq[] =
	{ 0x10, 0x5B, address, 0x00, 0x16 };

	// Checksumme berechnen
	for (int i = 1; i < sizeof(seq) - 2; i++)
		seq[sizeof(seq) - 2] += seq[i];

	print_request("req_ud2(): Sende Anfrage Daten Klasse 2\n", seq, sizeof(seq));

	ret_out = write(fd, seq, sizeof(seq));  // send req_ud2
	if (ret_out == -1)
	{
		if (errno == EINTR)
			req_ud2(address); // repeat write call
		else
		{
			fprintf(stderr, "Error writing data: %d: %s\n", errno, strerror(errno));
			return -1;
		}
	}

	return ret_out;
}

int req_A6(unsigned char address)
{
	size_t ret_out;
	unsigned char seq[] =
	{ 0x68, 0x03, 0x03, 0x68, 0x53, address, 0xA6, 0x00, 0x16 };

	// Checksumme berechnen
	for (int i = 4; i < sizeof(seq) - 2; i++)
		seq[sizeof(seq) - 2] += seq[i];

	print_request("req_A6(): Sende Anfrage Daten\n", seq, sizeof(seq));

	ret_out = write(fd, seq, sizeof(seq));
	if (ret_out == -1)
	{
		if (errno == EINTR)
			req_A6(address); // repeat write call
		else
		{
			printf("Error writing data: %d: %s\n", errno, strerror(errno));
			return -1;
		}
	}

	return ret_out;
}

/*int select_slave()
 {
 / *
 68 0B 0B 68 53 FD 52 NN NN NN NN HH HH ID MM CS 16
 Response: E5 (only if filter matches)
 Structure of filter:
 4-byte BCD NN (serial number) $F digit joker
 2-byte HST HH (manufacturer code) $FF byte joker
 1-byte ID (SHARKY 775: $2F) ID (identification code) $FF joker
 1-byte SMED MM (medium code) $FF joker
 * /
 #define NN1	0x17
 #define NN2	0x42
 #define NN3	0x52
 #define NN4	0x11
 #define HH1	0xFF
 #define HH2	0xFF
 #define ID		0xFF
 #define MM	0xFF

 size_t ret_out;
 unsigned char seq[] =
 { 0x68, 0x0B, 0x0B, 0x68, 0x53, 0xFD, 0x52, NN1, NN2, NN3, NN4, HH1, HH2, ID, MM, 0x00, 0x16 };

 // Checksumme berechnen
 for (int i = 4; i < sizeof(seq) - 2; i++)
 {
 seq[sizeof(seq) - 2] += seq[i];
 }

 print_request("select_slave()", seq, sizeof(seq));

 ret_out = write(fd, seq, sizeof(seq));
 if (ret_out == -1)
 {
 if (errno == EINTR)
 select_slave(); // repeat write call
 else
 {
 fprintf(stderr, "Error writing data: %d: %s\n", errno, strerror(errno));
 return -1;
 }
 }

 return ret_out;
 }*/

int set_prim_adr(unsigned char address)
{
#define DIF	0x01
#define VIF		0x7A
	size_t ret_out;
	unsigned char seq[] =
	{ 0x68, 0x06, 0x06, 0x68, 0x73, 0xFE, 0x51, DIF, VIF, address, 0x00, 0x16 };

	// Checksumme berechnen
	for (int i = 4; i < sizeof(seq) - 2; i++)
		seq[sizeof(seq) - 2] += seq[i];

	verbose(1, "set_prim_adr(): Setze Primäradresse: 0x%02X\n", address);
	print_request(0, seq, sizeof(seq));

	ret_out = write(fd, seq, sizeof(seq));
	if (ret_out == -1)
	{
		if (errno == EINTR)
			set_prim_adr(address); // repeat write call
		else
		{
			printf("Error writing data: %d: %s\n", errno, strerror(errno));
			return -1;
		}
	}

	return ret_out;
}

int set_baudrate(unsigned char address, int baud)
{
	size_t ret_out;
	int code;
	switch (baud)
	{
		case 300:
			code = 0xB8;
			break;
		case 1200:
			code = 0xBA;
			break;
		case 2400:
			code = 0xBB;
			break;
		case 9600:
			code = 0xBD;
			break;
		default:
			printf("Error setting baudrate: Unknow value %d\n", baud);
			return -1;
	}

	unsigned char seq[] =
	{ 0x68, 0x03, 0x03, 0x68, 0x73, address, code, 0x00, 0x16 };

	// Checksumme berechnen
	for (int i = 4; i < sizeof(seq) - 2; i++)
		seq[sizeof(seq) - 2] += seq[i];

	verbose(1, "set_baudrate(): Setze Baudrate: %d\n", baud);
	print_request(0, seq, sizeof(seq));

	ret_out = write(fd, seq, sizeof(seq));
	if (ret_out == -1)
	{
		if (errno == EINTR)
			set_baudrate(address, baud); // repeat write call
		else
		{
			printf("Error writing data: %d: %s\n", errno, strerror(errno));
			return -1;
		}
	}

	return ret_out;
}

void print_request(char* text, unsigned char* data, int length)
{
	if (text != 0)
		verbose(1, text);

	if (getVerbose() >= 2)
	{
		for (int i = 0; i < length; i++)
			printf("[%02X]", data[i]);

		printf("\n");
	}
}

int listen(unsigned char* data, int lenght, int max_lenght)
{
	int mbus_bytes_received = 0, data_pos = 0, i;
	unsigned char rxbuffer[MAX_RX_SIZE];
	int timeout = 2;
	/*	struct timespec delay =
	 { .tv_sec = 0, .tv_nsec = 10000000 }; // 10 ms
	 */
	while (1)
	{
		mbus_bytes_received = read(fd, rxbuffer, MAX_RX_SIZE);
		if (mbus_bytes_received == -1)
		{
			fprintf(stderr, "error %d on reading data from serial device\n", errno);
			break;
		}

		if (mbus_bytes_received == 0)
		{
			if (--timeout)
				continue;
			else
				break;
		}

		verbose(2, "mbus_bytes_received: %d\n", mbus_bytes_received);
		for (int i = 0; i < mbus_bytes_received; i++)
		{
			verbose(2, "[%02X] ", rxbuffer[i]);
		}
		verbose(2, "\n");

		for (i = 0; i < mbus_bytes_received; i++)
		{
			if (data_pos < max_lenght)
				data[data_pos++] = rxbuffer[i];
			else
				return data_pos;

			if (data_pos == lenght)
			{
				return data_pos;
			}
		}
	}

	return data_pos;
}

int readout()
{
	int z = 0, error;
	allmess_zaehler zaehler;
	memset(&mbus_tg, 0, sizeof(mbus_tg));

// Frame 0 setzen
	snd_ud_frame(prim_adr, 0);

// Antwort muss 0xE5 (MBUS_FRAME_ACK_START) sein
	z = listen(mbus_tg, 1, MAX_RX_SIZE);
	if (z != 1 || mbus_tg[0] != MBUS_FRAME_ACK_START)
	{
		// Fehler, kein ACK empfangen
		fprintf(stderr, "error: no ACK, frame %0X\n", frame);
		return MBUS_RECV_RESULT_ERROR;
	}
	else
		verbose(1, "ACK empfangen\n");

// Abfrage
	memset(&mbus_tg, 0, sizeof(mbus_tg));
	req_ud2(prim_adr);

// Empfang
	z = listen(mbus_tg, MAX_RX_SIZE, MAX_RX_SIZE);
	if (z > 0)
	{
		verbose(1, "Empfang: %d bytes\n", z);
		error = mbus_parse_telegram(mbus_tg, z, &zaehler);
		if (error < 0)
		{
			fprintf(stderr, "Error: %d\n", error);
			return error;
		}
		else
		{
			mbus_print(&zaehler, human, unit);
		}
	}

	return 0;
}

int readout_all()
{
	int error;

	for (int i = 0; i < prim_adr_count; i++)
	{
		// Anfrage senden
		prim_adr = 	prim_adr_list[i];
		error = readout();
		if (error < 0)
			return error;

		// evtl. Pause zwischen den Zählern
	}
	return 0;
}

int request_history_all()
{
	history_entry history[MBUS_HISTORY_ENTRYS];
	int error;

	for (int i = 0; i < prim_adr_count; i++)
	{
		// Anfrage senden
		error = request_history(prim_adr_list[i], history);
		if (error < 0)
		{
			fprintf(stderr, "error: %d, primary address: %d\n", error, prim_adr_list[i]);
			return error;
		}

		print_history(prim_adr_list[i], history);

		// evtl. Pause zwischen den Zählern
	}
	return 0;
}

int test()
{
	printf("sending test data 0x55...\n");
	
	size_t ret_out;
//	int tx = 0;
	unsigned char seq[8];

	memset(seq, MBUS_WAKEUP_CHAR, sizeof(seq));

	// Umschalten auf 8E1
	verbose(1, "Port Modus 8N1\n");
	set_interface_attribs(mbus_speed, 0, CS8);


	while (1)
	{
		ret_out = write(fd, seq, sizeof(seq));
		if (ret_out == -1)
		{
			if (errno == EINTR)
				continue;

			fprintf(stderr, "Error writing data: %d: %s\n", errno, strerror(errno));
			return -1;
		}
//		tx += ret_out;
	}

	// Umschalten auf 8E1
	verbose(1, "Port Modus 8E1\n");
	set_interface_attribs(mbus_speed, PARENB, CS8);
		
	return 0;
}

void print_history(int primary_adr, history_entry* h)
{
	// Trennzeichen einstellbar
	char c = ';';

	if (!human)
	{
		printf("primary_address:%d", primary_adr);
		
		for (int i = 0; i < MBUS_HISTORY_ENTRYS; i++)
		{
			// index	date	time	energy (kWh)	volume (l)
			printf("%c%d", c, i);
			printf("%c%02d.%02d.%d", c, h->date.tm_mday, h->date.tm_mon + 1, h->date.tm_year + 1900);
			printf("%c%02d:%02d:%02d", c, h->date.tm_hour, h->date.tm_min, h->date.tm_sec);
			printf("%c%d", c, h->energy);
			printf("%c%d", c, h->volume);
			h++;
		}
		printf("\n");
		return;
	}

	printf("\nPrimary address: %d\n\n", primary_adr);
	printf("index\tdate\ttime\tenergy (kWh)\tvolume (l)\n");

	for (int i = 0; i < MBUS_HISTORY_ENTRYS; i++)
	{
		printf("%d\t", i);
		printf("%02d.%02d.%d\t", h->date.tm_mday, h->date.tm_mon + 1, h->date.tm_year + 1900);
		printf("%02d:%02d:%02d\t", h->date.tm_hour, h->date.tm_min, h->date.tm_sec);
		printf("%d\t", h->energy);
		printf("%d\n", h->volume);
		h++;
	}
	printf("\n");
}

int request_history(unsigned char primary_adr, history_entry* history)
{
	int error, i = 0;

	for (int frame = MBUS_FIRST_HISTORY_FRAME; frame <= MBUS_LAST_HISTORY_FRAME; frame++)
	{
// Anfrage senden
		error = request_history_frame(primary_adr, frame, &(history[i++]));
		if (error < 0)
		{
			return error;
		}

		mbus_sleep(100);
	}

	return 0;
}

int request_history_frame(unsigned char primary_adr, unsigned char frame, history_entry* history)
{
	int z = 0, error;
	allmess_zaehler zaehler;
	memset(&mbus_tg, 0, sizeof(mbus_tg));

// Frame setzen
	snd_ud_frame(primary_adr, frame);

// Antwort muss 0xE5 (MBUS_FRAME_ACK_START) sein
	z = listen(mbus_tg, 1, MAX_RX_SIZE);
	if (z != 1 || mbus_tg[0] != MBUS_FRAME_ACK_START)
	{
// Fehler, kein ACK empfangen
		fprintf(stderr, "error: no ACK, frame %0X\n", frame);
		return MBUS_RECV_RESULT_ERROR;
	}
	else
	{
		verbose(1, "ACK empfangen\n");
		mbus_sleep(100);
	}

// Abfrage
	memset(&mbus_tg, 0, sizeof(mbus_tg));
	req_ud2(primary_adr);

// Empfang
	z = listen(mbus_tg, MAX_RX_SIZE, MAX_RX_SIZE);
	if (z > 0)
	{
		verbose(1, "Empfang: %d bytes\n", z);
		error = mbus_parse_telegram(mbus_tg, z, &zaehler);
		if (error < 0)
		{
			fprintf(stderr, "Error: %d\n", error);
			return error;
		}
		else
		{
			history->date = zaehler.date;
			history->energy = zaehler.energy;
			history->volume = zaehler.volume;
		}
	}

	return 0;
}

int execute_command(int command, int wake)
{
	int ret = 0, num = 0, error;
	char c;
	allmess_zaehler zaehler;
	history_entry h[MBUS_HISTORY_ENTRYS];

	if (command == SET_PRIM_ADR)
	{
		if (prim_adr >= 0x01 && prim_adr <= 0xFA)
		{
			printf("WARNUNG! Es darf nur EIN Wärmezähler angeschlossen sein!\nFortfahren (y/n)?\n");
			c = getchar();
			if (c != 'y')
				return -1;
		}
		else
		{
			printf("Fehler: Zu setzende Primäradresse nicht gültig\n");
			return -1;
		}
	}

	if (wake)
	{
		ret = wakeup();
		if (ret < 0)
			return ret;
	}

	switch (command)
	{
		case NONE:
			return 0;
		case SND_NKE:
			ret = snd_nke(prim_adr);
			num = 1;
			break;
		case REQ_UD:
			break;
		case REQ_UD2:
			ret = req_ud2(prim_adr);
			num = MAX_RX_SIZE;
			break;
		case REQ_A6:
			ret = req_A6(prim_adr);
			num = MAX_RX_SIZE;
			break;
		case SET_PRIM_ADR:
			ret = set_prim_adr(prim_adr);
			break;
		case SET_BAUDRATE:
			set_baudrate(prim_adr, baud);
			num = 1;
			break;
		case SET_FRAME:
			snd_ud_frame(prim_adr, frame);
			num = 1;
			break;
		case READOUT:
			return readout();
		case READOUT_ALL:
			return readout_all();
		case HISTORY:
			error = request_history(prim_adr, h);
			if (error == 0)
				print_history(prim_adr, h);
			return error;
		case HISTORY_ALL:
			return request_history_all();
		case TEST:
			return test();
	}

	verbose(1, "%d bytes sent\n", ret);
	ret = listen(mbus_tg, num, MAX_RX_SIZE);
	verbose(1, "%d byte(s) received\n", ret);

	if (ret == 0)
		return -1;

	if (ret == 1 && mbus_tg[0] == MBUS_FRAME_ACK_START)
	{
		verbose(1, "ACK received\n");
		return 0;
	}

	error = mbus_parse_telegram(mbus_tg, ret, &zaehler);
	if (error < 0)
	{
		fprintf(stderr, "Error: %d\n", error);
		return error;
	}
	else
		mbus_print(&zaehler, human, unit);

	return 0;
}

int main(int argc, char *argv[])
{
	int index, c, error = 0;
	int command = NONE;

//	opterr = 0;
	memset(&mbus_device, 0, 50);

	while ((c = getopt(argc, argv, "a:b:d:hmivnurRyYt")) != -1)
		switch (c)
		{
			case 'a':
				errno = 0;
				int adr = strtol(optarg, 0, 0);

				if (adr < 0 || adr > 255 || errno != 0)
				{
					fprintf(stderr, "Error: Invald primary address (not in range 0-255)\n");
					return -1;
				}
				
				if (prim_adr_count == 0)
					prim_adr = (unsigned char) adr;

				if (prim_adr_count < 10)
					prim_adr_list[prim_adr_count++] = adr;

				verbose(1, "Using primary address %d\n", prim_adr_list[prim_adr_count-1]);
				break;
			case 'b':
				if (strcmp(optarg, "300") == 0)
					mbus_speed = B300;
				else if (strcmp(optarg, "1200") == 0)
					mbus_speed = B1200;
				else if (strcmp(optarg, "2400") == 0)
					mbus_speed = B2400;
				else if (strcmp(optarg, "9600") == 0)
					mbus_speed = B9600;
				else
				{
					fprintf(stderr, "Error: Invald speed (not in range 300-9600)\n");
					return -1;
				}
				break;
			case 'd':
				strncpy(mbus_device, optarg, 50);
				break;
			case 'h':
				usage(argv[0], true);
				return 0;
			case 'm':
				human = 0;
				break;
			case 'u':
				unit = 0;
				break;
			case 'v':
				setVerbose(getVerbose() + 1);
				break;
			case 'i':
				command = NONE;
				break;
			case 'n':
				command = SND_NKE;
				break;
			case 'r':
				command = READOUT;
				break;
			case 'R':
				command = READOUT_ALL;
				break;
			case 'y':
				command = HISTORY;
				break;
			case 'Y':
				command = HISTORY_ALL;
				break;
			case 't':
				command = TEST;
				break;
			case ':': /* Option without required operand */
				fprintf(stderr, "option -%c requires an arg\n", optopt);
				break;
			case '?':
				usage(argv[0], true);
		}

	for (index = optind; index < argc; index++)
		fprintf(stderr, "Non-option argument %s\n", argv[index]);

	if (getVerbose() > 1)
	{
		if (sizeof(size_t) == 4)
			printf("32 bit system\n");
		else if (sizeof(size_t) == 8)
			printf("64 bit system\n");

		/*printf("Size uint8_t=\t%lu\n", sizeof(uint8_t));
		 printf("Size uint16_t=\t%lu\n", sizeof(uint16_t));
		 printf("Size uint32_t=\t%lu\n", sizeof(uint32_t));
		 printf("Size uint64_t=\t%lu\n", sizeof(uint64_t));
		 printf("Size int=\t%lu\n", sizeof(int));
		 printf("Size long=\t%lu\n", sizeof(long));
		 printf("Size long long=\t%lu\n", sizeof(long long));*/
	}

	if (strlen(mbus_device) == 0)
		strncpy(mbus_device, DEFAULT_SERIAL_DEVICE, 50);
	//verbose(1, "verbosity level is %d\n", getVerbose());

	int speed = 0;
	if (mbus_speed == B300)
		speed = 300;
	else if (mbus_speed == B1200)
		speed = 1200;
	else if (mbus_speed == B2400)
		speed = 2400;
	else if (mbus_speed == B9600)
		speed = 9600;

	verbose(1, "serial device: %s with %d baud\n", mbus_device, speed);
	if (prim_adr != MBUS_ADDRESS_BROADCAST_REPLY)
		verbose(1, "primary address: 0x%02X\n", prim_adr);

	struct termios old_tio, new_tio;

	/* get the terminal settings for stdin */
	tcgetattr(STDIN_FILENO, &old_tio);

	/* we want to keep the old setting to restore them a the end */
	new_tio = old_tio;

	/* disable canonical mode (buffered i/o) and local echo */
	new_tio.c_lflag &= (~ICANON & ~ECHO);

	/* set the new settings immediately */
	tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

	fd = open(mbus_device, O_RDWR | O_NOCTTY/* | O_SYNC | O_NDELAY | O_NONBLOCK*/);
	if (fd == -1)
	{
		fprintf(stderr, "error opening serial device %s: %s)\n", mbus_device, strerror(errno));
		tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
		return -1;
	}

	set_interface_attribs(mbus_speed, PARENB, CS8);

	if (command == NONE)
	{
		usage(argv[0], false);
		anleitung();
		setVerbose(1);
		interactive();
	}
	else
	{
		error = execute_command(command, 1);
	}

	close(fd);
	/* restore the former settings */
	tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

	return error;
}

void interactive()
{
	char c;
	int command, wakeup;

	while (1)
	{
		tcflush(fd, TCIOFLUSH); /* Discards old data in the tx and rx buffer */
		mbus_bytes = 0;
		memset(&mbus_tg, 0, sizeof(mbus_tg));
		wakeup = 0;

		c = getchar();

		if (c == 'Q')
			break;

		if (c == 'h' || c == ' ')
		{
			anleitung();
			continue;
		}

		if (c == '+')
		{
			wakeup_size += 10;
			printf("wakeup_size = %d, time: %d ms\n", wakeup_size, (int) (((float) wakeup_size) * 4.1666));
			continue;
		}
		else if (c == '-')
		{
			if (wakeup_size > 10)
				wakeup_size -= 10;
			printf("wakeup_size = %d, time: %d ms\n", wakeup_size, (int) (((float) wakeup_size) * 4.1666));
			continue;
		}
		else if (c == 'a')
		{
			wakeup_pause += 10;
			printf("wakeup_pause = %d ms\n", wakeup_pause);
			continue;
		}
		else if (c == 'y')
		{
			if (wakeup_pause > 10)
				wakeup_pause -= 10;
			printf("wakeup_pause = %d ms\n", wakeup_pause);
			continue;
		}
		else if (c == 's')
		{
			prim_adr += 1;
			printf("prim_adr = 0x%02X\n", prim_adr);
			continue;
		}
		else if (c == 'x')
		{
			prim_adr -= 1;
			printf("prim_adr = 0x%02X\n", prim_adr);
			continue;
		}
		else if (c == 'd')
		{
			frame += 1;
			printf("frame = 0x%02X\n", frame);
			continue;
		}
		else if (c == 'c')
		{
			frame -= 1;
			printf("frame = 0x%02X\n", frame);
			continue;
		}
		else if (c == 'f')
		{
			switch (baud)
			{
				case 300:
					baud = 1200;
					break;
				case 1200:
					baud = 2400;
					break;
				case 2400:
					baud = 9600;
					break;
				case 9600:
					baud = 300;
					break;
			}
			printf("baudrate = %d\n", baud);
			continue;
		}
		else if (c == 'v')
		{
			int level = getVerbose();
			if (++level > 2)
				level = 0;

			setVerbose(level);
			printf("verbose: %d\n", level);
			continue;
		}
		else if (c >= '0' && c <= '9')
		{
			command = c - '0';
			wakeup = 1;
		}
		else if (c == '^')
		{
			command = HISTORY_ALL;
			wakeup = 1;
		}
		else
		{
			switch (c)
			{
				case 'q':
					command = NONE;
					wakeup = 1;
					break;
				case 'w':
					command = SND_NKE;
					break;
				case 'e':
					command = REQ_UD;
					break;
				case 'r':
					command = REQ_UD2;
					break;
				case 't':
					command = HISTORY;
					break;
				case 'z':
					command = HISTORY_ALL;
					break;
				case 'u':
					command = SET_PRIM_ADR;
					break;
				case 'i':
					command = SET_BAUDRATE;
					break;
				default:
					continue;
			}
		}

		execute_command(command, wakeup);
	}
}
