
/*
Copyright 1997-1999 Miller Puckette, Music Department, UCSD (msp@ucsd.edu)
and Ted Apel, CRCA, UCSD (tapel@ucsd.edu).
Permission is granted to use this software for any noncommercial purpose.
For commercial licensing contact the UCSD Technology Transfer Office.

UC MAKES NO WARRANTY, EXPRESS OR IMPLIED, IN CONNECTION WITH THIS SOFTWARE!

This file is downloadable from http://crca.ucsd.edu/~msp
*/

/*
 * Fiddle is a pitch tracker hardwired to have hop size ("H") equal to
 * half its window size ("N").
 *
 * This version should compile for Max "0.26," JMAX, Pd, or Max/MSP.
 *
 * The "lastanalysis" field holds the shifted FT of the previous H
 * samples.  The buffer contains in effect points 1/2,  3/2, ..., (N-1)/2
 * of the DTFT of a real vector of length N, half of whose points are zero,
 * i.e.,  only the first H points are used.  Put another way, we get the
 * the odd-numbered points of the FFT of the H points, zero padded to 4*H in
 * length. The integer points 0, 1, ..., H-1
 * are found by interpolating these others,  using the fact that the
 * half-integer points are band-limited (they only have positive frequencies.)
 * To facilitate the interpolation the "lastanalysis" buffer contains
 * FILTSIZE extra points (1/2-FILTSIZE, ...,  -1/2) at the beginning and
 * FILTSIZE again at the end ((N+1)/2, ..., FILTSIZE+(N-1)/2).  The buffer
 * therefore has N+4*FILTSIZE floating-point numbers in it.
 *
 * after doing this I found out that you can just do a real FFT
 * of the H new points, zero-padded to contain N points, and using a similar
 * but simpler interpolation scheme you can still get 2N points of the DTFT
 * of the N points.  Jean Laroche is a big fat hen.
 *
 */
#define MSP

#ifdef NT
#define flog log
#define fexp exp
#define fsqrt sqrt
#pragma warning (disable: 4305 4244)
#endif



char fiddle_version[] = "fiddle~ v1.2 -- 64 bit version by vb, 2015";


#ifdef PD
#include "m_pd.h"
#endif /* PD */

#ifdef MSP
#define flog log
#define fexp exp
#define fsqrt sqrt
#endif /* MSP */

#ifdef MSP
#define t_floatarg double	// this is a guess based on MAX26
#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include "fft_mayer.proto.h"



#endif /* MSP */

#include <math.h>


#define MINBIN 3
#define DEFAMPLO 40
#define DEFAMPHI 50
#define DEFATTACKTIME 100
#define DEFATTACKTHRESH 10
#define DEFVIBTIME 50
#define DEFVIBDEPTH 0.5
#define GLISS 0.7f
#define DBFUDGE 30.8f
#define MINFREQINBINS 5     /* minimum frequency in bins for reliable output */

#define MAXNPITCH 3
#define MAXHIST 3		    /* find N hottest peaks in histogram */

#define MAXPOINTS 8192
#define MINPOINTS 128
#define DEFAULTPOINTS 1024

#define HISTORY 20
#define MAXPEAK 100		/* maximum number of peaks */
#define DEFNPEAK 20		/* default number of peaks */

#define MAXNPEAK (MAXLOWPEAK + MAXSTRONGPEAK)
#define MINBW (0.03f)			/* consider BW >= 0.03 FFT bins */

#define BINPEROCT 48			/* bins per octave */
#define BPERO_OVER_LOG2 69.24936196f	/* BINSPEROCT/log(2) */
#define FACTORTOBINS (float)(4/0.0145453)	/* 4 / (pow(2.,1/48.) - 1) */
#define BINGUARD 10			/* extra bins to throw in front */
#define PARTIALDEVIANCE 0.023f		/* acceptable partial detuning in % */
#define LOGTODB 4.34294481903f		/* 20/log(10) */

#define KNOCKTHRESH 10.f     /* don't know how to describe this */


static float sigfiddle_partialonset[] =
{
0,
48,
76.0782000346154967102,
96,
111.45254855459339269887,
124.07820003461549671089,
134.75303625876499715823,
144,
152.15640006923099342109,
159.45254855459339269887,
166.05271769459026829915,
172.07820003461549671088,
177.62110647077242370064,
182.75303625876499715892,
187.53074858920888940907,
192,
};

#define NPARTIALONSET ((int)(sizeof(sigfiddle_partialonset)/sizeof(float)))

static int sigfiddle_intpartialonset[] =
{
0,
48,
76,
96,
111,
124,
135,
144,
152,
159,
166,
172,
178,
183,
188,
192,
};

/* these coefficients, which come from the "upsamp" subdirectory,
are a filter kernel for upsampling by a factor of two, assuming
the sound to be upsampled has no energy above half the Nyquist, i.e.,
that it's already 2x oversampled compared to the theoretically possible
sample rate.  I got these by trial and error. */

#define FILT1 ((float)(.5 * 1.227054))
#define FILT2 ((float)(.5 * -0.302385))
#define FILT3 ((float)(.5 * 0.095326))
#define FILT4 ((float)(.5 * -0.022748))
#define FILT5 ((float)(.5 * 0.002533))
#define FILTSIZE 5

typedef struct peakout	    /* a peak for output */
{
    float po_freq;		    /* frequency in hz */
    float po_amp;	    	    /* amplitude */
} t_peakout;

typedef struct peak 	    /* a peak for analysis */
{
    float p_freq;		    /* frequency in bins */
    float p_width;		    /* peak width in bins */
    float p_pow;		    /* peak power */
    float p_loudness;	    	    /* 4th root of power */
    float *p_fp;		    /* pointer back to spectrum */
} t_peak;

typedef struct histopeak
{
    float h_pitch;		    /* estimated pitch */
    float h_value;		    /* value of peak */
    float h_loud;		    /* combined strength of found partials */
    int h_index;		    /* index of bin holding peak */
    int h_used;			    /* true if an x_hist entry points here */
} t_histopeak;

typedef struct pitchhist	    /* struct for keeping history by pitch */
{
    float h_pitch;		    /* pitch to output */
    float h_amps[HISTORY];	    /* past amplitudes */
    float h_pitches[HISTORY];	    /* past pitches */
    float h_noted;		    /* last pitch output */
    int h_age;			    /* number of frames pitch has been there */
    t_histopeak *h_wherefrom;	    /* new histogram peak to incorporate */
    void *h_outlet;
} t_pitchhist;

