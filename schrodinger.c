/*********************************************************************************/
/*                                                                               */
/*  Animation of Schrödinger equation in a planar domain                         */
/*                                                                               */
/*  N. Berglund, May 2021                                                        */
/*                                                                               */
/*  Feel free to reuse, but if doing so it would be nice to drop a               */
/*  line to nils.berglund@univ-orleans.fr - Thanks!                              */
/*                                                                               */
/*  compile with                                                                 */
/*  gcc -o schrodinger schrodinger.c                                             */
/* -L/usr/X11R6/lib -ltiff -lm -lGL -lGLU -lX11 -lXmu -lglut -O3 -fopenmp        */
/*                                                                               */
/*  To make a video, set MOVIE to 1 and create subfolder tif_schrod              */
/*  It may be possible to increase parameter PAUSE                               */
/*                                                                               */
/*  create movie using                                                           */
/*  ffmpeg -i wave.%05d.tif -vcodec libx264 wave.mp4                             */
/*                                                                               */
/*********************************************************************************/

/*********************************************************************************/
/*                                                                               */
/* NB: The algorithm used to simulate the wave equation is highly paralellizable */
/* One could make it much faster by using a GPU                                  */
/*                                                                               */
/*********************************************************************************/

#include <math.h>
#include <string.h>
#include <GL/glut.h>
#include <GL/glu.h>
#include <unistd.h>
#include <sys/types.h>
#include <tiffio.h>     /* Sam Leffler's libtiff library. */
#include <omp.h>

#define MOVIE 0         /* set to 1 to generate movie */

/* General geometrical parameters */

// #define WINWIDTH 	1280  /* window width */
#define WINWIDTH 	720  /* window width */
#define WINHEIGHT 	720   /* window height */

// #define NX 1280          /* number of grid points on x axis */
#define NX 720          /* number of grid points on x axis */
#define NY 720          /* number of grid points on y axis */
// #define NX 640          /* number of grid points on x axis */
// #define NY 360          /* number of grid points on y axis */

/* setting NX to WINWIDTH and NY to WINHEIGHT increases resolution */
/* but will multiply run time by 4                                 */

#define XMIN -2.0
#define XMAX 2.0	/* x interval */
#define YMIN -2.0
#define YMAX 2.0	/* y interval for 9/16 aspect ratio */
// #define YMIN -1.125
// #define YMAX 1.125	/* y interval for 9/16 aspect ratio */

#define JULIA_SCALE 1.0 /* scaling for Julia sets */

/* Choice of the billiard table, see list in global_pdes.c  */

#define B_DOMAIN 19      /* choice of domain shape */

#define CIRCLE_PATTERN 0    /* pattern of circles, see list in global_pdes.c */

#define P_PERCOL 0.25       /* probability of having a circle in C_RAND_PERCOL arrangement */
#define NPOISSON 300        /* number of points for Poisson C_RAND_POISSON arrangement */

#define LAMBDA 0.0	    /* parameter controlling the dimensions of domain */
#define MU 1.75	            /* parameter controlling the dimensions of domain */
#define NPOLY 6             /* number of sides of polygon */
#define APOLY 1.0           /* angle by which to turn polygon, in units of Pi/2 */ 
#define MDEPTH 3            /* depth of computation of Menger gasket */
#define MRATIO 3            /* ratio defining Menger gasket */
#define MANDELLEVEL 1000      /* iteration level for Mandelbrot set */
#define MANDELLIMIT 10.0     /* limit value for approximation of Mandelbrot set */
#define FOCI 1              /* set to 1 to draw focal points of ellipse */
#define NGRIDX 15            /* number of grid point for grid of disks */
#define NGRIDY 20           /* number of grid point for grid of disks */

#define X_SHOOTER -0.2
#define Y_SHOOTER -0.6
#define X_TARGET 0.4
#define Y_TARGET 0.7        /* shooter and target positions in laser fight */

#define ISO_XSHIFT_LEFT -1.65  
#define ISO_XSHIFT_RIGHT 0.4
#define ISO_YSHIFT_LEFT -0.05
#define ISO_YSHIFT_RIGHT -0.05 
#define ISO_SCALE 0.85           /* coordinates for isospectral billiards */

/* You can add more billiard tables by adapting the functions */
/* xy_in_billiard and draw_billiard in sub_wave.c */

/* Physical patameters of wave equation */

// #define DT 0.00000005
#define DT 0.00000001
// #define DT 0.000000005
// #define DT 0.000000005
#define HBAR 1.0

/* Boundary conditions, see list in global_pdes.c  */

#define B_COND 1

/* Parameters for length and speed of simulation */

