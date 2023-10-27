/*
 * AstMultiDialer: CLI dialer for Asterisk
 *
 * Copyright (C) 2023, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 		http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*! \file
 *
 * \brief AstMultiDialer: 9-line CLI dialer for Asterisk
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 */

/* == Configurable settings == */

/* Will dial PJSIP/<PLAR CODE>@<PEER PREFIX><line #> */
/* Prefix of device name on remote server under testing */
#define PEER_PREFIX "autotest"
/* PLAR code on the remote server under testing */
#define PLAR_CODE "01"
/* Connect to this context and extension (priority 1) locally
 * (should be a dialplan context that answers and waits for a long time)
 *
 * e.g.
 * [idle]
 * exten => _X!,1,Answer()
 *     same => n,Wait(${EXTEN})
 *     same => n,Hangup()
 */
#define PLAR_DIALPLAN_CONTEXT "idle"
#define PLAR_DIALPLAN_EXTEN "9999"

/*
 * This is a simple CLI based dialer that uses AMI (Asterisk Manager Interface)
 * to manipulate virtual "lines" remotely. A typical setup will look like this:
 *
 * - Server under testing, with lines provisioned using PJSIP (or SIP)
 * - Server for testing, with line registrations using PJSIP
 *
 * This isn't really so much a dialer per se as a line manipulator, useful for testing.
 * There is no audio output, for instance, so this is not a generic softphone program.
 * You can use brief commands that are somewhat similar to the Hayes command set.
 *
 * The main application of this is performing (potentially complex) testing that requires access to multiple
 * telephone lines, without the tester having to physically manipulate multiple telephones.
 * It may also be used as part of an automated testing strategy.
 *
 * Currently, this program is very basic. The only configurable settings are provided above,
 * and this is only set up to work with PJSIP locally (though the server could use PJSIP or SIP).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <poll.h>
#include <termios.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include <cami/cami.h>
#include <cami/cami_actions.h>

static struct termios origterm, ttyterm;
static char inputbuf[64] = "";

struct line {
	char devicename[64];
	char dialstr[84];
	char dialexten[64];
	char channel[128];
	unsigned int offhook:1;
};

#define MAX_LINES 9

static struct line lines[MAX_LINES + 1]; /* Leave index 0 unused for simplicitly, so we can use it 1-indexed. */

/*! \brief Callback function executing asynchronously when new events are available */
static void ami_callback(struct ami_event *event)
{
	ami_event_free(event); /* Discard all events. We don't need them. */
}

static void simple_disconnect_callback(void)
{
	fprintf(stderr, "\nAMI was forcibly disconnected...\n");
	tcsetattr(STDIN_FILENO, TCSANOW, &origterm); /* Restore the original term settings */
	exit(EXIT_FAILURE);
}

static void hangup_all(void)
{
	int i;

	for (i = 1; i <= MAX_LINES; i++) {
		if (lines[i].offhook) {
			struct ami_response *resp = ami_action("Hangup", "Channel:%s\r\nCause:%d", lines[i].channel, 16);
			if (resp && resp->success) {
				fprintf(stderr, "Hung up line %d\n", i);
				lines[i].offhook = 0;
			}
			if (resp) {
				ami_resp_free(resp);
			}
		}
	}
}

static void restore_term(int num)
{
	/* Be nice and restore the terminal to how it was before, before we exit. */
	tcsetattr(STDIN_FILENO, TCSANOW, &origterm); /* Restore the original term settings */
	fprintf(stderr, "\n");

	/* Hang up any lines still active */
	hangup_all();

	ami_disconnect();
	fprintf(stderr, "\nAstMultiDialer exiting...\n");
	exit(EXIT_FAILURE);
}

static int find_channel(int n)
{
	int i, found = 0;
	struct ami_response *resp;
	const char *prefix = lines[n].devicename;
	int prefixlen;

	/* Originate action doesn't give us the new channel name, so try to find it,
	 * assuming there's only one channel with the prefix of the device name */

	resp = ami_action_show_channels();
	if (!resp) {
		fprintf(stderr, "Failed to show channels\n");
		return -1;
	}

	prefixlen = strlen(prefix);
	for (i = 1; i < resp->size - 1; i++) {
		const char *channel = ami_keyvalue(resp->events[i], "Channel");
		if (!strncmp(channel, prefix, prefixlen)) {
			strncpy(lines[n].channel, channel, sizeof(lines[n].channel) - 1);
			found = 1;
			break;
		}
	}
	if (!found) {
		fprintf(stderr, "Failed to find channel for %s\n", lines[n].devicename);
	}
	return found ? 0 : -1;
}

#define REQUIRE_RESP(resp) if (!resp) { fprintf(stderr, "No response\n"); return 0; }
#define REQUIRE_ACTIVE() if (!lines[n].offhook) { fprintf(stderr, "Can't do this action on on-hook line\n"); return 0; }

#define ltrim(s) \
	while (isspace(*s)) { \
		s++; \
	}