typedef struct sigfiddle		    /* instance struct */
{

#ifdef MSP
	t_pxobject x_obj;
	void *x_clock;
	long x_downsample;		// downsample feature because of MSP's large sig vector sizes
#endif
    float *x_inbuf;		    /* buffer to analyze, npoints/2 elems */
    float *x_lastanalysis;	    /* FT of last buffer (see main comment) */
    float *x_spiral;		    /* 1/4-wave complex exponential */
    t_peakout *x_peakbuf;   	    /* spectral peaks for output */
    int x_npeakout; 	    	    /* number of spectral peaks to output */
    int x_npeakanal;	    	    /* number of spectral peaks to analyze */
    int x_phase;		    /* number of points since last output */
    int x_histphase;		    /* phase into amplitude history vector */
    int x_hop;			    /* period of output, npoints/2 */
    float x_sr;			    /* sample rate */
    t_pitchhist x_hist[MAXNPITCH];  /* history of current pitches */
    int x_nprint;		    /* how many periods to print */
    int x_npitch;		    /* number of simultaneous pitches */
    float x_dbs[HISTORY];	    /* DB history, indexed by "histphase" */
    float x_peaked;		    /* peak since last attack */
    int x_dbage;		    /* number of bins DB has met threshold */
    int x_auto;     	    	    /* true if generating continuous output */
/* parameters */
    float x_amplo;
    float x_amphi;
    int x_attacktime;
    int x_attackbins;
    float x_attackthresh;
    int x_vibtime;
    int x_vibbins;
    float x_vibdepth;
    float x_npartial;
/* outlets & clock */
    void *x_envout;
    int x_attackvalue;
    void *x_attackout;
    void *x_noteout;
    void *x_peakout;
} t_sigfiddle;

#if CHECKER
float fiddle_checker[1024];
#endif

#ifdef MSP
// Mac compiler requires prototypes for everything

int sigfiddle_ilog2(int n);
float fiddle_mtof(float f);
float fiddle_ftom(float f);
void sigfiddle_doit(t_sigfiddle *x);
void sigfiddle_debug(t_sigfiddle *x);
void sigfiddle_print(t_sigfiddle *x);
void sigfiddle_assist(t_sigfiddle *x, void *b, long m, long a, char *s);
void sigfiddle_amprange(t_sigfiddle *x, double amplo,  double amphi);
void sigfiddle_reattack(t_sigfiddle *x, t_floatarg attacktime, t_floatarg attackthresh);
void sigfiddle_vibrato(t_sigfiddle *x, t_floatarg vibtime, t_floatarg vibdepth);
void sigfiddle_npartial(t_sigfiddle *x, double npartial);
void sigfiddle_auto(t_sigfiddle *x, t_floatarg f);
int sigfiddle_doinit(t_sigfiddle *x, long npoints, long npitch, long npeakanal, long npeakout);
static t_int *fiddle_perform(t_int *w);
void sigfiddle_dsp(t_sigfiddle *x, t_signal **sp);
/* add prototypes for 64 bit perform and dsp setup */
void fiddle_perform64(t_sigfiddle *x, t_object *dsp64, double **ins, long numins,
					  double **outs, long numouts, long sampleframes, long flags, void *userparam);
void sigfiddle_dsp64(t_sigfiddle *x, t_object *dsp64, short *count, double samplerate,
					 long maxvectorsize, long flags);
/* */
void sigfiddle_tick(t_sigfiddle *x);
void sigfiddle_bang(t_sigfiddle *x);
void sigfiddle_ff(t_sigfiddle *x);
//void *sigfiddle_new(long npoints, long npitch);
void *sigfiddle_new(long npoints, long npitch, long npeakanal, long npeakout);
void msp_fft(float *buf, long np, long inv);
float msp_ffttemp[MAXPOINTS*2];
int errno;
#endif

int sigfiddle_ilog2(int n)
{
    int ret = -1;
    while (n)
    {
	n >>= 1;
	ret++;
    }
    return (ret);
}

float fiddle_mtof(float f)
{
	return (8.17579891564 * exp(.0577622650 * f));
}

float fiddle_ftom(float f)
{
	return (17.3123405046 * log(.12231220585 * f));
}
#define ftom fiddle_ftom
#define mtof fiddle_mtof

