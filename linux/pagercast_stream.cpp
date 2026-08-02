/*
 * PagerCast v3 radio stream client: HTTPS PCM (s16le @ 48 kHz) → u8 → decoder.
 *
 * Auth: Bearer pcat_… (or ?api_token=). Subscriber id comes from /api/v2/radio/config —
 * the UI does not ask for it.
 */
#ifdef __linux__
#include "platform/pdl_linux_types.h"
#include "Headers/pdl.h"
#include "Headers/sound_in.h"
#include "linux/pagercast_stream.h"
#include "linux/hw_decode.h"
#include "linux/gpu_spectrum.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>

#define PC_PCM_RATE 48000
#define PC_MAX_NODES 32
#define PC_MAX_URL 768

typedef struct {
	char display_name[128];
	char api_base_url[256];
	long carrier_id;
} PcNode;

static PcNode s_nodes[PC_MAX_NODES];
static int s_node_count;
static pthread_t s_thread;
static volatile int s_want_stream; /* user wants PagerCast mode (survives drops) */
static volatile int s_running;     /* curl read loop should continue */
static volatile int s_connected;
static volatile int s_state; /* PdlPagerCastState */
static int s_thread_joined = 1;
static char s_status[256] = "Disconnected";
static pthread_mutex_t s_status_mu = PTHREAD_MUTEX_INITIALIZER;
static int s_saved_sample_rate;
static int s_have_saved_rate;
static volatile double s_stream_level;

extern void pdl_linux_pause_local_capture(void);
extern void pdl_linux_resume_local_capture(void);

static void set_status_state(const char *msg, PdlPagerCastState st)
{
	pthread_mutex_lock(&s_status_mu);
	strncpy(s_status, msg ? msg : "", sizeof(s_status) - 1);
	s_status[sizeof(s_status) - 1] = '\0';
	s_state = (int)st;
	pthread_mutex_unlock(&s_status_mu);
}

static void set_status(const char *msg)
{
	/* Infer state from message when caller doesn't specify. */
	PdlPagerCastState st = PDL_PC_DISCONNECTED;
	if (msg) {
		if (strstr(msg, "Streaming"))
			st = PDL_PC_STREAMING;
		else if (strstr(msg, "Connecting") || strstr(msg, "Resolving") ||
			strstr(msg, "Disconnecting") || strstr(msg, "Refreshing"))
			st = PDL_PC_CONNECTING;
		else if (strstr(msg, "fail") || strstr(msg, "Fail") || strstr(msg, "rejected") ||
			strstr(msg, "Auth") || strstr(msg, "HTTP") || strstr(msg, "required") ||
			strstr(msg, "Missing") || strstr(msg, "No stream") ||
			strstr(msg, "Couldn't") || strstr(msg, "Could not") || strstr(msg, "Timeout") ||
			strstr(msg, "timeout") || strstr(msg, "error") || strstr(msg, "Error") ||
			strstr(msg, "Thread failed"))
			st = PDL_PC_ERROR;
		else if (strstr(msg, "Disconnected") || strstr(msg, "Stream ended") || !msg[0])
			st = PDL_PC_DISCONNECTED;
	}
	set_status_state(msg, st);
}

const char *pdl_pagercast_status(void)
{
	static char copy[256];
	pthread_mutex_lock(&s_status_mu);
	strncpy(copy, s_status, sizeof(copy) - 1);
	copy[sizeof(copy) - 1] = '\0';
	pthread_mutex_unlock(&s_status_mu);
	return copy;
}

PdlPagerCastState pdl_pagercast_state(void)
{
	return (PdlPagerCastState)s_state;
}

const char *pdl_pagercast_state_label(void)
{
	switch (pdl_pagercast_state()) {
	case PDL_PC_STREAMING:  return "Streaming";
	case PDL_PC_CONNECTING: return "Connecting";
	case PDL_PC_ERROR:      return "Error";
	default:                return "Offline";
	}
}

const char *pdl_pagercast_input_label(void)
{
	static char buf[192];
	PdlPagerCastState st = pdl_pagercast_state();
	if (st != PDL_PC_STREAMING && st != PDL_PC_CONNECTING)
		return NULL;

	char host[96] = "";
	const char *base = Profile.pagercast_api_base;
	if (base && base[0]) {
		const char *p = strstr(base, "://");
		p = p ? p + 3 : base;
		size_t n = 0;
		while (p[n] && p[n] != '/' && p[n] != ':' && n + 1 < sizeof(host)) {
			host[n] = p[n];
			n++;
		}
		host[n] = '\0';
	}
	if (!host[0])
		strncpy(host, "pagercast", sizeof(host) - 1);

	const char *freq = Profile.pagercast_frequency[0] ? Profile.pagercast_frequency : "?";
	if (st == PDL_PC_CONNECTING)
		snprintf(buf, sizeof(buf), "PagerCast %s @ %s (connecting)", freq, host);
	else
		snprintf(buf, sizeof(buf), "PagerCast %s @ %s", freq, host);
	return buf;
}

