/*
 * Spectrum stubs — waterfall disabled (it froze the GTK UI).
 * Keep symbols so audio/stream call sites stay linked without work.
 */
#ifdef __linux__

#include "linux/gpu_spectrum.h"

void pdl_gpu_spectrum_init(void) {}
void pdl_gpu_spectrum_shutdown(void) {}
void pdl_gpu_spectrum_push_u8(const unsigned char *samples, int n)
{
	(void)samples;
	(void)n;
}
void pdl_gpu_spectrum_push_s16(const short *samples, int n)
{
	(void)samples;
	(void)n;
}
int pdl_gpu_spectrum_update(void) { return 0; }
const float *pdl_gpu_spectrum_bins(int *n_bins)
{
	if (n_bins) *n_bins = 0;
	return 0;
}
int pdl_gpu_spectrum_using_gpu(void) { return 0; }

#endif
