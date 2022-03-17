/*
 * fix-cec: Automatically turn on/off projector connected to Denon AVR
 * Copyright (c) 2022 Stuart McLaren
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

/* Note: This code is based on https://github.com/glywood/cec-fix */

/* Change the IP address below to the IP address of your AVR */

#define IPADDR "192.168.1.45"

/* for hdmi */
#include <bcm_host.h>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

enum simode {
    si_unknown = -1,
    /*
     * Note: numbers here are completely arbitrary
     */
    dvd    = 1,
    game   = 2,
    aux1   = 3,
    aux2   = 4,
    cbl    = 5,
    bluray = 6,
    cd     = 7,
    tuner  = 8,
    phono  = 9,
    tv     = 10, /* TV Audio */
    /*
     * Note: For non-video sources
     * You can use numbers over 100 to prevent certain
     * sources from triggering the project to power on.
     * Eg to prevent "tuner", "phono", and "tv" sources
     * from powering on the projector, change the
     * lines above to be:
     *
     * tuner = 108,
     * phono = 109,
     * tv    = 110,
    */
};

enum projstate {
    proj_unknown = -1,
    off = 0,
    on = 1,
};

int serial_fd;
simode SI = si_unknown;
bool want_on = 0;
projstate PROJSTATE = proj_unknown;

/* for socket */
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAXLINE 4096

static int read_cnt;
static char *read_ptr;
static char read_buf[MAXLINE];

void cec_off() {
	uint8_t bytes[1];
	bytes[0] = CEC_Opcode_Standby;
	if (vc_cec_send_message(CEC_AllDevices_eTV,
			       	bytes, 1, VC_FALSE) != 0) {
		std::cerr << "Failed to press Power Off." << std::endl;
	}

}

void power_off_projector() {
	int i = 0;
	while (1){
		if (PROJSTATE == off){
			std::cerr << "Successfully turned off the projector" << std::endl;
			return;
		}
		i++;
		if (i == 60){
			std::cerr << "Failed to turn off the projector: attempt " << i << std::endl;
			std::cerr << "PROJSTATE -> unknown " << std::endl;
			PROJSTATE = proj_unknown;
			return;
		}
		std::cerr << "Turning off the projector: attempt " << i << std::endl;
		cec_off();
		usleep(500000); // microseconds
	}
}

void cec_on() {
	uint8_t bytes[1];
	bytes[0] = CEC_Opcode_ImageViewOn;
	if (vc_cec_send_message(CEC_AllDevices_eTV,
				bytes, 1, VC_FALSE) != 0) {
		std::cerr << "Failed to press Power On." << std::endl;
	} else {
		std::cerr << "Successfully sent cec power on message to projector " << std::endl;
		std::cerr << "PROJSTATE -> on " << std::endl;
		PROJSTATE = on;
	}
}

void power_on_projector() {
	if (PROJSTATE == on) {
		std::cerr << "Projector already on. Skipping power on." << std::endl;
		return;
	}
	/* tuner/phono/anything we don't want to turn on projector, use enum > 100 */
	if (SI > 100) {
		std::cerr << "Non-video source. Skipping power on." << "SI=" << SI << std::endl;
		return;
	}

	std::cerr << "Powering on projector." << "SI=" << SI << std::endl;
	cec_on();
}