int pdl_pagercast_is_connected(void)
{
	return s_connected;
}

double pdl_pagercast_get_input_level(void)
{
	return s_connected ? s_stream_level : 0.0;
}

static void normalize_base(char *base, size_t n)
{
	size_t len = strlen(base);
	while (len > 0 && base[len - 1] == '/')
		base[--len] = '\0';
	(void)n;
}

static void trim_inplace(char *s)
{
	if (!s) return;
	char *p = s;
	while (*p && isspace((unsigned char)*p)) p++;
	if (p != s) memmove(s, p, strlen(p) + 1);
	size_t n = strlen(s);
	while (n > 0 && isspace((unsigned char)s[n - 1]))
		s[--n] = '\0';
}

/* Accept "154.600", "154.6", or compact "154600" → "154.600". */
static void normalize_frequency_mhz(char *freq, size_t buflen)
{
	trim_inplace(freq);
	if (!freq[0] || buflen < 8) return;

	/* Already has a decimal point. */
	if (strchr(freq, '.')) {
		/* Normalize frac to 3 digits when possible. */
		char tmp[64];
		int ip = 0, fp = 0;
		const char *dot = strchr(freq, '.');
		ip = atoi(freq);
		int digits = (int)strlen(dot + 1);
		fp = atoi(dot + 1);
		if (digits == 1) fp *= 100;
		else if (digits == 2) fp *= 10;
		else if (digits > 3) {
			fp = 0;
			for (int i = 0; i < 3 && dot[1 + i]; i++)
				fp = fp * 10 + (dot[1 + i] - '0');
		}
		snprintf(tmp, sizeof(tmp), "%d.%03d", ip, fp);
		strncpy(freq, tmp, buflen - 1);
		freq[buflen - 1] = '\0';
		return;
	}

	/* Digits only: compact host label (154600) or integer MHz. */
	int all_digit = 1;
	for (const char *p = freq; *p; p++) {
		if (!isdigit((unsigned char)*p)) { all_digit = 0; break; }
	}
	if (!all_digit) return;

	size_t len = strlen(freq);
	if (len > 3) {
		/* 154600 → 154.600 */
		char tmp[64];
		snprintf(tmp, sizeof(tmp), "%.*s.%s", (int)(len - 3), freq, freq + len - 3);
		strncpy(freq, tmp, buflen - 1);
		freq[buflen - 1] = '\0';
	} else {
		char tmp[64];
		snprintf(tmp, sizeof(tmp), "%s.000", freq);
		strncpy(freq, tmp, buflen - 1);
		freq[buflen - 1] = '\0';
	}
}

/** 154.600 → 154600 for pocsag-154600 host label. */
static void frequency_host_label(const char *mhz, char *out, size_t outlen)
{
	out[0] = '\0';
	if (!mhz || !mhz[0] || outlen < 4) return;
	char norm[64];
	strncpy(norm, mhz, sizeof(norm) - 1);
	norm[sizeof(norm) - 1] = '\0';
	normalize_frequency_mhz(norm, sizeof(norm));
	int int_part = 0, frac = 0;
	const char *dot = strchr(norm, '.');
	if (dot) {
		int_part = atoi(norm);
		frac = atoi(dot + 1);
		int digits = (int)strlen(dot + 1);
		if (digits == 1) frac *= 100;
		else if (digits == 2) frac *= 10;
		else if (digits > 3) {
			frac = 0;
			for (int i = 0; i < 3 && dot[1 + i]; i++)
				frac = frac * 10 + (dot[1 + i] - '0');
		}
	} else {
		int_part = atoi(norm);
	}
	snprintf(out, outlen, "%d%03d", int_part, frac);
}

static void build_channel(char *out, size_t outlen)
{
	char freq[64];
	strncpy(freq, Profile.pagercast_frequency, sizeof(freq) - 1);
	freq[sizeof(freq) - 1] = '\0';
	normalize_frequency_mhz(freq, sizeof(freq));
	int sub = Profile.pagercast_subscriber_id;
	if (sub > 0 && freq[0])
		snprintf(out, outlen, "sub%d_%s", sub, freq);
	else if (freq[0])
		snprintf(out, outlen, "%s", freq);
	else
		out[0] = '\0';
}

typedef struct {
	char *data;
	size_t len;
	size_t cap;
} MemBuf;

static size_t mem_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	MemBuf *m = (MemBuf *)userdata;
	size_t n = size * nmemb;
	if (m->len + n + 1 > m->cap) {
		size_t nc = m->cap ? m->cap * 2 : 4096;
		while (nc < m->len + n + 1) nc *= 2;
		char *p = (char *)realloc(m->data, nc);
		if (!p) return 0;
		m->data = p;
		m->cap = nc;
	}
	memcpy(m->data + m->len, ptr, n);
	m->len += n;
	m->data[m->len] = '\0';
	return n;
}

