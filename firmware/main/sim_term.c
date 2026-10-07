/*
 This is a thoroughly hacked version of the SIMH sim_term.c file, keeping only what's needed to
 start up emulation. All hackwork is (c) 2020 Sprite_tm.

 Original copyright follows:
*/

/*
   Copyright (c) 1993-2010, Robert M Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

*/

#ifdef ESP_PLATFORM
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "driver/gpio.h"
#include <stdbool.h>
#endif

#include "sim_defs.h"
#include "scp.h"
#include "sim_term.h"

#include <stdio.h>
#include <sys/select.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "bthid.h"
#include "usb_keyboard.h"
#include "sim_hang_probe.h"
#include "esp_heap_trace.h"
#include "esp_log.h"
#include "ie15lcd.h"
#include "esp_timer.h"
#if CONFIG_ESPPDP_PERF_TRACE
#include "sim_perf.h"
#endif
char last_char;
int64_t autoboot_next_evt;
static bool uart0_input_ready;
static bool usb_serial_jtag_input_ready;
static volatile bool host_escape_enabled;
static volatile bool host_escape_requested;
static const char *SIM_TERM_TAG = "sim_term";
extern int stop_cpu;

static t_bool sim_host_input_is_escape(int c)
{
	if (host_escape_enabled && ((uint8_t)c == (uint8_t)sim_int_char)) {
		host_escape_requested = TRUE;
		stop_cpu = TRUE;
		return TRUE;
	}
	return FALSE;
}

#endif


uint32_t sim_int_char = 5;
t_bool sim_signaled_int_char=FALSE;
uint32 sim_last_poll_kbd_time=0;
//int32 sim_tt_pchar = 0x00002780;
int32 sim_tt_pchar = 0xffffffff; //pass on all chars

char *read_line (char *cptr, int32 size, FILE *stream){
	printf("unimp: read_line\n");
	return "";
}

t_stat sim_set_pchar (int32 flag, CONST char *cptr) {
	(void)flag;
	(void)cptr;
	return SCPE_OK;
}

void sim_host_escape_enable(t_bool enable)
{
#ifdef ESP_PLATFORM
	host_escape_enabled = enable;
#else
	(void)enable;
#endif
}

t_bool sim_host_escape_requested(void)
{
#ifdef ESP_PLATFORM
	return host_escape_requested;
#else
	return FALSE;
#endif
}

void sim_host_escape_clear(void)
{
#ifdef ESP_PLATFORM
	host_escape_requested = FALSE;
#endif
}