#define NSTEPS 1400      /* number of frames of movie */
#define NVID 2000         /* number of iterations between images displayed on screen */
// #define NVID 1200         /* number of iterations between images displayed on screen */
#define NSEG 100         /* number of segments of boundary */
#define BOUNDARY_WIDTH 2    /* width of billiard boundary */

#define PAUSE 1000       /* number of frames after which to pause */
#define PSLEEP 1         /* sleep time during pause */
#define SLEEP1  1        /* initial sleeping time */
#define SLEEP2  1        /* final sleeping time */

/* For debugging purposes only */
#define FLOOR 0         /* set to 1 to limit wave amplitude to VMAX */
#define VMAX 10.0       /* max value of wave amplitude */


/* Plot type, see list in global_pdes.c  */

#define PLOT 11


/* Color schemes, see list in global_pdes.c  */

#define COLOR_PALETTE 0     /* Color palette, see list in global_pdes.c  */

#define BLACK 1          /* black background */

#define COLOR_SCHEME 1   /* choice of color scheme */

#define SCALE 1          /* set to 1 to adjust color scheme to variance of field */
#define SLOPE 1.0        /* sensitivity of color on wave amplitude */
#define ATTENUATION 0.0  /* exponential attenuation coefficient of contrast with time */

#define COLORHUE 260     /* initial hue of water color for scheme C_LUM */
#define COLORDRIFT 0.0   /* how much the color hue drifts during the whole simulation */
#define LUMMEAN 0.5      /* amplitude of luminosity variation for scheme C_LUM */
#define LUMAMP 0.3       /* amplitude of luminosity variation for scheme C_LUM */
#define HUEMEAN 150.0    /* mean value of hue for color scheme C_HUE */
#define HUEAMP -150.0      /* amplitude of variation of hue for color scheme C_HUE */

#include "global_pdes.c"
#include "sub_wave.c"

double courant2;  /* Courant parameter squared */
double dx2;       /* spatial step size squared */
double intstep;   /* integration step */
double intstep1;  /* integration step used in absorbing boundary conditions */



void init_coherent_state(double x, double y, double px, double py, double scalex, double *phi[NX], 
                         double *psi[NX], short int *xy_in[NX])
/* initialise field with coherent state of position (x,y) and momentum (px, py) */
/* phi is real part, psi is imaginary part */
{
    int i, j;
    double xy[2], dist2, module, phase, scale2;    

    scale2 = scalex*scalex;
    for (i=0; i<NX; i++)
        for (j=0; j<NY; j++)
        {
            ij_to_xy(i, j, xy);
	    xy_in[i][j] = xy_in_billiard(xy[0],xy[1]);

            if (xy_in[i][j])
            {
                dist2 = (xy[0]-x)*(xy[0]-x) + (xy[1]-y)*(xy[1]-y);
                module = exp(-dist2/scale2);
                if (module < 1.0e-15) module = 1.0e-15;
                phase = (px*(xy[0]-x) + py*(xy[1]-y))/scalex;

                phi[i][j] = module*cos(phase);
                psi[i][j] = module*sin(phase);
            }
            else
            {
                phi[i][j] = 0.0;
                psi[i][j] = 0.0;
            }
        }
}



/*********************/
/* animation part    */
/*********************/

void schrodinger_color_scheme(double phi, double psi, double scale, int time, double rgb[3])
// double phi, psi, scale, rgb[3];
// int time;
{
    double phase, amp, lum;
    
    if (PLOT == P_MODULE)
        color_scheme(COLOR_SCHEME, 2.0*module2(phi, psi)-1.0, scale, time, rgb);
    else if (PLOT == P_PHASE)
    {
        amp = module2(phi,psi);
//         if (amp < 1.0e-10) amp = 1.0e-10;
        phase = argument(phi/amp, psi/amp);
        if (phase < 0.0) phase += DPI;
        lum = (color_amplitude(amp, scale, time))*0.5;
        if (lum < 0.0) lum = 0.0;
        hsl_to_rgb(phase*360.0/DPI, 0.9, lum, rgb);
    }
    else if (PLOT == P_REAL) color_scheme(COLOR_SCHEME, phi, scale, time, rgb);
    else if (PLOT == P_IMAGINARY) color_scheme(COLOR_SCHEME, psi, scale, time, rgb);
}


void draw_wave(double *phi[NX], double *psi[NX], short int *xy_in[NX], double scale, int time)
/* draw the field */
{
    int i, j;
    double rgb[3], xy[2], x1, y1, x2, y2, amp, phase;

    glBegin(GL_QUADS);

    for (i=0; i<NX; i++)
        for (j=0; j<NY; j++)
        {
            if (xy_in[i][j])
            {
                schrodinger_color_scheme(phi[i][j],psi[i][j], scale, time, rgb);
                    
                glColor3f(rgb[0], rgb[1], rgb[2]);

                glVertex2i(i, j);
                glVertex2i(i+1, j);
                glVertex2i(i+1, j+1);
                glVertex2i(i, j+1);
            }
        }

    glEnd ();
}

