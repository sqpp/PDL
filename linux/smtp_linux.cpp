/*
 * SMTP client for PDL Linux with optional OpenSSL TLS.
 * Port 465 + SSL: implicit TLS. Other ports + SSL: STARTTLS after EHLO.
 */
#ifdef __linux__
#include "platform/pdl_linux_types.h"
#include "utils/smtp.h"
#include "Headers/pdl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

char *szSmtpCharSets[MAX_SMTP_CHARSETS] = {
	(char*)"US-ASCII", (char*)"ISO-8859-1", (char*)"ISO-8859-2", (char*)"ISO-8859-3",
	(char*)"ISO-8859-4", (char*)"ISO-8859-5", (char*)"ISO-8859-6", (char*)"ISO-8859-7",
	(char*)"ISO-8859-8", (char*)"ISO-8859-9", (char*)"ISO-8859-10", (char*)"ISO-2022-KR",
	(char*)"KOI8-R", (char*)"EUC-KR", (char*)"Shift_JIS", (char*)"ISO-2022-JP",
	(char*)"EUC-JP", (char*)"GB2312", (char*)"Big5", (char*)"Windows-1250",
	(char*)"Windows-1251", (char*)"Windows-1252", (char*)"Windows-1253",
	(char*)"Windows-1254", (char*)"Windows-1255", (char*)"Windows-1256",
	(char*)"Windows-1257", (char*)"Windows-1258"
};

static char s_host[MAIL_TEXT_LEN];
static char s_helo[MAIL_TEXT_LEN];
static char s_from[MAIL_TEXT_LEN];
static char s_to[MAIL_TEXT_LEN * 5];
static char s_user[MAIL_TEXT_LEN];
static char s_pass[MAIL_TEXT_LEN];
static int s_port = 25;
static int s_options = 0;
static int s_inited = 0;

static SSL_CTX *s_ctx = NULL;
static SSL *s_ssl = NULL;

static int smtp_want_ssl(void)
{
	return Profile.ssl || (s_options & MAIL_OPTION_SSL);
}

static int smtp_implicit_tls(void)
{
	return smtp_want_ssl() && s_port == 465;
}

static void smtp_ssl_cleanup(void)
{
	if (s_ssl) {
		SSL_shutdown(s_ssl);
		SSL_free(s_ssl);
		s_ssl = NULL;
	}
	if (s_ctx) {
		SSL_CTX_free(s_ctx);
		s_ctx = NULL;
	}
}

static int smtp_ssl_init(int fd)
{
	smtp_ssl_cleanup();
	s_ctx = SSL_CTX_new(TLS_client_method());
	if (!s_ctx) return -1;
	SSL_CTX_set_verify(s_ctx, SSL_VERIFY_NONE, NULL);
	s_ssl = SSL_new(s_ctx);
	if (!s_ssl) {
		smtp_ssl_cleanup();
		return -1;
	}
	SSL_set_fd(s_ssl, fd);
	SSL_set_mode(s_ssl, SSL_MODE_AUTO_RETRY);
	if (s_host[0])
		SSL_set_tlsext_host_name(s_ssl, s_host);
	if (SSL_connect(s_ssl) != 1) {
		fprintf(stderr, "SMTP: SSL_connect failed\n");
		ERR_print_errors_fp(stderr);
		smtp_ssl_cleanup();
		return -1;
	}
	return 0;
}

static ssize_t smtp_read(int fd, void *buf, size_t len)
{
	if (s_ssl)
		return SSL_read(s_ssl, buf, (int)len);
	return read(fd, buf, len);
}

static ssize_t smtp_write(int fd, const void *buf, size_t len)
{
	if (s_ssl)
		return SSL_write(s_ssl, buf, (int)len);
	return write(fd, buf, len);
}

static int smtp_readline(int fd, char *buf, size_t buflen)
{
	size_t n = 0;
	while (n + 1 < buflen) {
		char c;
		ssize_t r = smtp_read(fd, &c, 1);
		if (r <= 0) return -1;
		buf[n++] = c;
		if (c == '\n') break;
	}
	buf[n] = '\0';
	return (int)n;
}

static int smtp_expect(int fd, int code)
{
	char line[512];
	for (;;) {
		if (smtp_readline(fd, line, sizeof(line)) < 0) return -1;
		int got = atoi(line);
		size_t len = strlen(line);
		/* Multi-line: "250-..." continues; "250 ..." ends */
		if (len >= 4 && line[3] == '-') continue;
		return (got == code) ? 0 : -1;
	}
}

