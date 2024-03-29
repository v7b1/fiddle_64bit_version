#define GOOD_TRIG
// MSP removing this file cause I dont have it.
#include "trigtbl.h"
char fht_version[] = "Brcwl-Hrtly-Ron-dbld";

#include "fft_mayer.proto.h"

void debug_printf(char *s, ...);

#define SQRT2_2   0.70710678118654752440084436210484
#define SQRT2   2*0.70710678118654752440084436210484

void fht(float *fz, int n)
{
    int i,k,k1,k2,k3,k4,kx;
    float /* REAL */ *fi,*fn,*gi;
    TRIG_VARS;

    for (k1=1,k2=0;k1<n;k1++) {
        REAL a;
        for (k=n>>1; (!((k2^=k)&k)); k>>=1);
        if (k1>k2) {
            a=fz[k1];
            fz[k1]=fz[k2];
            fz[k2]=a;
        }
    }
    for ( k=0 ; (1<<k)<n ; k++ );
    k  &= 1;
    if (k==0) {
        for (fi=fz,fn=fz+n;fi<fn;fi+=4) {
            REAL f0,f1,f2,f3;
            f1     = fi[0 ]-fi[1 ];
            f0     = fi[0 ]+fi[1 ];
            f3     = fi[2 ]-fi[3 ];
            f2     = fi[2 ]+fi[3 ];
            fi[2 ] = (f0-f2);
            fi[0 ] = (f0+f2);
            fi[3 ] = (f1-f3);
            fi[1 ] = (f1+f3);
        }
    } else {
        for (fi=fz,fn=fz+n,gi=fi+1;fi<fn;fi+=8,gi+=8) {
            REAL s1,c1,s2,c2,s3,c3,s4,c4,g0,f0,f1,g1,f2,g2,f3,g3;
            c1     = fi[0 ] - gi[0 ];
            s1     = fi[0 ] + gi[0 ];
            c2     = fi[2 ] - gi[2 ];
            s2     = fi[2 ] + gi[2 ];
            c3     = fi[4 ] - gi[4 ];
            s3     = fi[4 ] + gi[4 ];
            c4     = fi[6 ] - gi[6 ];
            s4     = fi[6 ] + gi[6 ];
            f1     = (s1 - s2);
            f0     = (s1 + s2);
            g1     = (c1 - c2);
            g0     = (c1 + c2);
            f3     = (s3 - s4);
            f2     = (s3 + s4);
            g3     = SQRT2*c4;
            g2     = SQRT2*c3;
            fi[4 ] = f0 - f2;
            fi[0 ] = f0 + f2;
            fi[6 ] = f1 - f3;
            fi[2 ] = f1 + f3;
            gi[4 ] = g0 - g2;
            gi[0 ] = g0 + g2;
            gi[6 ] = g1 - g3;
            gi[2 ] = g1 + g3;
        }
    }
    if (n<16)
        return;

    do {
        double /* REAL */ s1,c1;
        k  += 2;
        k1  = 1  << k;
        k2  = k1 << 1;
        k4  = k2 << 1;
        k3  = k2 + k1;
        kx  = k1 >> 1;
        fi  = fz;
        gi  = fi + kx;
        fn  = fz + n;
        do {
            REAL g0,f0,f1,g1,f2,g2,f3,g3;
            f1      = fi[0 ] - fi[k1];
            f0      = fi[0 ] + fi[k1];
            f3      = fi[k2] - fi[k3];
            f2      = fi[k2] + fi[k3];
            fi[k2]  = f0      - f2;
            fi[0 ]  = f0      + f2;
            fi[k3]  = f1      - f3;
            fi[k1]  = f1      + f3;
            g1      = gi[0 ] - gi[k1];
            g0      = gi[0 ] + gi[k1];
            g3      = SQRT2  * gi[k3];
            g2      = SQRT2  * gi[k2];
            gi[k2]  = g0      - g2;
            gi[0 ]  = g0      + g2;
            gi[k3]  = g1      - g3;
            gi[k1]  = g1      + g3;
            gi     += k4;
            fi     += k4;
        } while (fi<fn);
        TRIG_INIT(k,c1,s1);
        for (i=1;i<kx;i++) {
            double /* REAL */ c2,s2;
            TRIG_NEXT(k,c1,s1);
            c2 = c1*c1 - s1*s1;
            s2 = 2*(c1*s1);
            fn = fz + n;
            fi = fz +i;
            gi = fz +k1-i;
            do {
                double /* REAL */ a,b,g0,f0,f1,g1,f2,g2,f3,g3;
                b       = s2*fi[k1] - c2*gi[k1];
                a       = c2*fi[k1] + s2*gi[k1];
                f1      = fi[0 ]    - a;
                f0      = fi[0 ]    + a;
                g1      = gi[0 ]    - b;
                g0      = gi[0 ]    + b;
                b       = s2*fi[k3] - c2*gi[k3];
                a       = c2*fi[k3] + s2*gi[k3];
                f3      = fi[k2]    - a;
                f2      = fi[k2]    + a;
                g3      = gi[k2]    - b;
                g2      = gi[k2]    + b;
                b       = s1*f2     - c1*g3;
                a       = c1*f2     + s1*g3;
                fi[k2]  = f0        - a;
                fi[0 ]  = f0        + a;
                gi[k3]  = g1        - b;
                gi[k1]  = g1        + b;
                b       = c1*g2     - s1*f3;
                a       = s1*g2     + c1*f3;
                gi[k2]  = g0        - a;
                gi[0 ]  = g0        + a;
                fi[k3]  = f1        - b;
                fi[k1]  = f1        + b;
                gi     += k4;
                fi     += k4;
            } while (fi<fn);
        }
        TRIG_RESET(k,c1,s1);
    } while (k4<n);
}