static CURL *make_curl(void)
{
	CURL *c = curl_easy_init();
	if (!c) return NULL;
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "PDL/1.0");
	/* Avoid inherited HTTP(S)_PROXY breaking direct HTTPS (CONNECT 403). */
	curl_easy_setopt(c, CURLOPT_PROXY, "");
	curl_easy_setopt(c, CURLOPT_NOPROXY, "*");
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	return c;
}

static struct curl_slist *auth_headers(void)
{
	struct curl_slist *hdrs = NULL;
	if (Profile.pagercast_api_key[0]) {
		char auth[400];
		snprintf(auth, sizeof(auth), "Authorization: Bearer %s", Profile.pagercast_api_key);
		hdrs = curl_slist_append(hdrs, auth);
	}
	return hdrs;
}

static int http_get(const char *url, MemBuf *out, long timeout_sec, long *http_code_out)
{
	CURL *c = make_curl();
	if (!c) return -1;
	struct curl_slist *hdrs = auth_headers();
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, timeout_sec);
	CURLcode rc = curl_easy_perform(c);
	long code = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	if (http_code_out) *http_code_out = code;
	curl_slist_free_all(hdrs);
	curl_easy_cleanup(c);
	if (rc != CURLE_OK || code < 200 || code >= 300) return -1;
	return 0;
}

static void add_node(const char *name, const char *base, long id)
{
	if (s_node_count >= PC_MAX_NODES || !base || !base[0]) return;
	for (int i = 0; i < s_node_count; i++) {
		if (strcmp(s_nodes[i].api_base_url, base) == 0) return;
	}
	PcNode *n = &s_nodes[s_node_count++];
	memset(n, 0, sizeof(*n));
	strncpy(n->display_name, name && name[0] ? name : base, sizeof(n->display_name) - 1);
	strncpy(n->api_base_url, base, sizeof(n->api_base_url) - 1);
	normalize_base(n->api_base_url, sizeof(n->api_base_url));
	n->carrier_id = id;
}

static int extract_string_field(const char *json, const char *key, char *out, size_t outlen)
{
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(json, pat);
	if (!p) return -1;
	p = strchr(p + strlen(pat), ':');
	if (!p) return -1;
	p++;
	while (*p && isspace((unsigned char)*p)) p++;
	if (*p != '"') return -1;
	p++;
	size_t o = 0;
	while (*p && *p != '"' && o + 1 < outlen) {
		if (*p == '\\' && p[1]) p++;
		out[o++] = *p++;
	}
	out[o] = '\0';
	return 0;
}

static long extract_long_field(const char *json, const char *key)
{
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(json, pat);
	if (!p) return 0;
	p = strchr(p + strlen(pat), ':');
	if (!p) return 0;
	return strtol(p + 1, NULL, 10);
}

/** Fetch /api/v2/radio/config with API token → subscriber_id, host_suffix, optional freq. */
static int fetch_radio_config(const char *api_base)
{
	if (!api_base || !api_base[0]) return -1;
	char base[256];
	strncpy(base, api_base, sizeof(base) - 1);
	base[sizeof(base) - 1] = '\0';
	normalize_base(base, sizeof(base));

	char url[PC_MAX_URL];
	snprintf(url, sizeof(url), "%s/api/v2/radio/config", base);
	MemBuf mb = {0};
	long code = 0;
	if (http_get(url, &mb, 10, &code) != 0 || !mb.data) {
		fprintf(stderr, "PagerCast: radio/config failed http=%ld @ %s\n", code, base);
		free(mb.data);
		return -1;
	}

	long sub = extract_long_field(mb.data, "subscriber_id");
	if (sub > 0) {
		Profile.pagercast_subscriber_id = (int)sub;
		fprintf(stderr, "PagerCast: subscriber_id=%ld from token\n", sub);
	}
	char suffix[64] = {0}, freq[32] = {0}, owner[256] = {0};
	if (extract_string_field(mb.data, "host_suffix", suffix, sizeof(suffix)) == 0 && suffix[0]) {
		strncpy(Profile.pagercast_host_suffix, suffix, sizeof(Profile.pagercast_host_suffix) - 1);
		Profile.pagercast_host_suffix[sizeof(Profile.pagercast_host_suffix) - 1] = '\0';
	}
	if (extract_string_field(mb.data, "frequency_owner_api_base", owner, sizeof(owner)) == 0 && owner[0]) {
		normalize_base(owner, sizeof(owner));
		strncpy(Profile.pagercast_api_base, owner, sizeof(Profile.pagercast_api_base) - 1);
		Profile.pagercast_api_base[sizeof(Profile.pagercast_api_base) - 1] = '\0';
	}
	if (!Profile.pagercast_frequency[0]) {
		if (extract_string_field(mb.data, "radio_frequency", freq, sizeof(freq)) != 0)
			extract_string_field(mb.data, "test_frequency", freq, sizeof(freq));
		if (freq[0]) {
			normalize_frequency_mhz(freq, sizeof(freq));
			strncpy(Profile.pagercast_frequency, freq, sizeof(Profile.pagercast_frequency) - 1);
		}
	}
	free(mb.data);
	return (Profile.pagercast_subscriber_id > 0) ? 0 : -1;
}

