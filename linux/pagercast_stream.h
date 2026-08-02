#ifndef PDL_PAGERCAST_STREAM_H
#define PDL_PAGERCAST_STREAM_H

#ifdef __linux__

typedef enum {
	PDL_PC_DISCONNECTED = 0, /* red — idle / offline */
	PDL_PC_CONNECTING   = 1, /* orange — resolving / connecting */
	PDL_PC_STREAMING    = 2, /* green — live audio */
	PDL_PC_ERROR        = 3  /* red — failed */
} PdlPagerCastState;

/* Refresh node list from bootstrap (+ fallbacks). Returns count. */
int pdl_pagercast_refresh_nodes(void);
int pdl_pagercast_node_count(void);
const char *pdl_pagercast_node_name(int i);
const char *pdl_pagercast_node_url(int i);

int pdl_pagercast_connect(void);
void pdl_pagercast_disconnect(void);
void pdl_pagercast_shutdown(void);
int pdl_pagercast_is_connected(void);
int pdl_pagercast_is_wanted(void); /* user wants stream (even while reconnecting) */
/* Flip bit polarity when inverted sync is detected (does not touch Profile.invert). */
void pdl_pagercast_flip_bit_polarity(void);
const char *pdl_pagercast_status(void);
PdlPagerCastState pdl_pagercast_state(void);
/* Short label for UI: Offline / Connecting / Streaming / Error */
const char *pdl_pagercast_state_label(void);
/* Footer "Input:" text while streaming, e.g. "PagerCast 154.600 @ de-node01". NULL if offline. */
const char *pdl_pagercast_input_label(void);
/* 0..100 peak from stream PCM while connected; else 0. */
double pdl_pagercast_get_input_level(void);
/* Seed HU/DE fallbacks without network I/O. */
void pdl_pagercast_seed_fallback_nodes(void);

#endif
#endif