void sigfiddle_doit(t_sigfiddle *x)
{
#ifdef MSP
	// prevents interrupt-level stack overflow crash with Netscape.
    static float spect1[4*MAXPOINTS];
    static float spect2[MAXPOINTS + 4*FILTSIZE];
#else
    float spect1[4*MAXPOINTS];
    float spect2[MAXPOINTS + 4*FILTSIZE];
#endif
#if CHECKER
    float checker3[4*MAXPOINTS];
#endif

    t_peak peaklist[MAXPEAK + 1], *pk1;
    t_peakout *pk2;
    t_histopeak histvec[MAXHIST], *hp1;
    int i, j, k, hop = x->x_hop, n = 2*hop, npeak, npitch,
        logn = sigfiddle_ilog2(n), newphase, oldphase;
    float *fp, *fp1, *fp2, *fp3, total_power, total_loudness, total_db;
    float maxbin = BINPEROCT * (logn-2),  *histogram = spect2 + BINGUARD;
    t_pitchhist *phist;
    float hzperbin = x->x_sr / (2.0f * n);
    int npeakout = x->x_npeakout, npeakanal = x->x_npeakanal;
    int npeaktot = (npeakout > npeakanal ? npeakout : npeakanal);

    oldphase = x->x_histphase;
    newphase = x->x_histphase + 1;
    if (newphase == HISTORY) newphase = 0;
    x->x_histphase = newphase;

	/*
	 * multiply the H points by a 1/4-wave complex exponential,
	 * and take FFT of the result.
	 */
    for (i = 0, fp1 = x->x_inbuf, fp2 = x->x_spiral, fp3 = spect1;
	i < hop; i++, fp1++, fp2 += 2, fp3 += 2)
	    fp3[0] = fp1[0] * fp2[0], fp3[1] = fp1[0] * fp2[1];

#ifdef MAX26
    fft(spect1, hop, 0);
#endif
#ifdef PD
    pd_fft(spect1, hop, 0);
#endif
#ifdef FTS1X
    fts_cfft_inplc((complex *)spect1, hop);
#endif
#ifdef MSP
	msp_fft(spect1,hop,0);
#endif
	/*
	 * now redistribute the points to get in effect the odd-numbered
	 * points of the FFT of the H points, zero padded to 4*H in length.
	 */
    for (i = 0, fp1 = spect1, fp2 = spect2 + (2*FILTSIZE);
	i < (hop>>1); i++, fp1 += 2, fp2 += 4)
	    fp2[0] = fp1[0], fp2[1] = fp1[1];
    for (i = 0, fp1 = spect1 + n - 2, fp2 = spect2 + (2*FILTSIZE+2);
	i < (hop>>1); i++, fp1 -= 2, fp2 += 4)
	    fp2[0] = fp1[0], fp2[1] = -fp1[1];
    for (i = 0, fp1 = spect2 + (2*FILTSIZE), fp2 = spect2 + (2*FILTSIZE-2);
	i<FILTSIZE; i++, fp1+=2, fp2-=2)
	    fp2[0] = fp1[0],  fp2[1] = -fp1[1];
    for (i = 0, fp1 = spect2 + (2*FILTSIZE+n-2), fp2 = spect2 + (2*FILTSIZE+n);
	i<FILTSIZE; i++, fp1-=2, fp2+=2)
	    fp2[0] = fp1[0],  fp2[1] = -fp1[1];
#if 0
    {
	fp = spect2 + 2*FILTSIZE;
	post("x1 re %12.4f %12.4f %12.4f %12.4f %12.4f",
	    fp[0], fp[2], fp[4], fp[6], fp[8]);
	post("x1 im %12.4f %12.4f %12.4f %12.4f %12.4f",
	    fp[1], fp[3], fp[5], fp[7], fp[9]);
    }
#endif
	/* spect2 is now prepared; now combine spect2 and lastanalysis into
	 * spect1.  Odd-numbered points of spect1 are the points of "last"
	 * plus (-i, i, -i, ...) times spect1.  Even-numbered points are
	 * the interpolated points of "last" plus (1, -1, 1, ...) times the
	 * interpolated points of spect1.
	 *
	 * To interpolate, take FILT1 exp(-pi/4) times
	 * the previous point,  FILT2*exp(-3*pi/4) times 3 bins before,
	 * etc,  and FILT1 exp(pi/4), FILT2 exp(3pi/4), etc., to weight
	 * the +1, +3, etc., points.
	 *
	 * In this calculation,  we take (1, i, -1, -i, 1) times the
	 * -9, -7, ..., -1 points, and (i, -1, -i, 1, i) times the 1, 3,..., 9
	 * points of the OLD spectrum, alternately adding and subtracting
	 * the new spectrum to the old; then we multiply the whole thing
	 * by exp(-i pi/4).
	 */
    for (i = 0, fp1 = spect1, fp2 = x->x_lastanalysis + 2*FILTSIZE,
	fp3 = spect2 + 2*FILTSIZE;
	    i < (hop>>1); i++)
    {
	float re,  im;

	re= FILT1 * ( fp2[ -2] -fp2[ 1]  +fp3[ -2] -fp3[ 1]) +
	    FILT2 * ( fp2[ -3] -fp2[ 2]  +fp3[ -3] -fp3[ 2]) +
	    FILT3 * (-fp2[ -6] +fp2[ 5]  -fp3[ -6] +fp3[ 5]) +
	    FILT4 * (-fp2[ -7] +fp2[ 6]  -fp3[ -7] +fp3[ 6]) +
	    FILT5 * ( fp2[-10] -fp2[ 9]  +fp3[-10] -fp3[ 9]);

	im= FILT1 * ( fp2[ -1] +fp2[ 0]  +fp3[ -1] +fp3[ 0]) +
	    FILT2 * (-fp2[ -4] -fp2[ 3]  -fp3[ -4] -fp3[ 3]) +
	    FILT3 * (-fp2[ -5] -fp2[ 4]  -fp3[ -5] -fp3[ 4]) +
	    FILT4 * ( fp2[ -8] +fp2[ 7]  +fp3[ -8] +fp3[ 7]) +
	    FILT5 * ( fp2[ -9] +fp2[ 8]  +fp3[ -9] +fp3[ 8]);

	fp1[0] = 0.7071f * (re + im);
	fp1[1] = 0.7071f * (im - re);
	fp1[4] = fp2[0] + fp3[1];
	fp1[5] = fp2[1] - fp3[0];
	
	fp1 += 8, fp2 += 2, fp3 += 2;
	re= FILT1 * ( fp2[ -2] -fp2[ 1]  -fp3[ -2] +fp3[ 1]) +
	    FILT2 * ( fp2[ -3] -fp2[ 2]  -fp3[ -3] +fp3[ 2]) +
	    FILT3 * (-fp2[ -6] +fp2[ 5]  +fp3[ -6] -fp3[ 5]) +
	    FILT4 * (-fp2[ -7] +fp2[ 6]  +fp3[ -7] -fp3[ 6]) +
	    FILT5 * ( fp2[-10] -fp2[ 9]  -fp3[-10] +fp3[ 9]);

	im= FILT1 * ( fp2[ -1] +fp2[ 0]  -fp3[ -1] -fp3[ 0]) +
	    FILT2 * (-fp2[ -4] -fp2[ 3]  +fp3[ -4] +fp3[ 3]) +
	    FILT3 * (-fp2[ -5] -fp2[ 4]  +fp3[ -5] +fp3[ 4]) +
	    FILT4 * ( fp2[ -8] +fp2[ 7]  -fp3[ -8] -fp3[ 7]) +
	    FILT5 * ( fp2[ -9] +fp2[ 8]  -fp3[ -9] -fp3[ 8]);

	fp1[0] = 0.7071f * (re + im);
	fp1[1] = 0.7071f * (im - re);
	fp1[4] = fp2[0] - fp3[1];
	fp1[5] = fp2[1] + fp3[0];
	
	fp1 += 8, fp2 += 2, fp3 += 2;
    }
#if 0
    if (x->x_nprint)
    {
	for (i = 0,  fp = spect1; i < 16; i++,  fp+= 4)
	    post("spect %d %f %f --> %f", i, fp[0], fp[1],
		sqrt(fp[0] * fp[0] + fp[1] * fp[1]));
    }
#endif
	 /* copy new spectrum out */
    for (i = 0, fp1 = spect2, fp2 = x->x_lastanalysis;
	    i < n + 4*FILTSIZE; i++) *fp2++ = *fp1++;

    for (i = 0; i < MINBIN; i++) spect1[4*i + 2] = spect1[4*i + 3] = 0;
	/* starting at bin MINBIN, compute hanning windowed power spectrum */
    for (i = MINBIN, fp1 = spect1+4*MINBIN, total_power = 0;
    	i < n-2; i++,  fp1 += 4)
    {
	float re = fp1[0] - 0.5f * (fp1[-8] + fp1[8]);
	float im = fp1[1] - 0.5f * (fp1[-7] + fp1[9]);
	fp1[3] = (total_power += (fp1[2] = re * re + im * im));
    }

    if (total_power > 1e-9f)
    {
	total_db = (100.f - DBFUDGE) + LOGTODB * log(total_power/n);
	total_loudness = fsqrt(fsqrt(total_power));
	if (total_db < 0) total_db = 0;
    }
    else total_db = total_loudness = 0;
	/*  store new db in history vector */
    x->x_dbs[newphase] = total_db;
    if (total_db < x->x_amplo) goto nopow;
#if 1
    if (x->x_nprint) post("power %f", total_power);
#endif

#if CHECKER
    	/* verify that our FFT resampling thing is putting out good results */
    for (i = 0; i < hop; i++)
    {
	checker3[2*i] = fiddle_checker[i];
	checker3[2*i + 1] = 0;
	checker3[n + 2*i] = fiddle_checker[i] = x->x_inbuf[i];
	checker3[n + 2*i + 1] = 0;
    }
    for (i = 2*n; i < 4*n; i++) checker3[i] = 0;
    fft(checker3, 2*n, 0);
    if (x->x_nprint)
    {
	for (i = 0,  fp = checker3; i < 16; i++,  fp += 2)
	    post("spect %d %f %f --> %f", i, fp[0], fp[1],
		sqrt(fp[0] * fp[0] + fp[1] * fp[1]));
    }

#endif
    npeak = 0;

	 /* search for peaks */
    for (i = MINBIN, fp = spect1+4*MINBIN, pk1 = peaklist;
	i < n-2 && npeak < npeaktot; i++, fp += 4)
    {
	float height = fp[2], h1 = fp[-2], h2 = fp[6];
	float totalfreq, pfreq, f1, f2, m, var, stdev;
	
	if (height < h1 || height < h2 ||
	    h1 < 0.00001f*total_power || h2 < 0.00001f*total_power)
	    	continue;

    	    /* use an informal phase vocoder to estimate the frequency.
    	    Do this for the two adjacent bins too. */
	pfreq= ((fp[-8] - fp[8]) * (2.0f * fp[0] - fp[8] - fp[-8]) +
		(fp[-7] - fp[9]) * (2.0f * fp[1] - fp[9] - fp[-7])) /
		    (2.0f * height);
	f1=    ((fp[-12] - fp[4]) * (2.0f * fp[-4] - fp[4] - fp[-12]) +
		(fp[-11] - fp[5]) * (2.0f * fp[-3] - fp[5] - fp[-11])) /
		    (2.0f * h1) - 1;
	f2=    ((fp[-4] - fp[12]) * (2.0f * fp[4] - fp[12] - fp[-4]) +
		(fp[-3] - fp[13]) * (2.0f * fp[5] - fp[13] - fp[-3])) /
		    (2.0f * h2) + 1;

    	    /* get sample mean and variance of the three */
	m = 0.333333f * (pfreq + f1 + f2);
	var = 0.5f * ((pfreq-m)*(pfreq-m) + (f1-m)*(f1-m) + (f2-m)*(f2-m));

	totalfreq = i + m;
	if (var * total_power > KNOCKTHRESH * height || var < 1e-30)
	{
#if 0
	    if (x->x_nprint)
		post("cancel: %.2f hz, index %.1f, power %.5f, stdev=%.2f",
		    totalfreq * hzperbin, BPERO_OVER_LOG2 * log(totalfreq) - 96,
		     height, sqrt(var));
#endif
	    continue;
	}
	stdev = fsqrt(var);
	if (totalfreq < 4)
	{
	    if (x->x_nprint) post("oops: was %d,  freq %f, m %f, stdev %f h %f",
		i,  totalfreq, m, stdev, height);
	    totalfreq = 4;
	}
	pk1->p_width = stdev;

	pk1->p_pow = height;
	pk1->p_loudness = fsqrt(fsqrt(height));
	pk1->p_fp = fp;
	pk1->p_freq = totalfreq;
	npeak++;
#if 1
	if (x->x_nprint)
	{
	    post("peak: %.2f hz. index %.1f, power %.5f, stdev=%.2f",
		pk1->p_freq * hzperbin,
		BPERO_OVER_LOG2 * log(pk1->p_freq) - 96,
		 height, stdev);
	}
#endif
	pk1++;
    }

    	    /* prepare the raw peaks for output */
    for (i = 0, pk1 = peaklist, pk2 = x->x_peakbuf; i < npeak;
    	i++, pk1++, pk2++)
    {
    	float loudness = pk1->p_loudness;
    	if (i >= npeakout) break;
    	pk2->po_freq = hzperbin * pk1->p_freq;
    	pk2->po_amp = (2.f / (float)n) * (loudness * loudness);
    }
    for (; i < npeakout; i++, pk2++) pk2->po_amp = pk2->po_freq = 0;

	/* now, working back into spect2, make a sort of "liklihood"
	 * spectrum.  Proceeding in 48ths of an octave,  from 2 to
	 * n/2 (in bins), the likelihood of each pitch range is contributed
	 * to by every peak in peaklist that's an integer multiple of it
	 * in frequency.
	 */

    if (npeak > npeakanal) npeak = npeakanal; /* max # peaks to analyze */
    for (i = 0, fp1 = histogram; i < maxbin; i++) *fp1++ = 0;
    for (i = 0, pk1 = peaklist; i < npeak; i++, pk1++)
    {
	float pit = BPERO_OVER_LOG2 * flog(pk1->p_freq) - 96.0f;
	float binbandwidth = FACTORTOBINS * pk1->p_width/pk1->p_freq;
	float putbandwidth = (binbandwidth < 2 ? 2 : binbandwidth);
	float weightbandwidth = (binbandwidth < 1.0f ? 1.0f : binbandwidth);
	/* float weightamp = 1.0f + 3.0f * pk1->p_pow / pow; */
	float weightamp = 4. * pk1->p_loudness / total_loudness;
	for (j = 0, fp2 = sigfiddle_partialonset; j < NPARTIALONSET; j++, fp2++)
	{
	    float bin = pit - *fp2;
	    if (bin < maxbin)
	    {
		float para, pphase, score = 30.0f * weightamp /
		    ((j+x->x_npartial) * weightbandwidth);
		int firstbin = bin + 0.5f - 0.5f * putbandwidth;
		int lastbin = bin + 0.5f + 0.5f * putbandwidth;
		int ibw = lastbin - firstbin;
		if (firstbin < -BINGUARD) break;
		para = 1.0f / (putbandwidth * putbandwidth);
		for (k = 0, fp3 = histogram + firstbin,
		    pphase = firstbin-bin; k <= ibw;
			k++, fp3++,  pphase += 1.0f)
		{
		    *fp3 += score * (1.0f - para * pphase * pphase);
		}
	    }
	}
    }
#if 1
    if (x->x_nprint)
    {
	for (i = 0; i < 6*5; i++)
	{
	    float fhz = hzperbin * exp ((8*i + 96) * (1./BPERO_OVER_LOG2));
	    if (!(i % 6)) post("-- bin %d pitch %f freq %f----", 8*i,
	    	ftom(fhz), fhz);;
	    post("%3d %3d %3d %3d %3d %3d %3d %3d",
		(int)(histogram[8*i]),
		(int)(histogram[8*i+1]),
		(int)(histogram[8*i+2]),
		(int)(histogram[8*i+3]),
		(int)(histogram[8*i+4]),
		(int)(histogram[8*i+5]),
		(int)(histogram[8*i+6]),
		(int)(histogram[8*i+7]));
	}
    }

#endif

	/*
	 * Next we find up to NPITCH strongest peaks in the histogram.
	 * if a peak is related to a stronger one via an interval in
	 * the sigfiddle_partialonset array,  we suppress it.
	 */

    for (npitch = 0; npitch < x->x_npitch; npitch++)
    {
	int index;
	float best;
	if (npitch)
	{
	    for (best = 0, index = -1, j=1; j < maxbin-1; j++)
	    {
		if (histogram[j] > best && histogram[j] > histogram[j-1] &&
		    histogram[j] > histogram[j+1])
		{
		    for (k = 0; k < npitch; k++)
			if (histvec[k].h_index == j)
			    goto peaknogood;
		    for (k = 0; k < NPARTIALONSET; k++)
		    {
			if (j - sigfiddle_intpartialonset[k] < 0) break;
			if (histogram[j - sigfiddle_intpartialonset[k]]
			    > histogram[j]) goto peaknogood;
		    }
		    for (k = 0; k < NPARTIALONSET; k++)
		    {
			if (j + sigfiddle_intpartialonset[k] >= maxbin) break;
			if (histogram[j + sigfiddle_intpartialonset[k]]
			    > histogram[j]) goto peaknogood;
		    }
		    index = j;
		    best = histogram[j];
		}
	    peaknogood: ;
	    }
	}
	else
	{
	    for (best = 0, index = -1, j=0; j < maxbin; j++)
		if (histogram[j] > best)
		    index = j,  best = histogram[j];
	}
	if (index < 0) break;
	histvec[npitch].h_value = best;
	histvec[npitch].h_index = index;
    }
#if 1
    if (x->x_nprint)
    {
	for (i = 0; i < npitch; i++)
	{
	    post("index %d freq %f --> value %f", histvec[i].h_index,
		exp((1./BPERO_OVER_LOG2) * (histvec[i].h_index + 96)),
		histvec[i].h_value);
	    post("next %f , prev %f",
		exp((1./BPERO_OVER_LOG2) * (histvec[i].h_index + 97)),
		exp((1./BPERO_OVER_LOG2) * (histvec[i].h_index + 95)) );
	}
    }
#endif

	/* for each histogram peak, we now search back through the
	 * FFT peaks.  A peak is a pitch if either there are several
	 * harmonics that match it,  or else if (a) the fundamental is
	 * present,  and (b) the sum of the powers of the contributing peaks
	 * is at least 1/100 of the total power.
	 *
	 * A peak is a contributor if its frequency is within 25 cents of
	 * a partial from 1 to 16.
	 *
	 * Finally, we have to be at least 5 bins in frequency, which
	 * corresponds to 2-1/5 periods fitting in the analysis window.
	 */

    for (i = 0; i < npitch; i++)
    {
	float cumpow = 0, cumstrength = 0, freqnum = 0, freqden = 0;
	int npartials = 0,  nbelow8 = 0;
	    /* guessed-at frequency in bins */
	float putfreq = fexp((1.0f / BPERO_OVER_LOG2) *
	    (histvec[i].h_index + 96.0f));
	for (j = 0; j < npeak; j++)
	{
	    float fpnum = peaklist[j].p_freq/putfreq;
	    int pnum = fpnum + 0.5f;
	    float fipnum = pnum;
	    float deviation;
	    if (pnum > 16 || pnum < 1) continue;
	    deviation = 1.0f - fpnum/fipnum;
	    if (deviation > -PARTIALDEVIANCE && deviation < PARTIALDEVIANCE)
	    {
		/*
		 * we figure this is a partial since it's within 1/4 of
		 * a halftone of a multiple of the putative frequency.
		 */

		float stdev, weight;
		npartials++;
		if (pnum < 8) nbelow8++;
		cumpow += peaklist[j].p_pow;
		cumstrength += fsqrt(fsqrt(peaklist[j].p_pow));
		stdev = (peaklist[j].p_width > MINBW ?
		    peaklist[j].p_width : MINBW);
		weight = 1.0f / ((stdev*fipnum) * (stdev*fipnum));
		freqden += weight;
		freqnum += weight * peaklist[j].p_freq/fipnum;		
#if 1
		if (x->x_nprint)
		{
		    post("peak %d partial %d f=%f w=%f",
			j, pnum, peaklist[j].p_freq/fipnum, weight);
		}
#endif
	    }
#if 1
	    else if (x->x_nprint) post("peak %d partial %d dev %f",
			j, pnum, deviation);
#endif
	}
	if ((nbelow8 < 4 || npartials < 7) && cumpow < 0.01f * total_power)
	    histvec[i].h_value = 0;
	else
	{
	    float pitchpow = (cumstrength * cumstrength) *
		(cumstrength * cumstrength);
    	    float freqinbins = freqnum/freqden;
    	    	/* check for minimum output frequency */

    	    if (freqinbins < MINFREQINBINS)
    	    	histvec[i].h_value = 0;
    	    else
    	    {
    	    	    /* we passed all tests... save the values we got */
	    	histvec[i].h_pitch = ftom(hzperbin * freqnum/freqden);
	    	histvec[i].h_loud = (100.0f -DBFUDGE) +
	    	    (LOGTODB) * log(pitchpow/n);
	    }
	}
    }
#if 1
    if (x->x_nprint)
    {
	for (i = 0; i < npitch; i++)
	{
	    if (histvec[i].h_value > 0)
		post("index %d pit %f loud %f", histvec[i].h_index,
		histvec[i].h_pitch, histvec[i].h_loud);
	    else post("-- cancelled --");
	}
    }
#endif

	/* now try to find continuous pitch tracks that match the new
	 * pitches.  First mark each peak unmatched.
	 */
    for (i = 0, hp1 = histvec; i < npitch; i++, hp1++)
	hp1->h_used = 0;

	/* for each old pitch, try to match a new one to it. */
    for (i = 0, phist = x->x_hist; i < x->x_npitch; i++,  phist++)
    {
	float thispitch = phist->h_pitches[oldphase];
	phist->h_pitch = 0;	    /* no output, thanks */
	phist->h_wherefrom = 0;
	if (thispitch == 0.0f) continue;
	for (j = 0, hp1 = histvec; j < npitch; j++, hp1++)
	    if ((hp1->h_value > 0) && hp1->h_pitch > thispitch - GLISS
		&& hp1->h_pitch < thispitch + GLISS)
	{
	    phist->h_wherefrom = hp1;
	    hp1->h_used = 1;
	}
    }
    for (i = 0, hp1 = histvec; i < npitch; i++, hp1++)
	if ((hp1->h_value > 0) && !hp1->h_used)
    {
	for (j = 0, phist = x->x_hist; j < x->x_npitch; j++,  phist++)
	    if (!phist->h_wherefrom)
	{
	    phist->h_wherefrom = hp1;
	    phist->h_age = 0;
	    phist->h_noted = 0;
	    hp1->h_used = 1;
	    goto happy;
	}
	break;
    happy: ;
    }
	/* copy the pitch info into the history vector */
    for (i = 0, phist = x->x_hist; i < x->x_npitch; i++,  phist++)
    {
	if (phist->h_wherefrom)
	{
	    phist->h_amps[newphase] = phist->h_wherefrom->h_loud;
	    phist->h_pitches[newphase] =
		phist->h_wherefrom->h_pitch;
	    (phist->h_age)++;
	}
	else
	{
	    phist->h_age = 0;
	    phist->h_amps[newphase] = phist->h_pitches[newphase] = 0;
	}
    }
#if 1
    if (x->x_nprint)
    {
	post("vibrato %d %f", x->x_vibbins, x->x_vibdepth);
	for (i = 0, phist = x->x_hist; i < x->x_npitch; i++,  phist++)
	{
	    post("noted %f, age %d", phist->h_noted,  phist->h_age);
#ifndef I860
	    post("values %f %f %f %f %f",
		phist->h_pitches[newphase],
		phist->h_pitches[(newphase + HISTORY-1)%HISTORY],
		phist->h_pitches[(newphase + HISTORY-2)%HISTORY],
		phist->h_pitches[(newphase + HISTORY-3)%HISTORY],
		phist->h_pitches[(newphase + HISTORY-4)%HISTORY]);
#endif
	}
    }
#endif
	/* look for envelope attacks */

    x->x_attackvalue = 0;

    if (x->x_peaked)
    {
	if (total_db > x->x_amphi)
	{
	    int binlook = newphase - x->x_attackbins;
	    if (binlook < 0) binlook += HISTORY;
	    if (total_db > x->x_dbs[binlook] + x->x_attackthresh)
	    {
		x->x_attackvalue = 1;
		x->x_peaked = 0;
	    }
	}
    }
    else
    {
	int binlook = newphase - x->x_attackbins;
	if (binlook < 0) binlook += HISTORY;
	if (x->x_dbs[binlook] > x->x_amphi && x->x_dbs[binlook] > total_db)
	    x->x_peaked = 1;
    }

	/* for each current frequency track, test for a new note using a
	 * stability criterion.  Later perhaps we should also do as in
	 * pitch~ and check for unstable notes a posteriori when
	 * there's a new attack with no note found since the last onset;
	 * but what's an attack &/or onset when we're polyphonic?
	 */

    for (i = 0, phist = x->x_hist; i < x->x_npitch; i++,  phist++)
    {
	    /*
	     * if we've found a pitch but we've now strayed from it turn
	     * it off.
	     */
	if (phist->h_noted)
	{
	    if (phist->h_pitches[newphase] > phist->h_noted + x->x_vibdepth
		|| phist->h_pitches[newphase] < phist->h_noted - x->x_vibdepth)
		    phist->h_noted = 0;
	}
	else
	{
	    if (phist->h_wherefrom && phist->h_age >= x->x_vibbins)
	    {
		float centroid = 0;
		int not = 0;
		for (j = 0, k = newphase; j < x->x_vibbins; j++)
		{
		    centroid += phist->h_pitches[k];
		    k--;
		    if (k < 0) k = HISTORY-1;
		}
		centroid /= x->x_vibbins;
		for (j = 0, k = newphase; j < x->x_vibbins; j++)
		{
			/* calculate deviation from norm */
		    float dev = centroid - phist->h_pitches[k];
		    k--;
		    if (k < 0) k = HISTORY-1;
		    if (dev > x->x_vibdepth ||
			-dev > x->x_vibdepth) not = 1;
		}
		if (!not)
		{
		    phist->h_pitch = phist->h_noted = centroid;
		}
	    }
	}
    }
    return;

nopow:
    for (i = 0; i < x->x_npitch; i++)
    {
	x->x_hist[i].h_pitch = x->x_hist[i].h_noted =
	    x->x_hist[i].h_pitches[newphase] =
	    x->x_hist[i].h_amps[newphase] = 0;
	x->x_hist[i].h_age = 0;
    }
    x->x_peaked = 1;
    x->x_dbage = 0;
}