void cec_callback(void *callback_data, uint32_t reason, uint32_t param1, uint32_t param2, uint32_t param3, uint32_t param4) {
	std::cerr << "Got a callback!" << std::endl << std::hex <<
		"reason = 0x" << reason << std::endl <<
		"param1 = 0x" << param1 << std::endl <<
		"param2 = 0x" << param2 << std::endl <<
		"param3 = 0x" << param3 << std::endl <<
		"param4 = 0x" << param4 << std::endl;

	VC_CEC_MESSAGE_T message;
	if (vc_cec_param2message(reason, param1, param2, param3, param4,
				 &message) == 0) {
		std::cerr << std::hex <<
			"Translated to message i=" << message.initiator <<
			" f=" << message.follower <<
			" len=" << message.length <<
			" content=" << (uint32_t)message.payload[0] <<
			" " << (uint32_t)message.payload[1] <<
			" " << (uint32_t)message.payload[2] << std::endl;

		// Detect when the projector is being told to turn on. Check the power
		// status of the receiver, because if it's not on we'll want to
		// turn it on.
		if (message.length == 1 &&
		    message.payload[0] == CEC_Opcode_ImageViewOn) {
			/*
			std::cerr << "ImageViewOn, checking power status of receiver." << std::endl;
			uint8_t bytes[1];
			bytes[0] = CEC_Opcode_GiveDevicePowerStatus;
			if (vc_cec_send_message(CEC_AllDevices_eAudioSystem,
					       	bytes, 1, VC_FALSE) != 0) {
				std::cerr << "Failed to check power status." << std::endl;
			}
			want_on = true;
			*/
		}
		if (message.length == 2 &&
		    message.payload[0] == CEC_Opcode_SetSystemAudioMode && (uint32_t)message.payload[1] == 1) {
				std::cerr << "XXX got CEC_Opcode_SetSystemAudioMode on" << " " << param1 << " " << (uint32_t)message.payload[2] << std::endl;
				if (message.follower == 0 ) {
					std::cerr << "power on projector" << std::endl;
					//power_on_projector2();
					
				} else {
					std::cerr << "do not power on projector" << std::endl;
				}
				want_on = false;
		}
		if (message.length == 2 &&
		    message.payload[0] == CEC_Opcode_SetSystemAudioMode && (uint32_t)message.payload[1] == 0) {
				std::cerr << "XXX got CEC_Opcode_SetSystemAudioMode off" << " " << param1 << " " << (uint32_t)message.payload[2] << std::endl;
				if ( message.follower == 0xf ) {
					std::cerr << "power off projector" << std::endl;
					//power_off_projector();
					
				} else {
					std::cerr << "do not power off projector" << std::endl;
				}
				want_on = true;
		}

		if (message.length == 2 &&
		    message.initiator == CEC_AllDevices_eAudioSystem &&
		    message.payload[0] == CEC_Opcode_ReportPowerStatus &&
		    want_on) {
			std::cerr << "Receiver has power status " << (int)message.payload[1] << ". (0=on, 1=off, 2=on_pending, 3=off_pending)" << std::endl;
			if (message.payload[1] == CEC_POWER_STATUS_ON ||
			    message.payload[1] == CEC_POWER_STATUS_ON_PENDING) {
				std::cerr << "Receiver is on now." << std::endl;
				//power_on_projector();
				want_on = false;
			} else {
				/*
				std::cerr << "Receiver is off but we want it on." << std::endl;
				uint8_t bytes[2];
				bytes[0] = CEC_Opcode_UserControlPressed;
				bytes[1] = CEC_User_Control_Power;
				if (vc_cec_send_message(CEC_AllDevices_eAudioSystem,
							bytes, 2, VC_FALSE) != 0) {
					std::cerr << "Failed to press Power On." << std::endl;
				}
				*/
			}
		}
		if (message.length == 1 &&
		    message.initiator == CEC_AllDevices_eTV &&
		    message.follower == CEC_BROADCAST_ADDR &&
		    (uint32_t)message.payload[0] == CEC_Opcode_Standby) {
			std::cerr << "Projector broadcast 36 (power off)" << std::endl;
			std::cerr << "PROJSTATE -> off " << std::endl;
			PROJSTATE = off;
		}

		// As soon as the power-on button press is finished sending,
		// also send a button release.
		if ((reason & VC_CEC_TX) &&
		    message.length == 2 &&
		    message.payload[0] == CEC_Opcode_UserControlPressed &&
		    message.payload[1] == CEC_User_Control_Power) {
			/*
			std::cerr << "Power on press complete, now sending release." << std::endl;
			uint8_t bytes[1];
			bytes[0] = CEC_Opcode_UserControlReleased;
			if (vc_cec_send_message(CEC_AllDevices_eAudioSystem,
						bytes, 1, VC_FALSE) != 0) {
				std::cerr << "Failed to release Power On." << std::endl;
			}
			*/
		}

		// As soon as the power-on button release is finished sending,
		// query the power status again.
		if ((reason & VC_CEC_TX) &&
		    message.length == 1 &&
		    message.payload[0] == CEC_Opcode_UserControlReleased) {
			/*
			std::cerr << "Power on release complete, now querying power status." << std::endl;
			uint8_t bytes[1];
			bytes[0] = CEC_Opcode_GiveDevicePowerStatus;
			if (vc_cec_send_message(CEC_AllDevices_eAudioSystem,
						bytes, 1, VC_FALSE) != 0) {
				std::cerr << "Failed to release Power On." << std::endl;
			}
			*/
		}

		// Detect when the projector is being told to go into standby. It
		// ignores that command, so force it to power off using the
		// serial port instead.
		//
		/*
		if (message.follower == 0 &&
		    message.length == 1 &&
		    message.payload[0] == CEC_Opcode_Standby) {
			power_off_projector();
		}
		*/
	}
}