void evolve_wave_half(double *phi_in[NX], double *psi_in[NX], double *phi_out[NX], double *psi_out[NX], 
                      short int *xy_in[NX])
// void evolve_wave_half(phi_in, psi_in, phi_out, psi_out, xy_in)
// /* time step of field evolution */
// /* phi is real part, psi is imaginary part */
//     double *phi_in[NX], *psi_in[NX], *phi_out[NX], *psi_out[NX]; short int *xy_in[NX];
{
    int i, j, iplus, iminus, jplus, jminus;
    double delta1, delta2, x, y;
    
    #pragma omp parallel for private(i,j,iplus,iminus,jplus,jminus,delta1,delta2,x,y)
    for (i=0; i<NX; i++){
        for (j=0; j<NY; j++){
            if (xy_in[i][j]){
                /* discretized Laplacian depending on boundary conditions */
                if ((B_COND == BC_DIRICHLET)||(B_COND == BC_ABSORBING))
                {
                    iplus = (i+1);   if (iplus == NX) iplus = NX-1;
                    iminus = (i-1);  if (iminus == -1) iminus = 0;
                    jplus = (j+1);   if (jplus == NY) jplus = NY-1;
                    jminus = (j-1);  if (jminus == -1) jminus = 0;
                }
                else if (B_COND == BC_PERIODIC)
                {
                    iplus = (i+1) % NX;
                    iminus = (i-1) % NX;
                    if (iminus < 0) iminus += NX;
                    jplus = (j+1) % NY;
                    jminus = (j-1) % NY;
                    if (jminus < 0) jminus += NY;
                }
                
                delta1 = phi_in[iplus][j] + phi_in[iminus][j] + phi_in[i][jplus] + phi_in[i][jminus] - 4.0*phi_in[i][j];
                delta2 = psi_in[iplus][j] + psi_in[iminus][j] + psi_in[i][jplus] + psi_in[i][jminus] - 4.0*psi_in[i][j];

                x = phi_in[i][j];
		y = psi_in[i][j];

                /* evolve phi and psi */
                if (B_COND != BC_ABSORBING)
                {
                    phi_out[i][j] = x - intstep*delta2;
                    psi_out[i][j] = y + intstep*delta1;
                }
                else        /* case of absorbing b.c. - this is only an approximation of correct way of implementing */
                {
                    /* in the bulk */
                    if ((i>0)&&(i<NX-1)&&(j>0)&&(j<NY-1))
                    {
                        phi_out[i][j] = x - intstep*delta2;
                        psi_out[i][j] = y + intstep*delta1;
                    }
                     /* right border */
                    else if (i==NX-1) 
                    {
                        phi_out[i][j] = x - intstep1*(y - psi_in[i-1][j]);
                        psi_out[i][j] = y + intstep1*(x - phi_in[i-1][j]);
                    }
                    /* upper border */
                    else if (j==NY-1) 
                    {
                        phi_out[i][j] = x - intstep1*(y - psi_in[i][j-1]);
                        psi_out[i][j] = y + intstep1*(x - phi_in[i][j-1]);
                    }
                    /* left border */
                    else if (i==0) 
                    {
                        phi_out[i][j] = x - intstep1*(y - psi_in[1][j]);
                        psi_out[i][j] = y + intstep1*(x - phi_in[1][j]);
                    }
                   /* lower border */
                    else if (j==0) 
                    {
                        phi_out[i][j] = x - intstep1*(y - psi_in[i][1]);
                        psi_out[i][j] = y + intstep1*(x - phi_in[i][1]);
                    }
                }


                if (FLOOR)
                {
                    if (phi_out[i][j] > VMAX) phi_out[i][j] = VMAX;
                    if (phi_out[i][j] < -VMAX) phi_out[i][j] = -VMAX;
                    if (psi_out[i][j] > VMAX) psi_out[i][j] = VMAX;
                    if (psi_out[i][j] < -VMAX) psi_out[i][j] = -VMAX;
                }
            }
        }
    }
    
//     printf("phi(0,0) = %.3lg, psi(0,0) = %.3lg\n", phi[NX/2][NY/2], psi[NX/2][NY/2]);
}

void evolve_wave(double *phi[NX], double *psi[NX], double *phi_tmp[NX], double *psi_tmp[NX], short int *xy_in[NX])
/* time step of field evolution */
/* phi is real part, psi is imaginary part */
{
    evolve_wave_half(phi, psi, phi_tmp, psi_tmp, xy_in);
    evolve_wave_half(phi_tmp, psi_tmp, phi, psi, xy_in);
}


