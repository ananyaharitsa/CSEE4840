/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Name/UNI: Please Changeto Yourname (pcy2301)
 */
#include "fbputchar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "usbkeyboard.h"
#include <pthread.h>

/* Update SERVER_HOST to be the IP address of
 * the chat server you are connecting to
 */
/* arthur.cs.columbia.edu */
#define SERVER_HOST "128.59.19.114"
#define SERVER_PORT 42000

#define BUFFER_SIZE 128

/*
 * References:
 *
 * https://web.archive.org/web/20130307100215/http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 *
 * http://www.thegeekstuff.com/2011/12/c-socket-programming/
 * 
 */

int sockfd; /* Socket file descriptor */

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);

void clear_screen() {
    int row, col;
    for (row = 0; row < 24; row++) {
        for (col = 0; col < 64; col++) {
            fbputchar(' ', row, col);
        }
    }
}

void draw_layout() {
    int col;
    for (col = 0; col < 64; col++) {
        fbputchar('-', 21, col); // Draw horizontal line
    }
}


/* Keycode to ASCII conversion table */

static const char keycode_to_ascii[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z',
    'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0
};

void display_message(const char *msg) {
    static int current_line = 0;
    int col = 0;
    
    // Clear the current line before writing
    memset(screen_buffer[current_line], ' ', MAX_COLS);
    
    while (*msg && col < MAX_COLS) {
        screen_buffer[current_line][col] = *msg;
        fbputchar(*msg, current_line, col);
        msg++;
        col++;
    }

    current_line = (current_line + 1) % 20; // Scroll messages
}

void display_input() {
    int col;
    // Clear input area
    for (col = 0; col < 64; col++) {
        fbputchar(' ', 22, col);
    }

    // Display user input
    for (col = 0; col < cursor_pos; col++) {
        fbputchar(input_buffer[col], 22, col);
    }

    // Display cursor
    fbputchar('_', 22, cursor_pos);
}

void process_keypress(uint8_t keycode) {
    if (keycode == 0x2A) {  // Backspace
        if (cursor_pos > 0) {
            cursor_pos--;
            input_buffer[cursor_pos] = '\0';
        }
    } else if (keycode == 0x28) {  // Enter
        input_buffer[cursor_pos] = '\0';
        write(sockfd, input_buffer, strlen(input_buffer)); // Send message
        display_message(input_buffer); // Display sent message
        cursor_pos = 0; // Reset input
        memset(input_buffer, 0, MAX_COLS);
    } else if (cursor_pos < MAX_COLS - 1) {
        char ascii = keycode_to_ascii[keycode];
        if (ascii) {
            input_buffer[cursor_pos++] = ascii;
        }
    }
    display_input();
}

int main()
{
  int err, col;

  struct sockaddr_in serv_addr;

  struct usb_keyboard_packet packet;
  int transferred;
  char keystate[12];

  if ((err = fbopen()) != 0) {
    fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
    exit(1);
  }
  clear_screen();
  draw_layout();
	
  fbputs("Chat Client - CSEE 4840", 1, 10);

  /* Open the keyboard */
  if ( (keyboard = openkeyboard(&endpoint_address)) == NULL ) {
    fprintf(stderr, "Did not find a keyboard\n");
    exit(1);
  }
    
  /* Create a TCP communications socket */
  if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
    fprintf(stderr, "Error: Could not create socket\n");
    exit(1);
  }

  /* Get the server address */
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SERVER_PORT);
  if ( inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0) {
    fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
    exit(1);
  }

  /* Connect the socket to the server */
  if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    fprintf(stderr, "Error: connect() failed.  Is the server running?\n");
    exit(1);
  }

  /* Start the network thread */
  pthread_create(&network_thread, NULL, network_thread_f, NULL);

  /* Look for and handle keypresses */
  for (;;) {
    libusb_interrupt_transfer(keyboard, endpoint_address,
			      (unsigned char *) &packet, sizeof(packet),
			      &transferred, 0);
    if (transferred == sizeof(packet)) {
      sprintf(keystate, "%02x %02x %02x", packet.modifiers, packet.keycode[0],
	      packet.keycode[1]);
      printf("%s\n", keystate);
      fbputs(keystate, 6, 0);
      if (packet.keycode[0] == 0x29) { /* ESC pressed? */
	break;
      }
    }
  }

  /* Terminate the network thread */
  pthread_cancel(network_thread);

  /* Wait for the network thread to finish */
  pthread_join(network_thread, NULL);

  return 0;
}

void *network_thread_f(void *ignored)
{
  char recvBuf[BUFFER_SIZE];
  int n;
  /* Receive data */
  while ( (n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0 ) {
    recvBuf[n] = '\0';
    printf("%s", recvBuf);
    fbputs(recvBuf, 8, 0);
  }

  return NULL;
}