void ifft(int n, float *real, float *imag)
{
    double a,b,c,d;
    double q,r,s,t;
    int i,j,k;
    fht(real,n);
    fht(imag,n);
    for (i=1,j=n-1,k=n/2;i<k;i++,j--) {
        a = real[i]; b = real[j];  q=a+b; r=a-b;
        c = imag[i]; d = imag[j];  s=c+d; t=c-d;
        imag[i] = (s+r)*0.5;  imag[j] = (s-r)*0.5;
        real[i] = (q-t)*0.5;  real[j] = (q+t)*0.5;
    }
}

void realfft(int n, float *real)
{
    double a,b;
    int i,j,k;
    
    fht(real,n);
    for (i=1,j=n-1,k=n/2;i<k;i++,j--) {
        a = real[i];
        b = real[j];
        real[j] = (a-b)*0.5;
        real[i] = (a+b)*0.5;
    }
}

void fft(int n, float *real, float *imag)
{
    double a,b,c,d;
    double q,r,s,t;
    int i,j,k;

#ifdef DBG
    debug_printf("fft real %.2f %.2f",
        real[0],real[1]);

    debug_printf("fft imag %.2f %.2f",
        imag[0],imag[1]);
#endif
    for (i=1,j=n-1,k=n/2;i<k;i++,j--) {
        a = real[i]; b = real[j];  q=a+b; r=a-b;
        c = imag[i]; d = imag[j];  s=c+d; t=c-d;
        real[i] = (q+t)*.5; real[j] = (q-t)*.5;
        imag[i] = (s-r)*.5; imag[j] = (s+r)*.5;
    }
    fht(real,n);
    fht(imag,n);
#ifdef DBG
    debug_printf("fft after real %.2f %.2f",
        real[0],real[1]);

    debug_printf("fft after imag %.2f %.2f",
        imag[0],imag[1]);
#endif
}

void realifft(int n, float *real)
{
    double a,b;
    int i,j,k;
    for (i=1,j=n-1,k=n/2;i<k;i++,j--) {
        a = real[i];
        b = real[j];
        real[j] = (a-b);
        real[i] = (a+b);
    }
    fht(real,n);
}