int pdl_pagercast_refresh_nodes(void)
{
	s_node_count = 0;
	add_node("HU Node 01", "https://hu-node01.pagercast.com", 0);
	add_node("DE Node 01", "https://de-node01.pagercast.com", 0);

	const char *bases[] = {
		Profile.pagercast_api_base[0] ? Profile.pagercast_api_base : NULL,
		"https://hu-node01.pagercast.com",
		"https://de-node01.pagercast.com",
		"https://pagercast.com",
	};

	for (size_t bi = 0; bi < sizeof(bases) / sizeof(bases[0]); bi++) {
		if (!bases[bi]) continue;
		char url[PC_MAX_URL];
		char base[256];
		strncpy(base, bases[bi], sizeof(base) - 1);
		base[sizeof(base) - 1] = '\0';
		normalize_base(base, sizeof(base));
		snprintf(url, sizeof(url), "%s/api/v2/bootstrap/http", base);
		MemBuf mb = {0};
		long code = 0;
		if (http_get(url, &mb, 8, &code) != 0) {
			free(mb.data);
			continue;
		}
		const char *p = mb.data;
		while (p && *p) {
			const char *obj = strstr(p, "\"api_base_url\"");
			if (!obj) break;
			const char *start = obj;
			while (start > mb.data && *start != '{') start--;
			const char *end = strchr(obj, '}');
			if (!end) break;
			char chunk[512];
			size_t clen = (size_t)(end - start + 1);
			if (clen >= sizeof(chunk)) clen = sizeof(chunk) - 1;
			memcpy(chunk, start, clen);
			chunk[clen] = '\0';
			char api[256] = {0}, name[128] = {0};
			extract_string_field(chunk, "api_base_url", api, sizeof(api));
			if (extract_string_field(chunk, "display_name", name, sizeof(name)) != 0)
				extract_string_field(chunk, "slug", name, sizeof(name));
			long id = extract_long_field(chunk, "carrier_id");
			if (api[0]) add_node(name, api, id);
			p = end + 1;
		}
		free(mb.data);
		break;
	}

	if (Profile.pagercast_api_key[0]) {
		const char *cfg_base = Profile.pagercast_api_base[0] ? Profile.pagercast_api_base :
			(s_node_count ? s_nodes[0].api_base_url : "https://hu-node01.pagercast.com");
		fetch_radio_config(cfg_base);
	}
	return s_node_count;
}

void pdl_pagercast_seed_fallback_nodes(void)
{
	if (s_node_count > 0) return;
	add_node("HU Node 01", "https://hu-node01.pagercast.com", 0);
	add_node("DE Node 01", "https://de-node01.pagercast.com", 0);
}

int pdl_pagercast_node_count(void)
{
	return s_node_count;
}

const char *pdl_pagercast_node_name(int i)
{
	if (i < 0 || i >= s_node_count) return "";
	return s_nodes[i].display_name;
}

const char *pdl_pagercast_node_url(int i)
{
	if (i < 0 || i >= s_node_count) return "";
	return s_nodes[i].api_base_url;
}

/* pocsag-golang SymbolHigh/Low — bit1 = neg, bit0 = pos. */
#define PC_SYMBOL_MARK  ((int16_t)-12287)
#define PC_SYMBOL_SPACE ((int16_t)12287)
#define PC_SPB          (PC_PCM_RATE / 1200) /* 40 samples/bit @ 1200 */
/* Sliding window ≈ 0.8 bit @ 1200 baud / 48 kHz (stronger mark/space separation). */
#define PC_FSK_WIN 32
#define PC_FSK_MARK_HZ  2200.0
#define PC_FSK_SPACE_HZ 1200.0

typedef struct {
	unsigned char pending[16384];
	size_t pending_len;
	int got_bytes;
	/* 1 = FSK tones (2200/1200), 0 = baseband DC levels. */
	int is_fsk;
	/* Sliding Goertzel FSK → baseband */
	int16_t fsk_ring[PC_FSK_WIN];
	int fsk_ri;
	int fsk_fill;
	int16_t fsk_level;
	int polarity; /* 0 = mark→bit1, 1 = swapped */
	double coeff_mark;
	double coeff_space;
	int coeffs_ok;
	/* Direct baseband bit slicer (bypasses Audio_To_Bits edge hunt). */
	int slice_phase;       /* samples into current bit 0..PC_SPB-1 */
	int64_t slice_acc;     /* sum of center-window samples */
	int slice_center_n;
	int slice_armed;       /* saw enough energy to start emitting */
	int slice_idle_run;    /* consecutive near-zero samples */
	int mode_shown;
} StreamState;