void tv_callback(void *callback_data, uint32_t reason, uint32_t p0, uint32_t p1) {
	std::cerr << "Got a TV callback!" << std::endl << std::hex <<
		"reason = 0x" << reason << std::endl << 
		"param0 = 0x" << p0 << std::endl <<
		"param1 = 0x" << p1 << std::endl;
}

static ssize_t
my_read(int fd, char *ptr)
{

    if (read_cnt <= 0) {
      again:
        if ( (read_cnt = read(fd, read_buf, sizeof(read_buf))) < 0) {
            if (errno == EINTR)
                goto again;
            return (-1);
        } else if (read_cnt == 0)
            return (0);
        read_ptr = read_buf;
    }

    read_cnt--;
    *ptr = *read_ptr++;
    return (1);
}


ssize_t
readline(int fd, char *vptr, size_t maxlen)
{
    size_t n, rc;
    char    c, *ptr;

    ptr = vptr;
    for (n = 1; n < maxlen; n++) {
        if ( (rc = my_read(fd, &c)) == 1) {
            *ptr++ = c;
            if (c  == '\r')
                break;          /* newline is stored, like fgets() */
        } else if (rc == 0) {
            *ptr = 0;
            return (n - 1);     /* EOF, n - 1 bytes were read */
        } else
            return (-1);        /* error, errno set by read() */
    }

    vptr[n-1]  = '\0';                  /* null terminate like fgets() */
    return (n);
}

void handleDenonProtocolMessage(char *read_buf){
		std::cerr << "DPSI: SI=" << SI << std::endl;
		std::cerr << "DP: " << read_buf << std::endl;
		if (strcmp("SITUNER", read_buf) == 0) {
			if (SI == si_unknown ){
				/* This was triggered by initial SI? */
				SI = tuner;
				return;
			}
			SI = tuner;
			power_on_projector();
		} else if (strcmp("SIPHONO", read_buf) == 0) {
			if (SI == si_unknown ){
				/* This was triggered by initial SI? */
				SI = phono;
				return;
			}
			SI = phono;
			power_on_projector();
		} else if (strcmp("SITV", read_buf) == 0) {
			/* This may or may not have been triggered by initial SI? */
			if (SI == si_unknown ){
				/* This was triggered by initial SI? */
				SI = tv;
				return;
			}
			SI = tv;
			power_on_projector();
		} else if (strcmp("SIDVD", read_buf) == 0) {
			if (SI == si_unknown ){
				/* This was triggered by initial SI? */
				SI = dvd;
				return;
			}
			SI = dvd;
			power_on_projector();
		} else if (strcmp("SIGAME", read_buf) == 0) {
			if (SI == si_unknown ){
				/* This was triggered by initial SI? */
				SI = game;
				return;
			}
			SI = game;
			power_on_projector();
		} else if (strcmp("SIAUX1", read_buf) == 0) {
			if (SI == si_unknown ){
				/* This was triggered by initial SI? */
				SI = aux1;
				return;
			}
			SI = aux1;
			power_on_projector();
		} else if (strcmp("SIAUX1", read_buf) == 0) {
			if (SI == si_unknown ){
				/* This was triggered by initial SI? */
				SI = aux2;
				return;
			}
			SI = aux2;
			power_on_projector();
		} else if (strcmp("SISAT/CBL", read_buf) == 0) {
			if (SI == si_unknown ){
				SI = cbl;
				return;
			}
			SI = cbl;
			power_on_projector();
		} else if (strcmp("SIBD", read_buf) == 0) {
			if (SI == si_unknown ){
				/* This was triggered by initial SI? */
				SI = bluray;
				return;
			}
			SI = bluray;
			power_on_projector();
		} else if (strcmp("SICD", read_buf) == 0) {
			if (SI == si_unknown ){
				/* This was triggered by initial SI? */
				SI = cd;
				return;
			}
			SI = cd;
			power_on_projector();
		} else if (strcmp("PWON", read_buf) == 0) {
			power_on_projector();
		} else if (strcmp("PWSTANDBY", read_buf) == 0) {
			power_off_projector();
		} else {
			std::cerr << "DPX: SKIP" << std::endl;
		}
}