void sigfiddle_debug(t_sigfiddle *x)
{
    x->x_nprint = 1;
}

void sigfiddle_print(t_sigfiddle *x)
{
    post("amp-range %f %f,",  x->x_amplo, x->x_amphi);
    post("reattack %d %f,",  x->x_attacktime, x->x_attackthresh);
    post("vibrato %d %f",  x->x_vibtime, x->x_vibdepth);
    post("npartial %f",  x->x_npartial);
    post("auto %d",  x->x_auto);
}

void sigfiddle_amprange(t_sigfiddle *x, t_floatarg amplo, t_floatarg amphi)
{
    if (amplo < 0) amplo = 0;
    if (amphi < amplo) amphi = amplo + 1;
    x->x_amplo = amplo;
    x->x_amphi = amphi;
}

void sigfiddle_reattack(t_sigfiddle *x,
    t_floatarg attacktime, t_floatarg attackthresh)
{
    if (attacktime < 0) attacktime = 0;
    if (attackthresh <= 0) attackthresh = 1000;
    x->x_attacktime = attacktime;
    x->x_attackthresh = attackthresh;
    x->x_attackbins = (x->x_sr * 0.001 * attacktime) / x->x_hop;
    if (x->x_attackbins >= HISTORY) x->x_attackbins = HISTORY - 1;
}