double compute_variance(double *phi[NX], double *psi[NX], short int *xy_in[NX])
// double compute_variance(phi, psi, xy_in)
/* compute the variance (total probability) of the field */
// double *phi[NX], *psi[NX]; short int * xy_in[NX];
{
    int i, j, n = 0;
    double variance = 0.0;

    for (i=1; i<NX; i++)
        for (j=1; j<NY; j++)
        {
            if (xy_in[i][j])
            {
                n++;
                variance += phi[i][j]*phi[i][j] + psi[i][j]*psi[i][j];
            }
        }
    if (n==0) n=1;
    return(variance/(double)n);
}

void renormalise_field(double *phi[NX], double *psi[NX], short int *xy_in[NX], double variance)
/* renormalise variance of field */
{
    int i, j;
    double stdv;
    
    stdv = sqrt(variance);

    for (i=1; i<NX; i++)
        for (j=1; j<NY; j++)
        {
            if (xy_in[i][j])
            {
                phi[i][j] = phi[i][j]/stdv;
                psi[i][j] = psi[i][j]/stdv;
            }
        }
}


void animation()
{
    double time, scale, dx, var;
    double *phi[NX], *psi[NX], *phi_tmp[NX], *psi_tmp[NX];
    short int *xy_in[NX];
    int i, j, s;

    /* Since NX and NY are big, it seemed wiser to use some memory allocation here */
    for (i=0; i<NX; i++)
    {
        phi[i] = (double *)malloc(NY*sizeof(double));
        psi[i] = (double *)malloc(NY*sizeof(double));
        phi_tmp[i] = (double *)malloc(NY*sizeof(double));
        psi_tmp[i] = (double *)malloc(NY*sizeof(double));
        xy_in[i] = (short int *)malloc(NY*sizeof(short int));
    }

    dx = (XMAX-XMIN)/((double)NX);
    intstep = DT/(dx*dx*HBAR);
    intstep1 = DT/(dx*HBAR);
    
    printf("Integration step %.3lg\n", intstep);

    /* initialize wave wave function */
    init_coherent_state(0.5, 0.0, 40.0, 0.0, 0.25, phi, psi, xy_in);
//     init_coherent_state(0.0, 0.0, 0.0, 5.0, 0.03, phi, psi, xy_in);
//     init_coherent_state(-0.5, 0.0, 1.0, 1.0, 0.05, phi, psi, xy_in);
    
    
    
    if (SCALE)
    {
        var = compute_variance(phi,psi, xy_in);
        scale = sqrt(1.0 + var);
        renormalise_field(phi, psi, xy_in, var);
    }
    
    

    blank();
    glColor3f(0.0, 0.0, 0.0);

    glutSwapBuffers();



    sleep(SLEEP1);

    for (i=0; i<=NSTEPS; i++)
    {
        /* compute the variance of the field to adjust color scheme */
        /* the color depends on the field divided by sqrt(1 + variance) */
        if (SCALE)
        {
            var = compute_variance(phi,psi, xy_in);
            scale = sqrt(1.0 + var);
//             printf("Norm: %5lg\t Scaling factor: %5lg\n", var, scale);
            renormalise_field(phi, psi, xy_in, var);
        }
        else scale = 1.0;

        draw_wave(phi, psi, xy_in, scale, i);
        
//         printf("Wave drawn\n");
        
        for (j=0; j<NVID; j++) evolve_wave(phi, psi, phi_tmp, psi_tmp, xy_in);
        
        draw_billiard();
        
        glutSwapBuffers();

	if (MOVIE)
        {
            save_frame();

            /* it seems that saving too many files too fast can cause trouble with the file system */
            /* so this is to make a pause from time to time - parameter PAUSE may need adjusting   */
            if (i % PAUSE == PAUSE - 1)
            {
                printf("Making a short pause\n");
                sleep(PSLEEP);
                s = system("mv wave*.tif tif_schrod/");
            }
        }

    }

//     if (MOVIE)
//     {
//         for (i=0; i<20; i++) save_frame();
//         s = system("mv wave*.tif tif_schrod/");
//     }
    for (i=0; i<NX; i++)
    {
        free(phi[i]);
        free(psi[i]);
        free(phi_tmp[i]);
        free(psi_tmp[i]);
        free(xy_in[i]);
    }

}


void display(void)
{
    glPushMatrix();

    blank();
    glutSwapBuffers();
    blank();
    glutSwapBuffers();

    animation();
    sleep(SLEEP2);

    glPopMatrix();

    glutDestroyWindow(glutGetWindow());

}


int main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(WINWIDTH,WINHEIGHT);
    glutCreateWindow("Schrodinger equation in a planar domain");

    init();

    glutDisplayFunc(display);

    glutMainLoop();

    return 0;
}