int watch_denon() {
	 struct timeval tv;
	 tv.tv_sec = 300 /* timeout_in_seconds */;
	 tv.tv_usec = 0;

         int socket_desc;
	 int n = 0;
         struct sockaddr_in server;

         socket_desc  = socket(AF_INET, SOCK_STREAM, 0);
         if (socket_desc == -1 )
         {
                 printf("Could not create socket");
         }
         server.sin_addr.s_addr = inet_addr(IPADDR);
         server.sin_family = AF_INET;
         server.sin_port = htons( 23 );
	 setsockopt(socket_desc, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

         //Connect to remote server
         if (connect(socket_desc , (struct sockaddr *)&server , sizeof(server)) < 0)
        {
                puts("connect error");
                return 1;
        }

        puts("Connected");
	// Trigger SI INPUT RESPONSE
	// This allows us to detect and populate the current input setting
	if ( write(socket_desc,"SI?",strlen("SI?") + 1) != strlen("SI?") + 1 ) {
                puts("socket write error");
		return 1;
	}

         //Receive a reply from the server
        while(1) {
                n = readline(socket_desc, read_buf, 4096);
		if (n<0){
                	printf("XXX readline error/timeout [%d]\n", n);
			close(socket_desc);
         		socket_desc  = socket(AF_INET, SOCK_STREAM, 0);
         		if (socket_desc == -1 )
         		{
                 		printf("Could not recreate socket");
				return 1;
         		}
         		server.sin_addr.s_addr = inet_addr(IPADDR);
         		server.sin_family = AF_INET;
         		server.sin_port = htons( 23 );
	 		setsockopt(socket_desc, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
         		if (connect(socket_desc , (struct sockaddr *)&server , sizeof(server)) < 0)
        		{
                		puts("XXX reconnect error");
                		return 1;
        		} else {
                		puts("XXX reconnect ok");
			}
		}
		handleDenonProtocolMessage(read_buf);
        }

  return 0;
}

int main(int argc, char *argv[]) {
	bcm_host_init();
	vcos_init();

	VCHI_INSTANCE_T vchi_instance;
	if (vchi_initialise(&vchi_instance) != 0) {
		std::cerr << "Could not initiaize VHCI" << std::endl;
		return 1;
	}

	if (vchi_connect(nullptr, 0, vchi_instance) != 0) {
		std::cerr << "Failed to connect to VHCI" << std::endl;
		return 1;
	}

	vc_vchi_cec_init(vchi_instance, nullptr, 0);

	if (vc_cec_set_passive(VC_TRUE) != 0) {
		std::cerr << "Failed to enter passive mode" << std::endl;
		return 1;
	}

	vc_cec_register_callback(cec_callback, nullptr);
	vc_tv_register_callback(tv_callback, nullptr);

	if (vc_cec_register_all() != 0) {
		std::cerr << "Failed to register all opcodes" << std::endl;
		return 1;
	}

	vc_cec_register_command(CEC_Opcode_GivePhysicalAddress);
	vc_cec_register_command(CEC_Opcode_GiveDeviceVendorID);
	vc_cec_register_command(CEC_Opcode_GiveOSDName);
	vc_cec_register_command(CEC_Opcode_GetCECVersion);
	vc_cec_register_command(CEC_Opcode_GiveDevicePowerStatus);
	vc_cec_register_command(CEC_Opcode_MenuRequest);
	vc_cec_register_command(CEC_Opcode_GetMenuLanguage);
	vc_cec_register_command(CEC_Opcode_SetSystemAudioMode);

	if (vc_cec_set_logical_address(CEC_AllDevices_eRec2, CEC_DeviceType_Rec, CEC_VENDOR_ID_BROADCOM) != 0) {
		std::cerr << "Failed to set logical address" << std::endl;
		return 1;
	}

	watch_denon();
	return 0;
}