/* Stream-local polarity: flipped on inverted sync, never touches Profile.invert. */
static int s_bit_invert = 0;

void pdl_pagercast_flip_bit_polarity(void)
{
	s_bit_invert ^= 1;
}

extern void Reset_ATB(void);
extern POCSAG pocsag;
extern long BaudRate;
extern int config_index;
extern int pocsag_baud_rate;
extern int pocbit;
extern void display_showmo(int mode);

static void fsk_init_coeffs(StreamState *st)
{
	if (st->coeffs_ok) return;
	st->coeff_mark = 2.0 * cos(2.0 * M_PI * PC_FSK_MARK_HZ / (double)PC_PCM_RATE);
	st->coeff_space = 2.0 * cos(2.0 * M_PI * PC_FSK_SPACE_HZ / (double)PC_PCM_RATE);
	st->fsk_level = PC_SYMBOL_SPACE;
	st->coeffs_ok = 1;
}

static double goertzel_power(const int16_t *ring, int ri, int n, double coeff)
{
	double q1 = 0.0, q2 = 0.0;
	for (int i = 0; i < n; i++) {
		int idx = ri + i;
		if (idx >= n) idx -= n;
		double s0 = coeff * q1 - q2 + (double)ring[idx];
		q2 = q1;
		q1 = s0;
	}
	return q1 * q1 + q2 * q2 - coeff * q1 * q2;
}

/*
 * PagerCast UseFSK=true streams AFSK (mark=2200 Hz = bit1, space=1200 = bit0).
 * Convert to SymbolHigh/Low baseband for the bit slicer.
 */
static int16_t fsk_to_baseband(StreamState *st, int16_t sample)
{
	fsk_init_coeffs(st);
	st->fsk_ring[st->fsk_ri] = sample;
	st->fsk_ri++;
	if (st->fsk_ri >= PC_FSK_WIN) st->fsk_ri = 0;
	if (st->fsk_fill < PC_FSK_WIN) {
		st->fsk_fill++;
		return st->fsk_level;
	}

	double em = goertzel_power(st->fsk_ring, st->fsk_ri, PC_FSK_WIN, st->coeff_mark);
	double es = goertzel_power(st->fsk_ring, st->fsk_ri, PC_FSK_WIN, st->coeff_space);
	/*
	 * Quiet RF: do NOT hold the last ±symbol level. Holding it prevented the
	 * slicer from seeing end-of-burst silence, so short NUMERIC pages that
	 * finished without a clean IDLE word never flushed to the UI.
	 */
	if (em + es < 1.0e10)
		return 0;
	int mark = (em > es);
	if (st->polarity) mark = !mark;
	st->fsk_level = mark ? PC_SYMBOL_MARK : PC_SYMBOL_SPACE;
	return st->fsk_level;
}

static void emit_pocsag_bit(StreamState *st, int bit)
{
	BaudRate = 1200;
	config_index = INDEX1200;
	pocsag_baud_rate = STAT_POCSAG1200;
	if (pocbit < 400)
		pocbit = 1500;
	if (!st->mode_shown) {
		display_showmo(MODE_POCSAG + MODE_P1200);
		st->mode_shown = 1;
	}
	/* Use stream polarity only — ignore Profile.invert (can be flipped by false sync). */
	if (s_bit_invert)
		bit ^= 1;
	pocsag.frame(bit ? 1 : 0);
}

/*
 * Center-window integrate (pocsag-golang style): sum the middle 50% of each
 * 40-sample bit at 48 kHz / 1200 baud. No mid-bit edge restart — that was
 * shifting phase and producing "@*" after the sender label.
 *
 * Long idle (near-zero) disarms; next energetic edge re-arms on a bit boundary.
 */