t_stat sim_poll_kbd (void) {
#ifndef ESP_PLATFORM
	int bytesWaiting;
	ioctl(0, FIONREAD, &bytesWaiting);
	if (bytesWaiting!=0) {
		return getchar() | SCPE_KFLAG;
	}
#else
	/* The native USB Serial/JTAG console is fd 0 when selected as the
	 * primary ESP-IDF console.  Also poll UART0 so the separate USB-UART
	 * connector can be used at the same time. */
	if (uart0_input_ready) {
		size_t buffered = 0;
		if (uart_get_buffered_data_len(UART_NUM_0, &buffered) == ESP_OK && buffered != 0) {
			uint8_t byte;
			if (uart_read_bytes(UART_NUM_0, &byte, 1, 0) == 1) {
				sim_hang_probe_input();
				ESP_LOGD(SIM_TERM_TAG, "UART0 input 0x%02x", byte);
				if (sim_host_input_is_escape(byte)) return SCPE_OK;
				return byte | SCPE_KFLAG;
			}
		}
	}
	/* IDF's secondary USB console intentionally mirrors output only.  Read
	 * its driver queue directly so the native USB connector is an input too. */
	if (usb_serial_jtag_input_ready) {
		uint8_t byte;
		if (usb_serial_jtag_read_bytes(&byte, 1, 0) == 1) {
			sim_hang_probe_input();
			ESP_LOGD(SIM_TERM_TAG, "USB Serial/JTAG input 0x%02x", byte);
			if (sim_host_input_is_escape(byte)) return SCPE_OK;
			return byte | SCPE_KFLAG;
		}
	}
	int c = EOF;
	bool stdin_input_covered = false;
#if CONFIG_ESP_CONSOLE_UART && CONFIG_ESP_CONSOLE_UART_NUM == 0
	stdin_input_covered = uart0_input_ready;
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
	stdin_input_covered = usb_serial_jtag_input_ready;
#endif
	/* stdin is another view of the primary console when its direct driver is
	 * available.  ESP-IDF's VFS select() waits a FreeRTOS tick even with a
	 * zero timeout, so do not call it on every empty terminal poll.  Keep the
	 * fallback for console configurations without a direct input queue. */
	if (!stdin_input_covered) {
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(STDIN_FILENO, &readfds);
		struct timeval timeout = { .tv_sec = 0, .tv_usec = 0 };
		int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
		if (ready > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
			unsigned char byte;
			if (read(STDIN_FILENO, &byte, 1) == 1) c = byte;
		}
	}
//If tracing is enabled, '|' dumps the trace.
#if CONFIG_HEAP_TRACING_STANDALONE
	if (c=='|') {
		ESP_ERROR_CHECK( heap_trace_stop() );
		heap_trace_dump();
	}
#endif
	if (c!=EOF) {
		sim_hang_probe_input();
		if (sim_host_input_is_escape(c)) return SCPE_OK;
		return c|SCPE_KFLAG;
	}
	c=usb_keyboard_getchar();
	if (c!=-1) {
		sim_hang_probe_input();
		if (sim_host_input_is_escape(c)) return SCPE_OK;
		return c|SCPE_KFLAG;
	}
	c=bthid_getchar();
	if (c!=-1) {
		sim_hang_probe_input();
		if (sim_host_input_is_escape(c)) return SCPE_OK;
		return c|SCPE_KFLAG;
	}
	//Try to automagically boot into 2.11BSD if no keyboard is connected.
#if 0
	if (!bthid_connected() && esp_timer_get_time()>autoboot_next_evt) {
		autoboot_next_evt=esp_timer_get_time()+(1000UL*1000*5);
		if (last_char==':') return '\n'|SCPE_KFLAG;
		if (last_char=='#') return 0x4|SCPE_KFLAG;
	}
#endif

#endif
	return SCPE_OK;
}

/* Input character processing */

int32 sim_tt_inpcvt (int32 c, uint32 mode) {
	uint32 md = mode & TTUF_M_MODE;
	
	if (md != TTUF_MODE_8B) {
		uint32 par_mode = (mode >> TTUF_W_MODE) & TTUF_M_PAR;
		static int32 nibble_even_parity = 0x699600;		/* bit array indicating the even parity for each index (offset by 8) */

		c = c & 0177;
		if (md == TTUF_MODE_UC) {
			if (islower (c)) c = toupper (c);
			if (mode & TTUF_KSR) c = c | 0200;							/* Force MARK parity */
		}
		switch (par_mode) {
			case TTUF_PAR_EVEN:
				c |= (((nibble_even_parity >> ((c & 0xF) + 1)) ^ (nibble_even_parity >> (((c >> 4) & 0xF) + 1))) & 0x80);
				break;
			case TTUF_PAR_ODD:
				c |= ((~((nibble_even_parity >> ((c & 0xF) + 1)) ^ (nibble_even_parity >> (((c >> 4) & 0xF) + 1)))) & 0x80);
				break;
			case TTUF_PAR_MARK:
				c = c | 0x80;
				break;
		}
	} else {
		c = c & 0377;
	}
	return c;
}

/* Output character processing */

int32 sim_tt_outcvt (int32 c, uint32 mode) {
	uint32 md = mode & TTUF_M_MODE;
	if (md != TTUF_MODE_8B) {
		c = c & 0177;
		if (md == TTUF_MODE_UC) {
			if (islower (c)) c = toupper (c);
			if ((mode & TTUF_KSR) && (c >= 0140)) return -1;
		}
		if (((md == TTUF_MODE_UC) || (md == TTUF_MODE_7P)) &&
				((c == 0177) ||
				 ((c < 040) && !((sim_tt_pchar >> c) & 1)))) {
			return -1;
		}
	} else {
		c = c & 0377;
	}
	return c;
}

t_stat tmxr_set_console_units (UNIT *rxuptr, UNIT *txuptr) {
	//tmxr_set_line_unit (&sim_con_tmxr, 0, rxuptr);
	//tmxr_set_line_output_unit (&sim_con_tmxr, 0, txuptr);
	return SCPE_OK;
}