void sigfiddle_vibrato(t_sigfiddle *x, t_floatarg vibtime, t_floatarg vibdepth)
{
    if (vibtime < 0) vibtime = 0;
    if (vibdepth <= 0) vibdepth = 1000;
    x->x_vibtime = vibtime;
    x->x_vibdepth = vibdepth;
    x->x_vibbins = (x->x_sr * 0.001  * vibtime) / x->x_hop;
    if (x->x_vibbins >= HISTORY) x->x_vibbins = HISTORY - 1;
    if (x->x_vibbins < 1) x->x_vibbins = 1;
}

void sigfiddle_npartial(t_sigfiddle *x, t_floatarg npartial)
{
    if (npartial < 0.1) npartial = 0.1;
    x->x_npartial = npartial;
}

void sigfiddle_auto(t_sigfiddle *x, t_floatarg f)
{
    x->x_auto = (f != 0);
}

int sigfiddle_doinit(t_sigfiddle *x, long npoints, long npitch,
    long npeakanal, long npeakout)
{
    float *buf1, *buf2,  *buf3;
    t_peakout *buf4;
    int i, hop;

    if (npoints < MINPOINTS || npoints > MAXPOINTS) npoints = DEFAULTPOINTS;
    npoints = 1 << sigfiddle_ilog2(npoints);
    hop = npoints>>1;
    if (!npeakanal && !npeakout) npeakanal = DEFNPEAK, npeakout = 0;
    if (!npeakanal < 0) npeakanal = 0;
    else if (npeakanal > MAXPEAK) npeakanal = MAXPEAK;
    if (!npeakout < 0) npeakout = 0;
    else if (npeakout > MAXPEAK) npeakout = MAXPEAK;
    if (npitch <= 0) npitch = 0;
    else if (npitch > MAXNPITCH) npitch = MAXNPITCH;
    if (npeakanal && !npitch) npitch = 1;


    if (!(buf1 = (float *)getbytes(sizeof(float) * hop)))
    {
	error("fiddle~: out of memory");
	return (0);
    }
    if (!(buf2 = (float *)getbytes(sizeof(float) * (npoints + 4 * FILTSIZE))))
    {
	freebytes(buf1, sizeof(float) * hop);
	error("fiddle~: out of memory");
	return (0);
    }
    if (!(buf3 = (float *)getbytes(sizeof(float) * npoints)))
    {
	freebytes(buf1, sizeof(float) * hop);
	freebytes(buf2, sizeof(float) * (npoints + 4 * FILTSIZE));
	error("fiddle~: out of memory");
	return (0);
    }
    if (!(buf4 = (t_peakout *)getbytes(sizeof(*buf4) * npeakout)))
    {
	freebytes(buf1, sizeof(float) * hop);
	freebytes(buf2, sizeof(float) * (npoints + 4 * FILTSIZE));
	freebytes(buf3, sizeof(float) * npoints);
	error("fiddle~: out of memory");
	return (0);
    }
    for (i = 0; i < hop; i++) buf1[i] = 0;
    for (i = 0; i < npoints + 4 * FILTSIZE; i++) buf2[i] = 0;
    for (i = 0; i < hop; i++)
	buf3[2*i] =    cos((3.14159*i)/(npoints)),
	buf3[2*i+1] = -sin((3.14159*i)/(npoints));
    for (i = 0; i < npeakout; i++)
	buf4[i].po_freq = buf4[i].po_amp = 0;
    x->x_inbuf = buf1;
    x->x_lastanalysis = buf2;
    x->x_spiral = buf3;
    x->x_peakbuf = buf4;

    x->x_npeakout = npeakout;
    x->x_npeakanal = npeakanal;
    x->x_phase = 0;
    x->x_histphase = 0;
    x->x_hop = npoints>>1;
    x->x_sr = 44100;		/* this and the next are filled in later */
    for (i = 0; i < MAXNPITCH; i++)
    {
	int j;
	x->x_hist[i].h_pitch = x->x_hist[i].h_noted = 0;
	x->x_hist[i].h_age = 0;
	x->x_hist[i].h_wherefrom = 0;
	x->x_hist[i].h_outlet = 0;
	for (j = 0; j < HISTORY; j++)
	    x->x_hist[i].h_amps[j] = x->x_hist[i].h_pitches[j] = 0;
    }
    x->x_nprint = 0;
    x->x_npitch = npitch;
    for (i = 0; i < HISTORY; i++) x->x_dbs[i] = 0;
    x->x_dbage = 0;
    x->x_peaked = 0;
    x->x_auto = 1;
    x->x_amplo = DEFAMPLO;
    x->x_amphi = DEFAMPHI;
    x->x_attacktime = DEFATTACKTIME;
    x->x_attackbins = 1;		/* real value calculated afterward */
    x->x_attackthresh = DEFATTACKTHRESH;
    x->x_vibtime = DEFVIBTIME;
    x->x_vibbins = 1;			/* real value calculated afterward */
    x->x_vibdepth = DEFVIBDEPTH;
    x->x_npartial = 7;
    x->x_attackvalue = 0;
    return (1);
}