static void slice_baseband_sample(StreamState *st, int16_t s)
{
	const int spb = PC_SPB;
	const int win0 = spb / 4;       /* start of center window */
	const int win1 = (spb * 3) / 4; /* end of center window */
	const int idle_thresh = 1500;
	const int active_thresh = 4000;

	if (s > -idle_thresh && s < idle_thresh) {
		st->slice_idle_run++;
		/*
		 * ~100 ms of silence → between pages (not mid-burst).
		 * 20 ms was short enough to flush after the address word and drop
		 * the numeric body on weak / fading baseband.
		 */
		if (st->slice_idle_run > (PC_PCM_RATE / 10)) {
			if (st->slice_armed) {
				/*
				 * Audio went quiet before IDLE codewords were clocked.
				 * Flush the pending POCSAG page now, otherwise it only
				 * appears when the next page's address word arrives.
				 */
				pocsag.frame(-1);
			}
			st->slice_armed = 0;
			st->slice_phase = 0;
			st->slice_acc = 0;
			st->slice_center_n = 0;
		}
		/* Still clock bits during short gaps inside a page (don't drop phase). */
	} else {
		st->slice_idle_run = 0;
	}

	if (!st->slice_armed) {
		if (s <= -active_thresh || s >= active_thresh) {
			st->slice_armed = 1;
			st->slice_phase = 0;
			st->slice_acc = 0;
			st->slice_center_n = 0;
		} else {
			return;
		}
	}

	if (st->slice_phase >= win0 && st->slice_phase < win1) {
		st->slice_acc += s;
		st->slice_center_n++;
	}
	st->slice_phase++;

	if (st->slice_phase >= spb) {
		int64_t avg = st->slice_center_n ? st->slice_acc : (int64_t)s;
		/* SymbolHigh (bit1) is negative; SymbolLow (bit0) is positive. */
		int bit = (avg < 0) ? 1 : 0;
		emit_pocsag_bit(st, bit);
		st->slice_phase = 0;
		st->slice_acc = 0;
		st->slice_center_n = 0;
	}
}

/* Convert pending PCM and feed decoder. Leaves 0–1 byte in pending. */
static void process_pending(StreamState *st)
{
	const int use_fsk = (st->is_fsk == 1);

	while (st->pending_len >= 2) {
		size_t samples = st->pending_len / 2;
		if (samples > 8192)
			samples = 8192;

		unsigned int peak = 0;
		for (size_t i = 0; i < samples; i++) {
			int16_t raw = (int16_t)(st->pending[i * 2] | (st->pending[i * 2 + 1] << 8));
			unsigned int mag = (unsigned)(raw < 0 ? -raw : raw);
			if (mag > peak) peak = mag;
			int16_t s = use_fsk ? fsk_to_baseband(st, raw) : raw;
			slice_baseband_sample(st, s);
		}
		/* Spectrum viz (does not affect decode). */
		pdl_gpu_spectrum_push_s16((const short *)st->pending, (int)samples);
		s_stream_level = (peak * 100.0) / 32768.0;

		size_t used = samples * 2;
		memmove(st->pending, st->pending + used, st->pending_len - used);
		st->pending_len -= used;
	}
}

static void feed_s16le(StreamState *st, const unsigned char *data, size_t len)
{
	if (len > 0) st->got_bytes = 1;

	/* Never drop bytes: curl requires the write callback to consume the full buffer. */
	while (len > 0) {
		size_t avail = sizeof(st->pending) - st->pending_len;
		if (avail == 0) {
			process_pending(st);
			avail = sizeof(st->pending) - st->pending_len;
			if (avail == 0)
				break;
		}
		size_t take = len < avail ? len : avail;
		memcpy(st->pending + st->pending_len, data, take);
		st->pending_len += take;
		data += take;
		len -= take;
		process_pending(st);
	}
}

static size_t header_cb(char *buffer, size_t size, size_t nitems, void *userdata)
{
	StreamState *st = (StreamState *)userdata;
	size_t n = size * nitems;
	if (!st || n < 8) return n;
	char line[256];
	size_t copy = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
	memcpy(line, buffer, copy);
	line[copy] = '\0';
	for (size_t i = 0; i < copy; i++) {
		if (line[i] >= 'A' && line[i] <= 'Z')
			line[i] = (char)(line[i] - 'A' + 'a');
	}
	if (strstr(line, "x-pagercast-radio-fsk:")) {
		/* Only enable FSK demod when the server explicitly says so.
		 * Production often sends baseband (false); wrongly demoding destroys decode. */
		if (strstr(line, "true") || strstr(line, ": 1"))
			st->is_fsk = 1;
		else
			st->is_fsk = 0;
		fprintf(stderr, "PagerCast: stream FSK=%d\n", st->is_fsk);
	}
	return n;
}

static size_t stream_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	if (!s_want_stream || !s_running) return 0;
	StreamState *st = (StreamState *)userdata;
	size_t n = size * nmemb;
	feed_s16le(st, (const unsigned char *)ptr, n);
	if (st->got_bytes && !s_connected) {
		s_connected = 1;
		set_status_state("Streaming", PDL_PC_STREAMING);
	}
	return n;
}