#ifdef ESP_PLATFORM
#include "ie15lcd.h"
#endif

t_stat sim_putchar_s (int32 c) {
#ifdef ESP_PLATFORM
#if CONFIG_ESPPDP_BOOT_TRACE
	static uint32 esp_output_trace_count;
	if (esp_output_trace_count < 256) {
		printf("[TTO output 0x%02x]\n", c & 0377);
		esp_output_trace_count++;
	}
#endif
	ie15_sendchar(c);
	if (c!=' ') last_char=c;
#endif
#ifdef ESP_PLATFORM
#if CONFIG_ESPPDP_PERF_TRACE
	int64_t output_start_us = esp_timer_get_time();
#endif
	putchar(c);
	sim_hang_probe_output();
	/* The guest console is character-oriented; flush echoes and prompts
	 * immediately instead of waiting for a line ending. */
	fflush(stdout);
#if CONFIG_ESPPDP_PERF_TRACE
	int64_t output_elapsed_us = esp_timer_get_time() - output_start_us;
	if (output_elapsed_us > 0)
		sim_perf_note_tto((uint32_t)output_elapsed_us);
#endif
#else
	putchar(c);
#endif
	return SCPE_OK;
}

t_stat sim_tt_show_modepar (FILE *st, UNIT *uptr, int32 val, CONST void *desc) {
	return SCPE_OK;
}

t_stat sim_tt_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
	uint32 par_mode = (TT_GET_MODE (uptr->flags) >> TTUF_W_MODE) & TTUF_M_PAR;
	uptr->flags = uptr->flags & ~((TTUF_M_MODE << TTUF_V_MODE) | (TTUF_M_PAR << TTUF_V_PAR) | TTUF_KSR);
	uptr->flags |= val;
	if (val != TT_MODE_8B) uptr->flags |= (par_mode << TTUF_V_PAR);
	return SCPE_OK;
}

t_stat sim_tt_set_parity (UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
	uptr->flags = uptr->flags & ~(TTUF_M_MODE | TTUF_M_PAR);
	uptr->flags |= TT_MODE_7B | val;
	return SCPE_OK;
}

//Called in main to init tt
t_stat sim_ttinit (void) {
#ifndef ESP_PLATFORM
	struct termios term;
	tcgetattr(0, &term);
	term.c_lflag &= ~ICANON;
	tcsetattr(0, TCSANOW, &term);
	setbuf(stdin, NULL);
#else
	autoboot_next_evt=0;
	static bool initialized;
	if (initialized) return SCPE_OK;
	/* Characters should be delivered as soon as they arrive, not after a
	 * terminal line ending.  sim_poll_kbd() performs the nonblocking poll. */
	setvbuf(stdin, NULL, _IONBF, 0);
	int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	if (flags >= 0) (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
	/* The primary UART console is normally already installed by ESP-IDF.  On
	 * that path uart_driver_install returns ESP_ERR_INVALID_STATE; that still
	 * means its RX queue is available for the USB-UART connector. */
	const uart_config_t uart_config = {
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};
	esp_err_t uart_install = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
	if (uart_install == ESP_OK || uart_install == ESP_ERR_INVALID_STATE) {
		if (uart_install == ESP_OK) {
			(void)uart_param_config(UART_NUM_0, &uart_config);
			(void)uart_set_pin(UART_NUM_0, 43, 44,
			                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
		}
		uart0_input_ready = true;
	}
	/* A secondary console is deliberately output-only in ESP-IDF and may not
	 * install the interrupt-driven USB driver at all.  Install a small RX/TX
	 * queue here so the native connector can also feed the simulator. */
	if (!usb_serial_jtag_is_driver_installed()) {
		usb_serial_jtag_driver_config_t usb_config =
			USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
		esp_err_t usb_install = usb_serial_jtag_driver_install(&usb_config);
		if (usb_install != ESP_OK && usb_install != ESP_ERR_INVALID_STATE)
			printf("usb serial input unavailable: %s\n", esp_err_to_name(usb_install));
	}
	usb_serial_jtag_input_ready = usb_serial_jtag_is_driver_installed();
	if (usb_serial_jtag_input_ready)
		usb_serial_jtag_vfs_use_driver();
	initialized = true;
#endif
	return SCPE_OK;
}
