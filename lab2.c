/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Name/UNI: ah4308 and hab2175
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

#define MAX_LINES 20  // Number of lines for received messages
#define MAX_COLS 64   // Number of columns per line

char screen_buffer[MAX_LINES][MAX_COLS]; // Store displayed text
int cursor_pos = 0; // Cursor position in input field
char input_buffer[MAX_COLS]; // User input buffer

char convert_keycode_to_ascii(uint8_t keycode, uint8_t modifiers) {
    static const char keymap[] = {
         0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
        'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '1', '2', '3', '4',
        '5', '6', '7', '8', '9', '0', '\n', 0, 0, '\b', '\t', '-', '=', '[', ']', '\\', 'some', ':',
        '\'', 'some3' , ',', '.', '/', 'some2', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    static const char shifted_keymap[] = {
         0, 0, 0, 0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
        'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@', '#', '$',
        '%', '^', '&', '*', '(', ')', '\n', 0, 0, '\b', '\t', '_', '+', '{', '}', '|', ':',
        '"', '~', '<', '>', '?', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    
    if (keycode == 0x2C) {
        return ' ';  // Explicitly return space
    }


    if (keycode >= sizeof(keymap)) {
        return 0;  // Invalid keycode
    }

    //char ascii = keymap[keycode];

    // Handle shift key (Left Shift: 0x02, Right Shift: 0x20)
    if (modifiers & 0x22) {
        return shifted_keymap[keycode];  // Return shifted character
    } else {
        return keymap[keycode];  // Return normal character
    }
}

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
        fbputchar('-',12, col); // Draw horizontal line
    }
}

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
    int row, col;
    int max_lines = 2;  // Allows input to span 2 lines

    // Clear input area (Rows 22 and 23)
    for (row = 22; row < 22 + max_lines; row++) {
        for (col = 0; col < MAX_COLS; col++) {
            fbputchar(' ', row, col);
        }
    }

    int max_cursor_pos = strlen(input_buffer);  // Full input length

    // Display input buffer, wrapping every MAX_COLS characters
    for (col = 0; col < max_cursor_pos; col++) {
        int row_offset = col / MAX_COLS;  // Move to next row after every MAX_COLS chars
        int col_offset = col % MAX_COLS;  // Column position in current row
        fbputchar(input_buffer[col], 22 + row_offset, col_offset);
    }

    // Display cursor at `cursor_pos`, considering wrapped rows
    int cursor_row = 22 + (cursor_pos / MAX_COLS);
    int cursor_col = cursor_pos % MAX_COLS;
    fbputchar('_', cursor_row, cursor_col);
}

//int shift_pressed = 0; 

#include <time.h>
#include <unistd.h>  // For usleep()

#define MAX_KEYS 128
#define KEY_REPEAT_DELAY 500000  // 500ms before repeating starts (in microseconds)
#define KEY_REPEAT_RATE 100000   // 100ms between repeated characters (in microseconds)

int key_state[MAX_KEYS] = {0};  // Tracks which keys are currently pressed
struct timespec last_keypress_time[MAX_KEYS];  // Track press time per key

void process_keypress(uint8_t keycode, uint8_t modifiers, int is_key_down) {
    clock_gettime(CLOCK_MONOTONIC, &last_keypress_time[keycode]);  // Store current time

    if (!is_key_down) {  // Key release event
        key_state[keycode] = 0;  // Mark key as released
        return;
    }

    if (!key_state[keycode]) {  // First key press
        key_state[keycode] = 1;  // Mark key as held

        // Handle input
        int max_chars = MAX_COLS * 2;  // Allow up to 2 lines

        if (keycode == 0x2A) {  // Backspace
            if (cursor_pos > 0) {
                cursor_pos--;
                memmove(&input_buffer[cursor_pos], &input_buffer[cursor_pos + 1], max_chars - cursor_pos - 1);
                input_buffer[max_chars - 1] = '\0';
            }
        } else if (keycode == 0x28) {  // Enter (Send Message)
            input_buffer[cursor_pos] = '\0';
            write(sockfd, input_buffer, strlen(input_buffer));
            display_message(input_buffer);
            cursor_pos = 0;
            memset(input_buffer, 0, max_chars);
        } else if (keycode == 0x50) {  // Left Arrow
            if (cursor_pos > 0) cursor_pos--;
        } else if (keycode == 0x4F) {  // Right Arrow
            if (cursor_pos < strlen(input_buffer)) cursor_pos++;
        } else if (cursor_pos < max_chars - 1) {  // Regular character input
            char ascii = convert_keycode_to_ascii(keycode, modifiers);
            if (ascii) {
                memmove(&input_buffer[cursor_pos + 1], &input_buffer[cursor_pos], max_chars - cursor_pos - 1);
                input_buffer[cursor_pos] = ascii;
                cursor_pos++;
            }
        }

        display_input();  // Refresh display
    }
}


void *key_repeat_thread(void *arg) {
    struct timespec now;
    
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &now);

        for (int keycode = 0; keycode < MAX_KEYS; keycode++) {
            if (key_state[keycode]) {  // Key is being held
                long elapsed_time = (now.tv_sec - last_keypress_time[keycode].tv_sec) * 1000000L +
                                    (now.tv_nsec - last_keypress_time[keycode].tv_nsec) / 1000L;

                if (elapsed_time >= KEY_REPEAT_DELAY) {  // Start repeating after delay
                    process_keypress(keycode, 0, 1);  // Repeat key press
                    clock_gettime(CLOCK_MONOTONIC, &last_keypress_time[keycode]);  // Reset timer
                    usleep(KEY_REPEAT_RATE);  // Enforce repeat rate
                }
            }
        }
        usleep(5000);  // Sleep for 5ms to prevent excessive CPU usage
    }

    return NULL;
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
  pthread_t repeat_thread;
  pthread_create(&repeat_thread, NULL, key_repeat_thread, NULL);


  clear_screen();
  draw_layout();

  //fbputs("Chat Client - CSEE 4840", 0, 10);

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
                              (unsigned char *)&packet, sizeof(packet),
                              &transferred, 0);
    if (transferred == sizeof(packet)) {
        for (int i = 0; i < 6; i++) {  // USB keyboard can report up to 6 simultaneous keys
            uint8_t keycode = packet.keycode[i];

            if (keycode != 0) {  // A key is pressed
                process_keypress(keycode, packet.modifiers, 1);  // Key press (keydown)
            }
        }

        // Handle key releases (keys that are no longer in the keycode array)
        for (int key = 4; key < MAX_KEYS; key++) {  // Keycodes start from index 4
            int key_found = 0;
            for (int j = 0; j < 6; j++) {
                if (packet.keycode[j] == key) {
                    key_found = 1;
                    break;
                }
            }
            if (!key_found && key_state[key]) {
                process_keypress(key, 0, 0);  // Key release (keyup)
            }
        }

        // Exit condition (ESC key)
        if (packet.keycode[0] == 0x29) {  // ESC pressed? Exit.
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
    char my_identifier[] = "ME:";  // Use a custom identifier for your messages

    while ((n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0) {
        recvBuf[n] = '\0';  // Null-terminate the message

        // Check if this message was sent by me (assuming "ME:" prefix for my messages)
        if (strncmp(recvBuf, my_identifier, strlen(my_identifier)) == 0) {
            char *msg_start = recvBuf + strlen(my_identifier);  // Extract message
            printf("%s\n", msg_start);  // Print only my messages
            display_message(msg_start);  // Display only my messages on the screen
        }
    }

    return NULL;
}

