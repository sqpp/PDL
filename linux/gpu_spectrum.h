#ifndef PDL_LINUX_GPU_SPECTRUM_H
#define PDL_LINUX_GPU_SPECTRUM_H

#ifdef __linux__
#ifdef __cplusplus
extern "C" {
#endif

void pdl_gpu_spectrum_init(void);
void pdl_gpu_spectrum_shutdown(void);

void pdl_gpu_spectrum_push_u8(const unsigned char *samples, int n);
void pdl_gpu_spectrum_push_s16(const short *samples, int n);

/* ≤10 Hz. Returns 1 if bins updated. Safe to call from a slow GTK timer only. */
int pdl_gpu_spectrum_update(void);

const float *pdl_gpu_spectrum_bins(int *n_bins);

int pdl_gpu_spectrum_using_gpu(void);

enum { PDL_GPU_SPECTRUM_N = 256 };

#ifdef __cplusplus
}
#endif
#endif

#endif