#pragma mark MaxMSP glue ----------------------------
/************* Beginning of MSP Code ******************************/

#ifdef MSP

static	void *sigfiddle_class;

static t_int *fiddle_perform(t_int *w)
{
    t_float *in = (t_float *)(w[1]);
    t_sigfiddle *x = (t_sigfiddle *)(w[2]);
    int n = (int)(w[3]);
    int count,inc = x->x_downsample;
    float *fp;

    if (x->x_obj.z_disabled)
    	goto skip;
    for (count = 0, fp = x->x_inbuf + x->x_phase; count < n; count+=inc) {
    	*fp++ = *in;
    	in += inc;
    }
    if (fp == x->x_inbuf + x->x_hop)
    {
		sigfiddle_doit(x);
		x->x_phase = 0;
		if (x->x_auto) clock_delay(x->x_clock, 0L);
		if (x->x_nprint) x->x_nprint--;
    }
    else x->x_phase += n;
skip:
    return (w+4);
}

void fiddle_perform64(t_sigfiddle *x, t_object *dsp64, double **ins, long numins,
					  double **outs, long numouts, long sampleframes, long flags, void *userparam)
{
	t_double *in = ins[0];
    int n = sampleframes;
    int count, inc = x->x_downsample;
    float *fp;
	
    if (x->x_obj.z_disabled)
    	return;
	
    for (count = 0, fp = x->x_inbuf + x->x_phase; count < n; count+=inc) {
    	*fp++ = *in;
    	in += inc;
    }
    if (fp == x->x_inbuf + x->x_hop)
    {
		sigfiddle_doit(x);
		x->x_phase = 0;
		if (x->x_auto) clock_delay(x->x_clock, 0L);
		if (x->x_nprint) x->x_nprint--;
    }
    else x->x_phase += n;

}