static int stream_xferinfo(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
	curl_off_t ultotal, curl_off_t ulnow)
{
	(void)clientp; (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
	return (s_want_stream && s_running) ? 0 : 1;
}

/* Returns: 1 = streamed then stopped/ended, 0 = failed (try next). */
static int try_stream_url(const char *url, char *errbuf, size_t errlen)
{
	/* Redact token in logs */
	{
		char logu[PC_MAX_URL];
		strncpy(logu, url, sizeof(logu) - 1);
		logu[sizeof(logu) - 1] = '\0';
		char *tok = strstr(logu, "api_token=");
		if (tok) strcpy(tok + 10, "…");
		fprintf(stderr, "PagerCast: trying %s\n", logu);
	}
	set_status_state("Connecting…", PDL_PC_CONNECTING);
	CURL *c = make_curl();
	if (!c) {
		if (errbuf) snprintf(errbuf, errlen, "curl init failed");
		return 0;
	}
	struct curl_slist *hdrs = auth_headers();
	StreamState st;
	memset(&st, 0, sizeof(st));
	st.is_fsk = 0; /* baseband unless X-PagerCast-Radio-FSK: true */
	s_bit_invert = 0;
	Reset_ATB();
	pocsag.frame(-1);
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, &st);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, stream_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &st);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 0L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 12L);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, stream_xferinfo);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 0L);
	curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(c, CURLOPT_TCP_KEEPIDLE, 30L);
	curl_easy_setopt(c, CURLOPT_TCP_KEEPINTVL, 15L);

	CURLcode rc = curl_easy_perform(c);
	long code = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(hdrs);
	curl_easy_cleanup(c);

	if (!s_want_stream && st.got_bytes)
		return 1; /* user disconnect after successful stream */
	if (st.got_bytes && (rc == CURLE_OK || rc == CURLE_ABORTED_BY_CALLBACK || rc == CURLE_WRITE_ERROR))
		return 1;
	if (code >= 200 && code < 300 && st.got_bytes)
		return 1;

	fprintf(stderr, "PagerCast: stream failed rc=%d http=%ld\n", (int)rc, code);
	if (errbuf) {
		if (code == 401 || code == 403)
			snprintf(errbuf, errlen, "Auth failed (HTTP %ld)", code);
		else if (code > 0)
			snprintf(errbuf, errlen, "HTTP %ld", code);
		else
			snprintf(errbuf, errlen, "%s", curl_easy_strerror(rc));
	}
	return 0;
}

static void stream_thread_cleanup(void)
{
	s_connected = 0;
	s_running = 0;
	s_stream_level = 0.0;
	pdl_linux_set_pagercast_audio_exclusive(0);
	if (s_have_saved_rate) {
		Profile.audioSampleRate = s_saved_sample_rate;
		s_have_saved_rate = 0;
	}
	pdl_linux_resume_local_capture();
}

/* Sleep up to sec seconds, aborting early if user disconnects. */
static void sleep_want(int sec)
{
	for (int i = 0; i < sec * 10 && s_want_stream; i++)
		usleep(100000);
}

static int run_one_stream_attempt(char *last_err, size_t errlen)
{
	normalize_frequency_mhz(Profile.pagercast_frequency, sizeof(Profile.pagercast_frequency));

	if (!Profile.pagercast_frequency[0]) {
		snprintf(last_err, errlen, "Missing frequency");
		return 0;
	}

	if (Profile.pagercast_api_key[0] && Profile.pagercast_subscriber_id <= 0) {
		set_status_state("Resolving account…", PDL_PC_CONNECTING);
		const char *bases_try[4];
		int nb = 0;
		if (Profile.pagercast_api_base[0]) bases_try[nb++] = Profile.pagercast_api_base;
		bases_try[nb++] = "https://hu-node01.pagercast.com";
		bases_try[nb++] = "https://de-node01.pagercast.com";
		int ok = 0;
		for (int i = 0; i < nb && s_want_stream; i++) {
			if (fetch_radio_config(bases_try[i]) == 0) { ok = 1; break; }
		}
		if (!ok) {
			snprintf(last_err, errlen, "API key rejected (radio/config)");
			return 0;
		}
	}

	const char *suffix = Profile.pagercast_host_suffix[0] ?
		Profile.pagercast_host_suffix : "pagercast.com";
	char label[32];
	frequency_host_label(Profile.pagercast_frequency, label, sizeof(label));

	char channel[64];
	build_channel(channel, sizeof(channel));
	fprintf(stderr, "PagerCast: freq=%s label=%s channel=%s sub=%d\n",
		Profile.pagercast_frequency, label, channel, Profile.pagercast_subscriber_id);

	char urls[10][PC_MAX_URL];
	int nurl = 0;

	if (label[0]) {
		snprintf(urls[nurl++], PC_MAX_URL, "https://pocsag-%s.%s/stream", label, suffix);
	}

	char base[256];
	strncpy(base, Profile.pagercast_api_base, sizeof(base) - 1);
	base[sizeof(base) - 1] = '\0';
	normalize_base(base, sizeof(base));
	if (base[0] && channel[0]) {
		char *enc = curl_easy_escape(NULL, channel, 0);
		snprintf(urls[nurl++], PC_MAX_URL, "%s/api/v2/radio/%s/stream",
			base, enc ? enc : channel);
		if (enc) curl_free(enc);
	}

	if (channel[0]) {
		static const char *edges[] = {
			"https://hu-node01.pagercast.com",
			"https://de-node01.pagercast.com",
		};
		char *enc = curl_easy_escape(NULL, channel, 0);
		for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]) && nurl < 10; i++) {
			if (base[0] && strcmp(base, edges[i]) == 0) continue;
			snprintf(urls[nurl++], PC_MAX_URL, "%s/api/v2/radio/%s/stream",
				edges[i], enc ? enc : channel);
		}
		if (enc) curl_free(enc);
	}

	snprintf(last_err, errlen, "No stream URL");
	for (int i = 0; i < nurl && s_want_stream; i++) {
		s_running = 1;
		if (try_stream_url(urls[i], last_err, errlen))
			return 1;
	}
	return 0;
}