static int run_command(char *command)
{
	struct ami_response *resp;
	char *tmp;
	int n = 0;

	tmp = strchr(command, ';'); /* Ignore comments. Use ; instead of # since # is a DTMF digit. */
	if (tmp) {
		*tmp = '\0';
	}

	/* Get line number, if applicable. */
	if (isdigit(*command)) {
		n = atoi(command);
		if (!n) {
			fprintf(stderr, "Line number must be between 1 and 9\n");
			return 0;
		}
		command++;
		ltrim(command);
	}

	/* Parse command */
	if (n) { /* Line command */
		tmp = command;
		snprintf(lines[n].devicename, sizeof(lines[n].devicename), "PJSIP/%s%d", PEER_PREFIX, n);
		snprintf(lines[n].dialstr, sizeof(lines[n].dialstr), "PJSIP/%s@%s%d", PLAR_CODE, PEER_PREFIX, n);
		snprintf(lines[n].dialexten, sizeof(lines[n].dialexten), PLAR_DIALPLAN_CONTEXT);
		switch (tolower(*command++)) {
			case 'a': /* answer (off hook) */
				/*! \todo add */
				fprintf(stderr, "XXX Not implemented yet\n");
				break;
			case 'o': /* originate (off hook) */
				resp = ami_action("Originate", "Channel:%s\r\nContext:%s\r\nExten:%s\r\nPriority:%s", lines[n].dialstr, lines[n].dialexten, PLAR_DIALPLAN_EXTEN, "1", NULL);
				REQUIRE_RESP(resp);
				if (resp && resp->success) {
					lines[n].offhook = 1;
					if (!find_channel(n)) {
						fprintf(stderr, "OK\n");
					}
				} else {
					fprintf(stderr, "Failed to go off hook on line %d\n", n);
				}
				ami_resp_free(resp);
				break;
			case 'h': /* on hook */
				REQUIRE_ACTIVE();
				resp = ami_action("Hangup", "Channel:%s\r\nCause:%d", lines[n].channel, 16);
				REQUIRE_RESP(resp);
				if (resp && resp->success) {
					lines[n].offhook = 0;
					fprintf(stderr, "OK\n");
				} else {
					fprintf(stderr, "Failed to go on hook on line %d\n", n);
				}
				ami_resp_free(resp);
				break;
			case 'f': /* flash */
				REQUIRE_ACTIVE();
				resp = ami_action("SendFlash", "Channel:%s", lines[n].channel);
				REQUIRE_RESP(resp);
				if (resp && resp->success) {
					fprintf(stderr, "OK\n");
				} else {
					fprintf(stderr, "Failed to send flash on line %d\n", n);
				}
				ami_resp_free(resp);
				break;
			case 'd': /* dial */
				REQUIRE_ACTIVE();
				tmp = command++;
				if (*tmp== 't') {
					/* The PlayDTMF action is kind of silly. You have to do it once digit at a time.
					 * However, we can send all the digits at once without waiting, and the channel will queue them up. */
					while (*command) {
						resp = ami_action("PlayDTMF", "Channel:%s\r\nDigit:%c", lines[n].channel, *command);
						command++;
					}
				} else if (*tmp == 'p') {
					fprintf(stderr, "Dial pulse not yet supported\n");
				} else {
					fprintf(stderr, "Invalid dial type %c\n", *tmp);
				}
				break;
			default:
				fprintf(stderr, "Unknown line command '%c'\n", *tmp);
		}
	} else { /* Global command */
		int sleeptime;
		if (*command == 's') {
			command++;
			ltrim(command);
			sleeptime = atoi(command);
			sleep(sleeptime);
		} else if (!strncasecmp(command, "ms", 2)) {
			command += 2;
			ltrim(command);
			sleeptime = atoi(command);
			usleep(sleeptime * 1000);
		} else if (!strcasecmp(command, "q")) {
			return -1;
		} else if (!strcasecmp(command, "k")) {
			hangup_all();
		} else {
			fprintf(stderr, "Unknown global command '%s'\n", command);
		}
	}

	return 0;
}

static void show_command_help(void)
{
	printf(
		"\r"
		"Usage: [<line #>] command [arguments]\n"
		"-- Line Actions (lines 1-9) --\n"
		"o     - Go off hook\n"
		"dt    - Dial digits using DTMF\n"
		"dp    - Dial digits using pulse dialing (not supported currently)\n"
		"a     - Answer incoming call\n"
		"f     - Hook flash\n"
		"h     - Go on hook\n"
		"p     - Play audio file\n"
		"-- General Actions --\n"
		"k     - hang up all active lines\n"
		"s     - sleep for N seconds\n"
		"ms    - sleep for N milliseconds\n"
		"q     - Quit\n"
		"-- Examples --\n"
		"1o             ; originate on line 1\n"
		"2 o            ; originate on line 2 (whitespace is ignored)\n"
		"1dt47          ; dial DTMF 47 on line 1\n"
		"3a             ; answer incoming call on line 3\n"
		"1p custom/beep ; Play audio file on line\n"
		"ms750          ; sleep for 750ms\n"
	);
}