void sigfiddle_dsp(t_sigfiddle *x, t_signal **sp)
{
	x->x_sr = sp[0]->s_sr;		/* adding for MSP2 compatibility */
     if (sp[0]->s_n > x->x_hop) {
    	x->x_downsample = sp[0]->s_n / x->x_hop;
    	post("* warning: fiddle~: will downsample input by %ld",x->x_downsample);
    	x->x_sr = sp[0]->s_sr / x->x_downsample;
    } else {
    	x->x_downsample = 1;
   		x->x_sr = sp[0]->s_sr;
   	}
	sigfiddle_reattack(x, x->x_attacktime, x->x_attackthresh);
    sigfiddle_vibrato(x, x->x_vibtime, x->x_vibdepth);
    dsp_add(fiddle_perform, 3, sp[0]->s_vec, x, sp[0]->s_n);
}

//64-bit dsp method
void sigfiddle_dsp64(t_sigfiddle *x, t_object *dsp64, short *count, double samplerate,
				 long maxvectorsize, long flags)
{
	object_method(dsp64, gensym("dsp_add64"), x, fiddle_perform64, 0, NULL);
	
	x->x_sr = samplerate;
	if(x->x_sr<=0) x->x_sr = 44100.0;
	
	if (maxvectorsize > x->x_hop) {
    	x->x_downsample = maxvectorsize / x->x_hop;
    	post("* warning: fiddle~: will downsample input by %ld",x->x_downsample);
    	x->x_sr = samplerate / x->x_downsample;
    } else {
    	x->x_downsample = 1;
   		x->x_sr = samplerate;
   	}
	sigfiddle_reattack(x, x->x_attacktime, x->x_attackthresh);
    sigfiddle_vibrato(x, x->x_vibtime, x->x_vibdepth);
}

