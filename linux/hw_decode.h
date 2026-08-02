#ifndef PDL_LINUX_HW_DECODE_H
#define PDL_LINUX_HW_DECODE_H

#ifdef __linux__
/* Start RS232 bitstream → pdl_decode (pauses sound / disconnects PagerCast). */
int pdl_linux_hw_decode_start(void);
void pdl_linux_hw_decode_stop(void);
int pdl_linux_hw_decode_active(void);
/* Apply Profile.comPortRS232 / comPort — start or stop as needed. */
void pdl_linux_hw_decode_apply_settings(void);
#endif

#endif