static int multidialer(void)
{
	struct pollfd pfd;
	char *pos;
	int left, reset, res;

	tcgetattr(STDIN_FILENO, &origterm);
	ttyterm = origterm;

	/* Set up the terminal */
	ttyterm.c_lflag &= ~ICANON; /* Disable canonical mode to disable input buffering. Needed so poll works correctly on STDIN_FILENO */
	signal(SIGINT, restore_term); /* Setup a signal handler for SIGINT, so we can restore the terminal. */
	tcsetattr(STDIN_FILENO, TCSANOW, &ttyterm); /* Apply changes */

	/* Wait for input. */
	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;

	reset = 1;

	for (;;) {
		if (reset) {
			pos = inputbuf;
			left = sizeof(inputbuf) - 1;
			reset = 0;
			fprintf(stderr, ">");
		}
		/* This thread will block forever on input. */
		res = poll(&pfd, 1, -1);
		if (res < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		} else if (pfd.revents) {
			/* Got some input. */
			char c;
			int num_read = read(STDIN_FILENO, &c, 1); /* Only read one char. */
			if (num_read < 1) {
				break; /* Disconnect */
			}
			/*! \todo Might be nice to add command history (using histedit) */
			if (c == '\n') {
				/* Got a full command, execute */
				*pos = '\0';
				if (run_command(inputbuf)) {
					break;
				}
				inputbuf[0] = '\0';
				reset = 1;
			} else if (c == '?') {
				show_command_help();
				printf("\n");
				reset = 1;
			} else {
				*pos++ = c;
				if (--left <= 1) {
					fprintf(stderr, "Command too long\n");
					reset = 1;
				}
			}
		}
	}

	ami_disconnect();
	tcsetattr(STDIN_FILENO, TCSANOW, &origterm); /* Restore the original term settings */
	return 0;
}

static void show_help(void)
{
	printf("AstMultiDialer for Asterisk\n");
	printf(" -d           Enable AMI debug\n");
	printf(" -h           Show this help\n");
	printf(" -l           Asterisk AMI hostname. Default is localhost (127.0.0.1)\n");
	printf(" -p           Asterisk AMI password. By default, this will be autodetected for local connections if possible.\n");
	printf(" -u           Asterisk AMI username.\n");
	printf("\n");
	printf("You can use AstMultiDialer interactively, or you can feed it commands using a script file (just redirect the file to STDIN).\n");
	printf("(C) 2023 Naveen Albert\n");
}

#define TERM_CLEAR "\e[1;1H\e[2J"

int main(int argc,char *argv[])
{
	char c;
	static const char *getopt_settings = "?dhl:p:u:";
	char ami_host[92] = "127.0.0.1"; /* Default to localhost */
	char ami_username[64] = "";
	char ami_password[64] = "";
	static int ami_debug_level = 0;

	while ((c = getopt(argc, argv, getopt_settings)) != -1) {
		switch (c) {
		case 'd':
			ami_debug_level++;
			break;
		case '?':
		case 'h':
			show_help();
			return 0;
		case 'l':
			strncpy(ami_host, optarg, sizeof(ami_host));
			break;
		case 'p':
			strncpy(ami_password, optarg, sizeof(ami_password));
			break;
		case 'u':
			strncpy(ami_username, optarg, sizeof(ami_username));
			break;
		default:
			fprintf(stderr, "Invalid option: %c\n", c);
			return -1;
		}
	}

	if (ami_username[0] && !ami_password[0] && !strcmp(ami_host, "127.0.0.1")) {
		/* If we're running as a privileged user with access to manager.conf, grab the password ourselves, which is more
		 * secure than getting as a command line arg from the user (and kind of convenient)
		 * Not that running as a user with access to the Asterisk config is great either, but, hey...
		 */
		if (ami_auto_detect_ami_pass(ami_username, ami_password, sizeof(ami_password))) {
			fprintf(stderr, "No password specified, and failed to autodetect from /etc/asterisk/manager.conf\n");
			return -1;
		}
	}

	if (!ami_username[0]) {
		fprintf(stderr, "No username provided (use -u flag)\n");
		return -1;
	}

	if (ami_connect(ami_host, 0, ami_callback, simple_disconnect_callback)) {
		fprintf(stderr, "Failed to connect to AMI (host: %s, user: %s)\n", ami_host, ami_username);
		return -1;
	}
	if (ami_action_login(ami_username, ami_password)) {
		fprintf(stderr, "Failed to log in with username %s\n", ami_username);
		return -1;
	}

	/* Clear the screen. */
	printf(TERM_CLEAR);
	printf("*** AstMultiDialer ***\n");
	printf("Press ? for help\n");
	fflush(stdout);

	if (ami_debug_level) {
		ami_set_debug(STDERR_FILENO);
		ami_set_debug_level(ami_debug_level);
		fprintf(stderr, "AMI debug level is %d\n", ami_debug_level);
	}

	if (multidialer()) {
		return -1;
	}
	return 0;
}