void sigfiddle_tick(t_sigfiddle *x)	/* callback function for the clock MSP*/
{
    int i;
    t_pitchhist *ph;
    if (x->x_npeakout)
    {
    	int npeakout = x->x_npeakout;
    	t_peakout *po;
    	for (i = 0, po = x->x_peakbuf; i < npeakout; i++, po++)
    	{
	    	t_atom at[3];
	    	// SETINT(at, i+1);  // changing for Mach-O.
			atom_setlong(at, i+1);					//atom_setfloat(at, i+1);
	    	atom_setfloat(at+1, po->po_freq);
	    	atom_setfloat(at+2, po->po_amp);
	    	outlet_list(x->x_peakout, 0, 3, at);
		}
    }
    outlet_float(x->x_envout, x->x_dbs[x->x_histphase]);
    for (i = 0,  ph = x->x_hist; i < x->x_npitch; i++,  ph++)
    {
	t_atom at[2];
	atom_setfloat(at, ph->h_pitches[x->x_histphase]);
	atom_setfloat(at+1, ph->h_amps[x->x_histphase]);
	outlet_list(ph->h_outlet, 0, 2, at);
    }
    if (x->x_attackvalue) outlet_bang(x->x_attackout);
    for (i = 0,  ph = x->x_hist; i < x->x_npitch; i++,  ph++)
 	if (ph->h_pitch) outlet_float(x->x_noteout, ph->h_pitch);
}

void sigfiddle_bang(t_sigfiddle *x)			// MSP
{
    int i;
    t_pitchhist *ph;
    if (x->x_npeakout)
    {
    	int npeakout = x->x_npeakout;
    	t_peakout *po;
    	for (i = 0, po = x->x_peakbuf; i < npeakout; i++, po++)
    	{
	    t_atom at[3];
	    atom_setlong(at, i+1);
	    atom_setfloat(at+1, po->po_freq);
	    atom_setfloat(at+2, po->po_amp);
	    outlet_list(x->x_peakout, 0, 3, at);
	}
    }
    outlet_float(x->x_envout, x->x_dbs[x->x_histphase]);
    for (i = 0,  ph = x->x_hist; i < x->x_npitch; i++,  ph++)
    {
	t_atom at[2];
	atom_setfloat(at, ph->h_pitches[x->x_histphase]);
	atom_setfloat(at+1, ph->h_amps[x->x_histphase]);
	outlet_list(ph->h_outlet, 0, 2, at);
    }
    if (x->x_attackvalue) outlet_bang(x->x_attackout);
    for (i = 0,  ph = x->x_hist; i < x->x_npitch; i++,  ph++)
 	if (ph->h_pitch) outlet_float(x->x_noteout, ph->h_pitch);
}


void sigfiddle_ff(t_sigfiddle *x)		/* cleanup on free  MSP  */
{

	dsp_free((t_pxobject *)x);		// Free the object
	
    if (x->x_inbuf)
    {
    	t_freebytes(x->x_inbuf, sizeof(float) * x->x_hop);
    	t_freebytes(x->x_lastanalysis, sizeof(float) * (2*x->x_hop + 4 * FILTSIZE));
    	t_freebytes(x->x_spiral, sizeof(float) * 2*x->x_hop);
    	t_freebytes(x->x_peakbuf, sizeof(*x->x_peakbuf) * x->x_npeakout);
		if(x->x_clock)
			clock_free(x->x_clock);
    }
    
}



void *sigfiddle_new(long npoints, long npitch,	long npeakanal, long npeakout)
{
	int i;
    t_sigfiddle *x = (t_sigfiddle *)object_alloc(sigfiddle_class);
	
	if(!x) {
		object_free(x);
		x = NULL;
		return x;
	}
    
    if (!sigfiddle_doinit(x, npoints, npitch, npeakanal, npeakout))
    {
    	x->x_inbuf = 0;     /* prevent the free routine from cleaning up */
    	return (0);
    }
   // post("npeak %d, npitch %d", npeakout, npitch);
   // set up the inlets and outlets.
    dsp_setup((t_pxobject *)x, 1);	// 1 input

    x->x_clock = clock_new(x, (method)sigfiddle_tick);
	
	if (x->x_npeakout)
    	x->x_peakout = listout((t_object *)x);         // listout?
    else x->x_peakout = 0;
    x->x_envout = floatout((t_object *)x);
    for (i = 0; i < x->x_npitch; i++)
		x->x_hist[i].h_outlet = listout((t_object *)x);
	x->x_attackout = bangout((t_object *)x);
	x->x_noteout = floatout((t_object *)x);
	
	return (x);


}


int C74_EXPORT main(void)
{
	t_class *c;
	c = class_new("fiddle~", (method)sigfiddle_new,
				  (method)sigfiddle_ff, sizeof(t_sigfiddle), 0L, A_DEFLONG, A_DEFLONG, A_DEFLONG, A_DEFLONG, 0);
	
	class_addmethod(c, (method)sigfiddle_dsp, "dsp", A_CANT, 0);
	class_addmethod(c, (method)sigfiddle_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)sigfiddle_debug, 	"debug", 		0);
    class_addmethod(c, (method)sigfiddle_amprange, "amp-range",	A_FLOAT, A_FLOAT, 0);
    class_addmethod(c, (method)sigfiddle_reattack, "reattack", 	A_FLOAT, A_FLOAT, 0);
    class_addmethod(c, (method)sigfiddle_vibrato, 	"vibrato", 		A_FLOAT, A_FLOAT, 0);
    class_addmethod(c, (method)sigfiddle_npartial, "npartial", 	A_FLOAT, 0);
    class_addmethod(c, (method)sigfiddle_auto, 		"auto", A_FLOAT, 0);
    class_addmethod(c, (method)sigfiddle_print, 	"print", 0);
  	class_addmethod(c, (method)sigfiddle_assist, 	"assist", A_CANT, 0);
	class_addmethod(c, (method)sigfiddle_bang, "bang", 0);

	class_dspinit(c);
	class_register(CLASS_BOX, c);
	sigfiddle_class = c;
	
	post(fiddle_version);
	
	
	return 0;
}

void sigfiddle_assist(t_sigfiddle *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) {
		switch(a) {
			case 0: sprintf (s,"(signal) audio input"); break;
		}
	}
	else {
		switch(a) {
			case 0: sprintf (s,"(float) cooked pitch output"); break;
			case 1: sprintf (s,"(bang) bang on attack"); break;
			case 2: sprintf (s,"(list) raw pitch and amplitude"); break;
			case 3: sprintf (s,"(float) amplitude (dB)"); break;
			case 4: sprintf (s,"(list) individual sinusoidal components");
		}
		
	}
}

void msp_fft(float *buf, long np, long inv)
{
	float *src,*real,*rp,*imag,*ip;
	long i;

	// because this fft algorithm uses separate real and imaginary
	// buffers
	// we must split the real and imaginary parts into two buffers,
	// then do the opposite on output
	// a more ambitious person would either do an in-place conversion
	// or rewrite the fft algorithm

	real = rp = msp_ffttemp;
	imag = ip = real + MAXPOINTS;
	src = buf;
	for (i = 0; i < np; i++) {
		*rp++ = *src++;
		*ip++ = *src++;
	}
	if (inv)
		ifft(np,real,imag);
	else
		fft(np,real,imag);
	rp = real;
	ip = imag;
	src = buf;
	for (i = 0; i < np; i++) {
		*src++ = *rp++;
		*src++ = *ip++;
	}
}

#endif /* MSP */