static int smtp_cmd(int fd, int expect_code, const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
	va_end(ap);
	size_t n = strlen(buf);
	buf[n++] = '\r';
	buf[n++] = '\n';
	if (smtp_write(fd, buf, n) != (ssize_t)n) return -1;
	return smtp_expect(fd, expect_code);
}

static int b64_encode(const char *in, char *out, size_t outlen)
{
	static const char tbl[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t len = strlen(in);
	size_t o = 0;
	for (size_t i = 0; i < len; i += 3) {
		unsigned v = ((unsigned char)in[i]) << 16;
		if (i + 1 < len) v |= ((unsigned char)in[i + 1]) << 8;
		if (i + 2 < len) v |= ((unsigned char)in[i + 2]);
		if (o + 4 >= outlen) return -1;
		out[o++] = tbl[(v >> 18) & 63];
		out[o++] = tbl[(v >> 12) & 63];
		out[o++] = (i + 1 < len) ? tbl[(v >> 6) & 63] : '=';
		out[o++] = (i + 2 < len) ? tbl[v & 63] : '=';
	}
	out[o] = '\0';
	return 0;
}

int MailInit(char *szMailHost, char *szMailHeloDomain, char *szMailFrom, char *szMailTo,
	char *szMailUser, char *szMailPassword, int iMailPort, int nOptions)
{
	strncpy(s_host, szMailHost ? szMailHost : "", sizeof(s_host) - 1);
	strncpy(s_helo, szMailHeloDomain ? szMailHeloDomain : "localhost", sizeof(s_helo) - 1);
	strncpy(s_from, szMailFrom ? szMailFrom : "", sizeof(s_from) - 1);
	strncpy(s_to, szMailTo ? szMailTo : "", sizeof(s_to) - 1);
	strncpy(s_user, szMailUser ? szMailUser : "", sizeof(s_user) - 1);
	strncpy(s_pass, szMailPassword ? szMailPassword : "", sizeof(s_pass) - 1);
	s_port = iMailPort > 0 ? iMailPort : 25;
	s_options = nOptions;
	s_inited = (s_host[0] && s_from[0] && s_to[0]) ? 1 : 0;
	return s_inited ? 0 : -1;
}

int SendMail(HWND hResponse, bool bMatch, bool bMonitor_only, int iSeparateSMTP,
	char *sz1, char *sz2, char *sz3, char *sz4, char *sz5, char *sz6, char *sz7, char *szLabel)
{
	(void)hResponse;
	if (!(s_options & MAIL_OPTION_ENABLE) && !Profile.SMTP) return 0;
	if (!s_inited) {
		MailInit(Profile.szMailHost, Profile.szMailHeloDomain, Profile.szMailFrom,
			Profile.szMailTo, Profile.szMailUser, Profile.szMailPassword,
			Profile.iMailPort, Profile.nMailOptions | MAIL_OPTION_ENABLE);
		if (!s_inited) return -1;
	}

	int mode = s_options & MAIL_OPTION_MODES;
	if (mode == MAIL_OPTION_MODE_FILTER && !bMatch) return 0;
	if (mode == MAIL_OPTION_MODE_MONITOR && !bMonitor_only) return 0;
	if (mode == MAIL_OPTION_MODE_SELECTABLE && !iSeparateSMTP) return 0;

	char portstr[16];
	snprintf(portstr, sizeof(portstr), "%d", s_port);
	struct addrinfo hints, *res = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	if (getaddrinfo(s_host, portstr, &hints, &res) != 0) {
		fprintf(stderr, "SMTP: resolve %s failed\n", s_host);
		return -1;
	}
	int fd = -1;
	for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0) continue;
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0) {
		fprintf(stderr, "SMTP: connect %s:%d failed: %s\n", s_host, s_port, strerror(errno));
		return -1;
	}

	/* Port 465: wrap TLS before greeting. */
	if (smtp_implicit_tls()) {
		if (smtp_ssl_init(fd) != 0) {
			close(fd);
			return -1;
		}
	}

	if (smtp_expect(fd, 220) != 0) {
		smtp_ssl_cleanup();
		close(fd);
		return -1;
	}

	if (smtp_cmd(fd, 250, "EHLO %s", s_helo[0] ? s_helo : "localhost") != 0) {
		if (smtp_cmd(fd, 250, "HELO %s", s_helo[0] ? s_helo : "localhost") != 0) {
			smtp_ssl_cleanup();
			close(fd);
			return -1;
		}
	}

	/* STARTTLS on non-465 when SSL requested. */
	if (smtp_want_ssl() && !smtp_implicit_tls() && !s_ssl) {
		if (smtp_cmd(fd, 220, "STARTTLS") != 0) {
			fprintf(stderr, "SMTP: STARTTLS failed\n");
			close(fd);
			return -1;
		}
		if (smtp_ssl_init(fd) != 0) {
			close(fd);
			return -1;
		}
		/* Re-EHLO after TLS upgrade. */
		if (smtp_cmd(fd, 250, "EHLO %s", s_helo[0] ? s_helo : "localhost") != 0) {
			smtp_ssl_cleanup();
			close(fd);
			return -1;
		}
	}

	if ((s_options & MAIL_OPTION_AUTH) && s_user[0]) {
		if (smtp_cmd(fd, 334, "AUTH LOGIN") != 0) {
			smtp_ssl_cleanup();
			close(fd);
			return -1;
		}
		char b64[256];
		b64_encode(s_user, b64, sizeof(b64));
		if (smtp_cmd(fd, 334, "%s", b64) != 0) {
			smtp_ssl_cleanup();
			close(fd);
			return -1;
		}
		b64_encode(s_pass, b64, sizeof(b64));
		if (smtp_cmd(fd, 235, "%s", b64) != 0) {
			smtp_ssl_cleanup();
			close(fd);
			return -1;
		}
	}

	if (smtp_cmd(fd, 250, "MAIL FROM:<%s>", s_from) != 0) {
		smtp_ssl_cleanup();
		close(fd);
		return -1;
	}
	char to_copy[sizeof(s_to)];
	strncpy(to_copy, s_to, sizeof(to_copy) - 1);
	to_copy[sizeof(to_copy) - 1] = '\0';
	char *tok = strtok(to_copy, ",;");
	int rcpt_ok = 0;
	while (tok) {
		while (*tok == ' ') tok++;
		if (*tok && smtp_cmd(fd, 250, "RCPT TO:<%s>", tok) == 0) rcpt_ok = 1;
		tok = strtok(NULL, ",;");
	}
	if (!rcpt_ok) {
		smtp_ssl_cleanup();
		close(fd);
		return -1;
	}
	if (smtp_cmd(fd, 354, "DATA") != 0) {
		smtp_ssl_cleanup();
		close(fd);
		return -1;
	}

	char subject[256];
	snprintf(subject, sizeof(subject), "PDL: %s %s",
		sz1 ? sz1 : "", szLabel ? szLabel : "");

	char body[4096];
	size_t o = 0;
	o += snprintf(body + o, sizeof(body) - o, "Subject: %s\r\n", subject);
	o += snprintf(body + o, sizeof(body) - o, "From: %s\r\nTo: %s\r\n", s_from, s_to);
	o += snprintf(body + o, sizeof(body) - o, "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n");
	if (sz1) o += snprintf(body + o, sizeof(body) - o, "Address: %s\r\n", sz1);
	if (sz2) o += snprintf(body + o, sizeof(body) - o, "Time: %s\r\n", sz2);
	if (sz3) o += snprintf(body + o, sizeof(body) - o, "Date: %s\r\n", sz3);
	if (sz4) o += snprintf(body + o, sizeof(body) - o, "Mode: %s\r\n", sz4);
	if (sz5) o += snprintf(body + o, sizeof(body) - o, "Type: %s\r\n", sz5);
	if (sz6) o += snprintf(body + o, sizeof(body) - o, "Bitrate: %s\r\n", sz6);
	if (sz7) o += snprintf(body + o, sizeof(body) - o, "Message: %s\r\n", sz7);
	if (szLabel && szLabel[0]) o += snprintf(body + o, sizeof(body) - o, "Label: %s\r\n", szLabel);
	o += snprintf(body + o, sizeof(body) - o, ".\r\n");

	if (smtp_write(fd, body, o) != (ssize_t)o) {
		smtp_ssl_cleanup();
		close(fd);
		return -1;
	}
	if (smtp_expect(fd, 250) != 0) {
		smtp_ssl_cleanup();
		close(fd);
		return -1;
	}
	smtp_cmd(fd, 221, "QUIT");
	smtp_ssl_cleanup();
	close(fd);
	return 0;
}

#endif