static void *stream_thread(void *arg)
{
	(void)arg;

	pdl_linux_set_pagercast_audio_exclusive(1);
	pdl_linux_hw_decode_stop();
	pdl_linux_pause_local_capture();

	int backoff = 1;
	while (s_want_stream) {
		s_running = 1;
		s_connected = 0;
		char last_err[128] = "No stream URL";
		int streamed = run_one_stream_attempt(last_err, sizeof(last_err));

		if (!s_want_stream)
			break;

		s_connected = 0;
		s_running = 0;
		s_stream_level = 0.0;

		if (streamed) {
			/* Server closed a live stream — reconnect quickly. */
			backoff = 1;
			set_status_state("Reconnecting…", PDL_PC_CONNECTING);
		} else {
			char msg[160];
			snprintf(msg, sizeof(msg), "%s — retry in %ds", last_err, backoff);
			set_status_state(msg, PDL_PC_CONNECTING);
		}
		fprintf(stderr, "PagerCast: stream ended (streamed=%d), retry in %ds\n",
			streamed, backoff);
		sleep_want(backoff);
		if (backoff < 30)
			backoff = (backoff < 8) ? backoff * 2 : 30;
	}

	set_status_state("Disconnected", PDL_PC_DISCONNECTED);
	stream_thread_cleanup();
	return NULL;
}

int pdl_pagercast_connect(void)
{
	normalize_frequency_mhz(Profile.pagercast_frequency, sizeof(Profile.pagercast_frequency));
	trim_inplace(Profile.pagercast_api_key);

	if (!Profile.pagercast_frequency[0]) {
		set_status_state("Set frequency first", PDL_PC_ERROR);
		return -1;
	}
	if (!Profile.pagercast_api_key[0]) {
		set_status_state("API key required", PDL_PC_ERROR);
		return -1;
	}

	/* Already streaming / reconnecting. */
	if (s_want_stream && !s_thread_joined)
		return 0;

	if (!Profile.pagercast_api_base[0] && s_node_count == 0)
		pdl_pagercast_seed_fallback_nodes();
	if (!Profile.pagercast_api_base[0] && s_node_count > 0) {
		strncpy(Profile.pagercast_api_base, s_nodes[0].api_base_url,
			sizeof(Profile.pagercast_api_base) - 1);
	}

	/* Clear cached sub id so connect always re-resolves from token. */
	Profile.pagercast_subscriber_id = 0;

	if (!s_have_saved_rate) {
		s_saved_sample_rate = Profile.audioSampleRate;
		s_have_saved_rate = 1;
	}
	Profile.audioSampleRate = PC_PCM_RATE;

	s_want_stream = 1;
	s_running = 1;
	s_connected = 0;
	s_thread_joined = 0;
	set_status_state("Connecting…", PDL_PC_CONNECTING);
	if (pthread_create(&s_thread, NULL, stream_thread, NULL) != 0) {
		s_want_stream = 0;
		s_running = 0;
		s_thread_joined = 1;
		set_status_state("Thread failed", PDL_PC_ERROR);
		if (s_have_saved_rate) {
			Profile.audioSampleRate = s_saved_sample_rate;
			s_have_saved_rate = 0;
		}
		return -1;
	}
	return 0;
}

static void stop_stream_thread(void)
{
	s_want_stream = 0;
	s_running = 0;
	s_connected = 0;
	if (!s_thread_joined) {
		pthread_join(s_thread, NULL);
		s_thread_joined = 1;
	}
	if (s_have_saved_rate) {
		Profile.audioSampleRate = s_saved_sample_rate;
		s_have_saved_rate = 0;
	}
	pdl_linux_set_pagercast_audio_exclusive(0);
	pdl_linux_resume_local_capture();
}

void pdl_pagercast_disconnect(void)
{
	/* Stop stream; keep Profile.pagercast_enabled (Integrations → Enable). */
	stop_stream_thread();
	set_status_state("Disconnected", PDL_PC_DISCONNECTED);
}

int pdl_pagercast_is_wanted(void)
{
	return s_want_stream ? 1 : 0;
}

void pdl_pagercast_shutdown(void)
{
	/* App exit: stop stream but keep Profile.pagercast_enabled for next launch. */
	stop_stream_thread();
	set_status_state("Disconnected", PDL_PC_DISCONNECTED);
}

#endif
