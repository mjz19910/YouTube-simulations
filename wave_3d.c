/*********************************************************************************/
/*                                                                               */
/*  Animation of wave equation in a planar domain                                */
/*                                                                               */
/*  N. Berglund, december 2012, may  2021                                        */
/*                                                                               */
/*  UPDATE 24/04: distinction between damping and "elasticity" parameters        */
/*  UPDATE 27/04: new billiard shapes, bug in color scheme fixed                 */
/*  UPDATE 28/04: code made more efficient, with help of Marco Mancini           */
/*                                                                               */
/*  Feel free to reuse, but if doing so it would be nice to drop a               */
/*  line to nils.berglund@univ-orleans.fr - Thanks!                              */
/*                                                                               */
/*  compile with                                                                 */
/*  gcc -o wave_billiard wave_billiard.c                                         */
/* -L/usr/X11R6/lib -ltiff -lm -lGL -lGLU -lX11 -lXmu -lglut -O3 -fopenmp        */
/*                                                                               */
/*  OMP acceleration may be more effective after executing                       */
/*  export OMP_NUM_THREADS=2 in the shell before running the program             */
/*                                                                               */
/*  To make a video, set MOVIE to 1 and create subfolder tif_wave                */
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
#define DOUBLE_MOVIE 0  /* set to 1 to produce movies for wave height and energy simultaneously */

/* General geometrical parameters */

/* uncomment for higher resolution */
// #define WINWIDTH 	1920  /* window width */
// #define WINHEIGHT 	1000  /* window height */
// #define NX 1920          /* number of grid points on x axis */
// #define NY 1000          /* number of grid points on y axis */
// // #define NX 3840          /* number of grid points on x axis */
// // #define NY 2000          /* number of grid points on y axis */
// 
// #define XMIN -2.0
// #define XMAX 2.0	/* x interval  */
// #define YMIN -1.041666667
// #define YMAX 1.041666667	/* y interval for 9/16 aspect ratio */

#define HIGHRES 0        /* set to 1 if resolution of grid is double that of displayed image */

/* comment out for higher resolution */
#define WINWIDTH 	1280  /* window width */
#define WINHEIGHT 	720   /* window height */

#define NX 1280          /* number of grid points on x axis */
#define NY 720          /* number of grid points on y axis */

#define XMIN -2.0
#define XMAX 2.0	/* x interval  */
#define YMIN -1.125
#define YMAX 1.125	/* y interval for 9/16 aspect ratio */

#define JULIA_SCALE 0.8 /* scaling for Julia sets */

/* Choice of the billiard table */

#define B_DOMAIN 16        /* choice of domain shape, see list in global_pdes.c */

#define CIRCLE_PATTERN 201   /* pattern of circles or polygons, see list in global_pdes.c */

#define P_PERCOL 0.25       /* probability of having a circle in C_RAND_PERCOL arrangement */
#define NPOISSON 300        /* number of points for Poisson C_RAND_POISSON arrangement */
#define RANDOM_POLY_ANGLE 1 /* set to 1 to randomize angle of polygons */

#define LAMBDA 0.6	    /* parameter controlling the dimensions of domain */
#define MU 0.6              /* parameter controlling the dimensions of domain */
#define NPOLY 6             /* number of sides of polygon */
#define APOLY 0.0           /* angle by which to turn polygon, in units of Pi/2 */ 
#define MDEPTH 3            /* depth of computation of Menger gasket */
#define MRATIO 3            /* ratio defining Menger gasket */
#define MANDELLEVEL 1000    /* iteration level for Mandelbrot set */
#define MANDELLIMIT 10.0    /* limit value for approximation of Mandelbrot set */
#define FOCI 1              /* set to 1 to draw focal points of ellipse */
#define NGRIDX 36           /* number of grid point for grid of disks */
#define NGRIDY 6           /* number of grid point for grid of disks */

#define X_SHOOTER -0.2
#define Y_SHOOTER -0.6
#define X_TARGET 0.4
#define Y_TARGET 0.7        /* shooter and target positions in laser fight */

#define ISO_XSHIFT_LEFT -2.9
#define ISO_XSHIFT_RIGHT 1.4
#define ISO_YSHIFT_LEFT -0.15
#define ISO_YSHIFT_RIGHT -0.15 
#define ISO_SCALE 0.5           /* coordinates for isospectral billiards */


/* You can add more billiard tables by adapting the functions */
/* xy_in_billiard and draw_billiard below */

/* Physical parameters of wave equation */

// #define TWOSPEEDS 0          /* set to 1 to replace hardcore boundary by medium with different speed */
#define TWOSPEEDS 0          /* set to 1 to replace hardcore boundary by medium with different speed */
#define OSCILLATE_LEFT 0     /* set to 1 to add oscilating boundary condition on the left */
#define OSCILLATE_TOPBOT 0   /* set to 1 to enforce a planar wave on top and bottom boundary */

#define OMEGA 0.005        /* frequency of periodic excitation */
#define AMPLITUDE 0.8      /* amplitude of periodic excitation */ 
#define COURANT 0.06       /* Courant number */
#define COURANTB 0.03      /* Courant number in medium B */
// #define COURANTB 0.016363636     /* Courant number in medium B */
#define GAMMA 0.0          /* damping factor in wave equation */
#define GAMMAB 1.0e-7        /* damping factor in wave equation */
#define GAMMA_SIDES 1.0e-4      /* damping factor on boundary */
#define GAMMA_TOPBOT 1.0e-7     /* damping factor on boundary */
#define KAPPA 0.0           /* "elasticity" term enforcing oscillations */
#define KAPPA_SIDES 5.0e-4  /* "elasticity" term on absorbing boundary */
#define KAPPA_TOPBOT 0.0    /* "elasticity" term on absorbing boundary */
/* The Courant number is given by c*DT/DX, where DT is the time step and DX the lattice spacing */
/* The physical damping coefficient is given by GAMMA/(DT)^2 */
/* Increasing COURANT speeds up the simulation, but decreases accuracy */
/* For similar wave forms, COURANT^2*GAMMA should be kept constant */

#define ADD_OSCILLATING_SOURCE 0        /* set to 1 to add an oscillating wave source */
#define OSCILLATING_SOURCE_PERIOD 30    /* period of oscillating source */
// #define OSCILLATING_SOURCE_PERIOD 14    /* period of oscillating source */

/* Boundary conditions, see list in global_pdes.c  */

#define B_COND 2
// #define B_COND 2

/* Parameters for length and speed of simulation */

#define NSTEPS 2500        /* number of frames of movie */
#define NVID 10           /* number of iterations between images displayed on screen */
#define NSEG 1000         /* number of segments of boundary */
#define INITIAL_TIME 0      /* time after which to start saving frames */
#define BOUNDARY_WIDTH 3    /* width of billiard boundary */

#define PAUSE 200       /* number of frames after which to pause */
#define PSLEEP 2         /* sleep time during pause */
#define SLEEP1  1        /* initial sleeping time */
#define SLEEP2  1        /* final sleeping time */
#define MID_FRAMES 200    /* number of still frames between parts of two-part movie */
#define END_FRAMES 100   /* number of still frames at end of movie */
#define FADE 1           /* set to 1 to fade at end of movie */

/* Parameters of initial condition */

#define INITIAL_AMP 0.5         /* amplitude of initial condition */
#define INITIAL_VARIANCE 0.0005  /* variance of initial condition */
#define INITIAL_WAVELENGTH  0.1  /* wavelength of initial condition */

/* Plot type, see list in global_pdes.c  */

#define ZPLOT 103     /* wave height */
#define CPLOT 103     /* color scheme */

#define ZPLOT_B 104        
#define CPLOT_B 104        /* plot type for second movie */


#define AMPLITUDE_HIGH_RES 1    /* set to 1 to increase resolution of plot */
#define SHADE_3D 1              /* set to 1 to change luminosity according to normal vector */
#define NON_DIRICHLET_BC 0      /* set to 1 to draw only facets in domain, if field is not zero on boundary */
#define DRAW_BILLIARD 1         /* set to 1 to draw boundary */
#define DRAW_BILLIARD_FRONT 1   /* set to 1 to draw front of boundary after drawing wave */
#define FADE_IN_OBSTACLE 1      /* set to 1 to fade color inside obstacles */

#define PLOT_SCALE_ENERGY 0.05      /* vertical scaling in energy plot */
#define PLOT_SCALE_LOG_ENERGY 0.6      /* vertical scaling in log energy plot */

/* 3D representation */

#define REPRESENTATION_3D 1     /* choice of 3D representation */ 

#define REP_AXO_3D 0        /* linear projection (axonometry) */
#define REP_PROJ_3D 1       /* projection on plane orthogonal to observer line of sight */


/* Color schemes */

#define COLOR_PALETTE 14    /* Color palette, see list in global_pdes.c  */
#define COLOR_PALETTE_B 11     /* Color palette, see list in global_pdes.c  */

#define BLACK 1          /* background */

#define COLOR_SCHEME 3   /* choice of color scheme, see list in global_pdes.c  */

#define SCALE 0          /* set to 1 to adjust color scheme to variance of field */
#define SLOPE 1.0       /* sensitivity of color on wave amplitude */
#define VSCALE_AMPLITUDE 0.2     /* additional scaling factor for color scheme P_3D_AMPLITUDE */
#define VSCALE_ENERGY 0.35       /* additional scaling factor for color scheme P_3D_ENERGY */
#define PHASE_FACTOR 20.0       /* factor in computation of phase in color scheme P_3D_PHASE */
#define PHASE_SHIFT 0.0      /* shift of phase in color scheme P_3D_PHASE */
#define ATTENUATION 0.0  /* exponential attenuation coefficient of contrast with time */
#define E_SCALE 200.0     /* scaling factor for energy representation */
#define LOG_SCALE 1.0     /* scaling factor for energy log representation */
#define LOG_SHIFT 1.0     /* shift of colors on log scale */
#define RESCALE_COLOR_IN_CENTER 0   /* set to 1 to decrease color intentiy in the center (for wave escaping ring) */

#define COLORHUE 260     /* initial hue of water color for scheme C_LUM */
#define COLORDRIFT 0.0   /* how much the color hue drifts during the whole simulation */
#define LUMMEAN 0.5      /* amplitude of luminosity variation for scheme C_LUM */
#define LUMAMP 0.3       /* amplitude of luminosity variation for scheme C_LUM */
#define HUEMEAN 240.0    /* mean value of hue for color scheme C_HUE */
#define HUEAMP -200.0      /* amplitude of variation of hue for color scheme C_HUE */

#define DRAW_COLOR_SCHEME 0     /* set to 1 to plot the color scheme */
#define COLORBAR_RANGE 3.0     /* scale of color scheme bar */
#define COLORBAR_RANGE_B 5.0    /* scale of color scheme bar for 2nd part */
#define ROTATE_COLOR_SCHEME 0   /* set to 1 to draw color scheme horizontally */

#define SAVE_TIME_SERIES 0      /* set to 1 to save wave time series at a point */

/* For debugging purposes only */
#define FLOOR 0         /* set to 1 to limit wave amplitude to VMAX */
#define VMAX 10.0       /* max value of wave amplitude */

/* Parameters controlling 3D projection */

double u_3d[2] = {0.75, -0.45};     /* projections of basis vectors for REP_AXO_3D representation */
double v_3d[2] = {-0.75, -0.45};
double w_3d[2] = {0.0, 0.015};
double light[3] = {0.816496581, -0.40824829, 0.40824829};      /* vector of "light" direction for P_3D_ANGLE color scheme */
double observer[3] = {10.0, 6.0, 8.5};    /* location of observer for REP_PROJ_3D representation */ 

#define Z_SCALING_FACTOR 0.018     /* overall scaling factor of z axis for REP_PROJ_3D representation */
#define XY_SCALING_FACTOR 3.75     /* overall scaling factor for on-screen (x,y) coordinates after projection */
#define ZMAX_FACTOR 1.0           /* max value of z coordinate for REP_PROJ_3D representation */
#define XSHIFT_3D 0.0             /* overall x shift for REP_PROJ_3D representation */
#define YSHIFT_3D 0.0             /* overall y shift for REP_PROJ_3D representation */


#include "global_pdes.c"        /* constants and global variables */
#include "sub_wave.c"           /* common functions for wave_billiard, heat and schrodinger */
#include "wave_common.c"        /* common functions for wave_billiard, wave_comparison, etc */

#include "global_3d.c"          /* constants and global variables */
#include "sub_wave_3d.c"        /* graphical functions specific to wave_3d */

FILE *time_series_left, *time_series_right;

double courant2, courantb2;  /* Courant parameters squared */


void evolve_wave_half(double phi_in[NX*NY], double psi_in[NX*NY], double phi_out[NX*NY], double psi_out[NX*NY], 
                      short int xy_in[NX*NY], double tc[NX*NY], double tcc[NX*NY], double tgamma[NX*NY])
// void evolve_wave_half(double *phi_in, double *psi_in, double *phi_out, double *psi_out, 
//                       short int *xy_in[NX])
/* time step of field evolution */
/* phi is value of field at time t, psi at time t-1 */
/* this version of the function has been rewritten in order to minimize the number of if-branches */
{
    int i, j, iplus, iminus, jplus, jminus;
    double delta, x, y, c, cc, gamma;
    static long time = 0;
//     static double tc[NX*NY], tcc[NX*NY], tgamma[NX*NY];
//     static short int first = 1;
    
    time++;
    
    #pragma omp parallel for private(i,j,iplus,iminus,jplus,jminus,delta,x,y)
    /* evolution in the bulk */
    for (i=1; i<NX-1; i++){
        for (j=1; j<NY-1; j++){
            if ((TWOSPEEDS)||(xy_in[i*NY+j] != 0)){
                x = phi_in[i*NY+j];
		y = psi_in[i*NY+j];
                
                /* discretized Laplacian */
                delta = phi_in[(i+1)*NY+j] + phi_in[(i-1)*NY+j] + phi_in[i*NY+j+1] + phi_in[i*NY+j-1] - 4.0*x;

                /* evolve phi */
                phi_out[i*NY+j] = -y + 2*x + tcc[i*NY+j]*delta - KAPPA*x - tgamma[i*NY+j]*(x-y);
                psi_out[i*NY+j] = x;
            }
        }
    }
    
    /* left boundary */
    if (OSCILLATE_LEFT) for (j=1; j<NY-1; j++) phi_out[j] = AMPLITUDE*cos((double)time*OMEGA);
    else for (j=1; j<NY-1; j++){
        if ((TWOSPEEDS)||(xy_in[j] != 0)){
            x = phi_in[j];
            y = psi_in[j];
                    
            switch (B_COND) {
                case (BC_DIRICHLET):
                {
                    delta = phi_in[NY+j] + phi_in[j+1] + phi_in[j-1] - 3.0*x;
                    phi_out[j] = -y + 2*x + tcc[j]*delta - KAPPA*x - tgamma[j]*(x-y);
                    break;
                }
                case (BC_PERIODIC):
                {
                    delta = phi_in[NY+j] + phi_in[(NX-1)*NY+j] + phi_in[j+1] + phi_in[j-1] - 4.0*x;
                    phi_out[j] = -y + 2*x + tcc[j]*delta - KAPPA*x - tgamma[j]*(x-y);
                    break;
                }
                case (BC_ABSORBING):
                {
                    delta = phi_in[NY+j] + phi_in[j+1] + phi_in[j-1] - 3.0*x;
                    phi_out[j] = x - tc[j]*(x - phi_in[NY+j]) - KAPPA_SIDES*x - GAMMA_SIDES*(x-y);
                    break;
                }
                case (BC_VPER_HABS):
                {
                    delta = phi_in[NY+j] + phi_in[j+1] + phi_in[j-1] - 3.0*x;
                    phi_out[j] = x - tc[j]*(x - phi_in[NY+j]) - KAPPA_SIDES*x - GAMMA_SIDES*(x-y);
                    break;
                }
            }
            psi_out[j] = x;
        }
    }
    
    /* right boundary */
    for (j=1; j<NY-1; j++){
        if ((TWOSPEEDS)||(xy_in[(NX-1)*NY+j] != 0)){
            x = phi_in[(NX-1)*NY+j];
            y = psi_in[(NX-1)*NY+j];
                    
            switch (B_COND) {
                case (BC_DIRICHLET):
                {
                    delta = phi_in[(NX-2)*NY+j] + phi_in[(NX-1)*NY+j+1] + phi_in[(NX-1)*NY+j-1] - 3.0*x;
                    phi_out[(NX-1)*NY+j] = -y + 2*x + tcc[(NX-1)*NY+j]*delta - KAPPA*x - tgamma[(NX-1)*NY+j]*(x-y);
                    break;
                }
                case (BC_PERIODIC):
                {
                    delta = phi_in[(NX-2)*NY+j] + phi_in[j] + phi_in[(NX-1)*NY+j+1] + phi_in[(NX-1)*NY+j-1] - 4.0*x;
                    phi_out[(NX-1)*NY+j] = -y + 2*x + tcc[(NX-1)*NY+j]*delta - KAPPA*x - tgamma[(NX-1)*NY+j]*(x-y);
                    break;
                }
                case (BC_ABSORBING):
                {
                    delta = phi_in[(NX-2)*NY+j] + phi_in[(NX-1)*NY+j+1] + phi_in[(NX-1)*NY+j-1] - 3.0*x;
                    phi_out[(NX-1)*NY+j] = x - tc[(NX-1)*NY+j]*(x - phi_in[(NX-2)*NY+j]) - KAPPA_SIDES*x - GAMMA_SIDES*(x-y);
                    break;
                }
                case (BC_VPER_HABS):
                {
                    delta = phi_in[(NX-2)*NY+j] + phi_in[(NX-1)*NY+j+1] + phi_in[(NX-1)*NY+j-1] - 3.0*x;
                    phi_out[(NX-1)*NY+j] = x - tc[(NX-1)*NY+j]*(x - phi_in[(NX-2)*NY+j]) - KAPPA_SIDES*x - GAMMA_SIDES*(x-y);
                    break;
                }
            }
            psi_out[(NX-1)*NY+j] = x;
        }
    }
    
    /* top boundary */
    for (i=0; i<NX; i++){
        if ((TWOSPEEDS)||(xy_in[i*NY+NY-1] != 0)){
            x = phi_in[i*NY+NY-1];
            y = psi_in[i*NY+NY-1];
                    
            switch (B_COND) {
                case (BC_DIRICHLET):
                {
                    iplus = i+1;   if (iplus == NX) iplus = NX-1;
                    iminus = i-1;  if (iminus == -1) iminus = 0;
                    
                    delta = phi_in[iplus*NY+NY-1] + phi_in[iminus*NY+NY-1] + phi_in[i*NY+NY-2] - 3.0*x;
                    phi_out[i*NY+NY-1] = -y + 2*x + tcc[i*NY+NY-1]*delta - KAPPA*x - tgamma[i*NY+NY-1]*(x-y);
                    break;
                }
                case (BC_PERIODIC):
                {
                    iplus = (i+1) % NX;
                    iminus = (i-1) % NX;
                    if (iminus < 0) iminus += NX;
                    
                    delta = phi_in[iplus*NY+NY-1] + phi_in[iminus*NY+NY-1] + phi_in[i*NY+NY-2] + phi_in[i*NY] - 4.0*x;
                    phi_out[i*NY+NY-1] = -y + 2*x + tcc[i*NY+NY-1]*delta - KAPPA*x - tgamma[i*NY+NY-1]*(x-y);
                    break;
                }
                case (BC_ABSORBING):
                {
                    iplus = (i+1);   if (iplus == NX) iplus = NX-1;
                    iminus = (i-1);  if (iminus == -1) iminus = 0;
                    
                    delta = phi_in[iplus*NY+NY-1] + phi_in[iminus*NY+NY-1] + phi_in[i*NY+NY-2] - 3.0*x;
                    phi_out[i*NY+NY-1] = x - tc[i*NY+NY-1]*(x - phi_in[i*NY+NY-2]) - KAPPA_TOPBOT*x - GAMMA_TOPBOT*(x-y);
                    break;
                }
                case (BC_VPER_HABS):
                {
                    iplus = (i+1);   if (iplus == NX) iplus = NX-1;
                    iminus = (i-1);  if (iminus == -1) iminus = 0;

                    delta = phi_in[iplus*NY+NY-1] + phi_in[iminus*NY+NY-1] + phi_in[i*NY+NY-2] + phi_in[i*NY] - 4.0*x;
                    if (i==0) phi_out[NY-1] = x - tc[NY-1]*(x - phi_in[1*NY+NY-1]) - KAPPA_SIDES*x - GAMMA_SIDES*(x-y);
                    else phi_out[i*NY+NY-1] = -y + 2*x + tcc[i*NY+NY-1]*delta - KAPPA*x - tgamma[i*NY+NY-1]*(x-y);
                    break;
                }
            }
            psi_out[i*NY+NY-1] = x;
        }
    }
    
    /* bottom boundary */
    for (i=0; i<NX; i++){
        if ((TWOSPEEDS)||(xy_in[i*NY] != 0)){
            x = phi_in[i*NY];
            y = psi_in[i*NY];
                    
            switch (B_COND) {
                case (BC_DIRICHLET):
                {
                    iplus = i+1;   if (iplus == NX) iplus = NX-1;
                    iminus = i-1;  if (iminus == -1) iminus = 0;
                    
                    delta = phi_in[iplus*NY] + phi_in[iminus*NY] + phi_in[i*NY+1] - 3.0*x;
                    phi_out[i*NY] = -y + 2*x + tcc[i*NY]*delta - KAPPA*x - tgamma[i*NY]*(x-y);
                    break;
                }
                case (BC_PERIODIC):
                {
                    iplus = (i+1) % NX;
                    iminus = (i-1) % NX;
                    if (iminus < 0) iminus += NX;
                    
                    delta = phi_in[iplus*NY] + phi_in[iminus*NY] + phi_in[i*NY+1] + phi_in[i*NY+NY-1] - 4.0*x;
                    phi_out[i*NY] = -y + 2*x + tcc[i*NY]*delta - KAPPA*x - tgamma[i*NY]*(x-y);
                    break;
                }
                case (BC_ABSORBING):
                {
                    iplus = (i+1);   if (iplus == NX) iplus = NX-1;
                    iminus = (i-1);  if (iminus == -1) iminus = 0;
                    
                    delta = phi_in[iplus*NY] + phi_in[iminus*NY] + phi_in[i*NY+1] - 3.0*x;
                    phi_out[i*NY] = x - tc[i*NY]*(x - phi_in[i*NY+1]) - KAPPA_TOPBOT*x - GAMMA_TOPBOT*(x-y);
                    break;
                }
                case (BC_VPER_HABS):
                {
                    iplus = (i+1);   if (iplus == NX) iplus = NX-1;
                    iminus = (i-1);  if (iminus == -1) iminus = 0;

                    delta = phi_in[iplus*NY] + phi_in[iminus*NY] + phi_in[i*NY+1] + phi_in[i*NY+NY-1] - 4.0*x;
                    if (i==0) phi_out[0] = x - tc[0]*(x - phi_in[NY]) - KAPPA_SIDES*x - GAMMA_SIDES*(x-y);
                    else phi_out[i*NY] = -y + 2*x + tcc[i*NY]*delta - KAPPA*x - tgamma[i*NY]*(x-y);
                    break;
                }
            }
            psi_out[i*NY] = x;
        }
    }
    
    /* add oscillating boundary condition on the left corners */
    if (OSCILLATE_LEFT)
    {
        phi_out[0] = AMPLITUDE*cos((double)time*OMEGA);
        phi_out[NY-1] = AMPLITUDE*cos((double)time*OMEGA);
    }
    
    /* for debugging purposes/if there is a risk of blow-up */
    if (FLOOR) for (i=0; i<NX; i++){
        for (j=0; j<NY; j++){
            if (xy_in[i*NY+j] != 0) 
            {
                if (phi_out[i*NY+j] > VMAX) phi_out[i*NY+j] = VMAX;
                if (phi_out[i*NY+j] < -VMAX) phi_out[i*NY+j] = -VMAX;
                if (psi_out[i*NY+j] > VMAX) psi_out[i*NY+j] = VMAX;
                if (psi_out[i*NY+j] < -VMAX) psi_out[i*NY+j] = -VMAX;
            }
        }
    }
}


void evolve_wave(double phi[NX*NY], double psi[NX*NY], double phi_tmp[NX*NY], double psi_tmp[NX*NY], short int xy_in[NX*NY],
    double tc[NX*NY], double tcc[NX*NY], double tgamma[NX*NY])
/* time step of field evolution */
/* phi is value of field at time t, psi at time t-1 */
{
    evolve_wave_half(phi, psi, phi_tmp, psi_tmp, xy_in, tc, tcc, tgamma);
    evolve_wave_half(phi_tmp, psi_tmp, phi, psi, xy_in, tc, tcc, tgamma);
}


void draw_color_bar_palette(int plot, double range, int palette)
{
    if (ROTATE_COLOR_SCHEME) draw_color_scheme_palette_3d(-1.0, -0.8, XMAX - 0.1, -1.0, plot, -range, range, palette);
    else draw_color_scheme_palette_3d(XMAX - 0.3, YMIN + 0.1, XMAX - 0.1, YMAX - 0.1, plot, -range, range, palette);
}


void animation()
{
    double time, scale, ratio, startleft[2], startright[2], sign, r2, xy[2], fade_value; 
    double *phi, *psi, *phi_tmp, *psi_tmp, *total_energy, *color_scale, *tc, *tcc, *tgamma;
    short int *xy_in;
    int i, j, s, sample_left[2], sample_right[2], period = 0, fade;
    static int counter = 0;
    long int wave_value;
    t_wave *wave;
    
    if (SAVE_TIME_SERIES)
    {
        time_series_left = fopen("wave_left.dat", "w");
        time_series_right = fopen("wave_right.dat", "w");
    }

    /* Since NX and NY are big, it seemed wiser to use some memory allocation here */
    xy_in = (short int *)malloc(NX*NY*sizeof(short int));
    phi = (double *)malloc(NX*NY*sizeof(double));
    psi = (double *)malloc(NX*NY*sizeof(double));
    phi_tmp = (double *)malloc(NX*NY*sizeof(double));
    psi_tmp = (double *)malloc(NX*NY*sizeof(double));
    total_energy = (double *)malloc(NX*NY*sizeof(double));
    color_scale = (double *)malloc(NX*NY*sizeof(double));
    tc = (double *)malloc(NX*NY*sizeof(double));
    tcc = (double *)malloc(NX*NY*sizeof(double));
    tgamma = (double *)malloc(NX*NY*sizeof(double));
    
    wave = (t_wave *)malloc(NX*NY*sizeof(t_wave));
    
    
    /* initialise positions and radii of circles */
    if ((B_DOMAIN == D_CIRCLES)||(B_DOMAIN == D_CIRCLES_IN_RECT)) init_circle_config(circles);
    else if (B_DOMAIN == D_POLYGONS) init_polygon_config(polygons);
    printf("Polygons initialized\n");
    
    /* initialise polyline for von Koch and similar domains */
    npolyline = init_polyline(MDEPTH, polyline);
    for (i=0; i<npolyline; i++) printf("vertex %i: (%.3f, %.3f)\n", i, polyline[i].x, polyline[i].y);

    courant2 = COURANT*COURANT;
    courantb2 = COURANTB*COURANTB;
    
        

    
    /* initialize color scale, for option RESCALE_COLOR_IN_CENTER */
    if (RESCALE_COLOR_IN_CENTER)
    {
        for (i=0; i<NX; i++)
            for (j=0; j<NY; j++)
            {
                ij_to_xy(i, j, xy);
                r2 = xy[0]*xy[0] + xy[1]*xy[1];
                color_scale[i*NY+j] = 1.0 - exp(-4.0*r2/LAMBDA*LAMBDA);
            }
    }

    /* initialize wave with a drop at one point, zero elsewhere */
//     init_circular_wave(0.0, -LAMBDA, phi, psi, xy_in);
    
    /* initialize total energy table */
    if ((ZPLOT == P_MEAN_ENERGY)||(ZPLOT_B == P_MEAN_ENERGY)||(ZPLOT == P_LOG_MEAN_ENERGY)||(ZPLOT_B == P_LOG_MEAN_ENERGY))
        for (i=0; i<NX; i++)
            for (j=0; j<NY; j++) 
                total_energy[i*NY+j] = 0.0;
    
    ratio = (XMAX - XMIN)/8.4;  /* for Tokarsky billiard */
    
//     isospectral_initial_point(0.2, 0.0, startleft, startright);    /* for isospectral billiards */
//     homophonic_initial_point(0.5, -0.25, 1.5, -0.25, startleft, startright);
//     homophonic_initial_point(0.5, -0.25, 1.5, -0.25, startleft, startright);
//     printf("xleft = (%.3f, %.3f) xright = (%.3f, %.3f)\n", startleft[0], startleft[1], startright[0], startright[1]);    
    
//     xy_to_ij(startleft[0], startleft[1], sample_left);
//     xy_to_ij(startright[0], startright[1], sample_right);
//     printf("xleft = (%.3f, %.3f) xright = (%.3f, %.3f)\n", xin_left, yin_left, xin_right, yin_right);
    
//     init_wave_flat(phi, psi, xy_in);
    
//     init_wave_plus(LAMBDA - 0.3*MU, 0.5*MU, phi, psi, xy_in);
//     init_wave(LAMBDA - 0.3*MU, 0.5*MU, phi, psi, xy_in);
//     init_circular_wave(X_SHOOTER, Y_SHOOTER, phi, psi, xy_in);
//     printf("Initializing wave\n");

//     init_circular_wave_mod(polyline[85].x, polyline[85].y, phi, psi, xy_in);
//     init_circular_wave_mod(0.0, 0.0, phi, psi, xy_in);
    
    init_circular_wave_mod(0.2, 0.4, phi, psi, xy_in);
    add_circular_wave_mod(-1.0, -0.2, -0.4, phi, psi, xy_in);

    
//     add_circular_wave(-1.0, -0.2, -0.4, phi, psi, xy_in);

//     printf("Wave initialized\n");

        
//     init_circular_wave(0.6*cos((double)(period)*DPI/3.0), 0.6*sin((double)(period)*DPI/3.0), phi, psi, xy_in);
//     period++;
//     for (i=0; i<3; i++)
//     {
//         add_circular_wave(-1.0, 0.6*cos(PID + (double)(i)*DPI/3.0), 0.6*sin(PID + (double)(i)*DPI/3.0), phi, psi, xy_in);
//     }
//     add_circular_wave(1.0, -LAMBDA, 0.0, phi, psi, xy_in);
//     add_circular_wave(-1.0, 0.0, -LAMBDA, phi, psi, xy_in);
    
//     init_circular_wave_xplusminus(startleft[0], startleft[1], startright[0], startright[1], phi, psi, xy_in);

//     init_circular_wave_xplusminus(-0.9, 0.0, 0.81, 0.0, phi, psi, xy_in);
//     init_circular_wave(-2.0*ratio, 0.0, phi, psi, xy_in);
//     init_planar_wave(XMIN + 0.015, 0.0, phi, psi, xy_in);
//     init_planar_wave(XMIN + 0.02, 0.0, phi, psi, xy_in);
//     init_planar_wave(XMIN + 0.5, 0.0, phi, psi, xy_in);
//     init_wave(-1.5, 0.0, phi, psi, xy_in);
//     init_wave(0.0, 0.0, phi, psi, xy_in);

    /* add a drop at another point */
//     add_drop_to_wave(1.0, 0.7, 0.0, phi, psi);
//     add_drop_to_wave(1.0, -0.7, 0.0, phi, psi);
//     add_drop_to_wave(1.0, 0.0, -0.7, phi, psi);

    /* initialize table of wave speeds/dissipation */
    for (i=0; i<NX; i++){
        for (j=0; j<NY; j++){
            if (xy_in[i*NY+j] != 0)
            {
                tc[i*NY+j] = COURANT;
                tcc[i*NY+j] = courant2;
                if (xy_in[i*NY+j] == 1) tgamma[i*NY+j] = GAMMA;
                else tgamma[i*NY+j] = GAMMAB;
            }
            else if (TWOSPEEDS)
            {
                tc[i*NY+j] = COURANTB;
                tcc[i*NY+j] = courantb2;
                tgamma[i*NY+j] = GAMMAB;
            }
        }
    }

    blank();
    glColor3f(0.0, 0.0, 0.0);
    draw_wave_3d(phi, psi, xy_in, wave, ZPLOT, CPLOT, COLOR_PALETTE, 0, 1.0);
//     draw_billiard();
    
    
    if (DRAW_COLOR_SCHEME) draw_color_bar_palette(CPLOT, COLORBAR_RANGE, COLOR_PALETTE);

    glutSwapBuffers();



    sleep(SLEEP1);

    for (i=0; i<=INITIAL_TIME + NSTEPS; i++)
    {
	//printf("%d\n",i);
        /* compute the variance of the field to adjust color scheme */
        /* the color depends on the field divided by sqrt(1 + variance) */
        if (SCALE)
        {
            scale = sqrt(1.0 + compute_variance_mod(phi,psi, xy_in));
//             printf("Scaling factor: %5lg\n", scale);
        }
        else scale = 1.0;
        
        draw_wave_3d(phi, psi, xy_in, wave, ZPLOT, CPLOT, COLOR_PALETTE, 0, 1.0);
        for (j=0; j<NVID; j++) 
        {
            evolve_wave(phi, psi, phi_tmp, psi_tmp, xy_in, tc, tcc, tgamma);
            if (SAVE_TIME_SERIES)
            {
                wave_value = (long int)(phi[sample_left[0]*NY+sample_left[1]]*1.0e16);
                fprintf(time_series_left, "%019ld\n", wave_value);
                wave_value = (long int)(phi[sample_right[0]*NY+sample_right[1]]*1.0e16);
                fprintf(time_series_right, "%019ld\n", wave_value);
                if ((j == 0)&&(i%10 == 0)) printf("Frame %i of %i\n", i, NSTEPS);
//                 fprintf(time_series_right, "%.15f\n", phi[sample_right[0]][sample_right[1]]);
            }
//             if (i % 10 == 9) oscillate_linear_wave(0.2*scale, 0.15*(double)(i*NVID + j), -1.5, YMIN, -1.5, YMAX, phi, psi);
        }
        
//         draw_billiard();
        
        if (DRAW_COLOR_SCHEME) draw_color_bar_palette(CPLOT, COLORBAR_RANGE, COLOR_PALETTE); 
        
        /* add oscillating waves */
        if ((ADD_OSCILLATING_SOURCE)&&(i%OSCILLATING_SOURCE_PERIOD == OSCILLATING_SOURCE_PERIOD - 1))
        {
            add_circular_wave_mod(1.0, -1.0, 0.0, phi, psi, xy_in);
//               add_circular_wave(1.0, -1.5*LAMBDA, 0.0, phi, psi, xy_in);
//             add_circular_wave(-1.0, 0.6*cos((double)(period)*DPI/3.0), 0.6*sin((double)(period)*DPI/3.0), phi, psi, xy_in);
            period++;    
        }

	glutSwapBuffers();

	if (MOVIE)
        {
            if (i >= INITIAL_TIME) save_frame();
            else printf("Initial phase time %i of %i\n", i, INITIAL_TIME);
            
            if ((i >= INITIAL_TIME)&&(DOUBLE_MOVIE))
            {
                draw_wave_3d(phi, psi, xy_in, wave, ZPLOT_B, CPLOT_B, COLOR_PALETTE_B, 0, 1.0);
                if (DRAW_COLOR_SCHEME) draw_color_bar_palette(CPLOT_B, COLORBAR_RANGE_B, COLOR_PALETTE_B);  
                glutSwapBuffers();
                save_frame_counter(NSTEPS + MID_FRAMES + 1 + counter);
//                 save_frame_counter(NSTEPS + 21 + counter);
                counter++;
            }

            /* it seems that saving too many files too fast can cause trouble with the file system */
            /* so this is to make a pause from time to time - parameter PAUSE may need adjusting   */
            if (i % PAUSE == PAUSE - 1)
            {
                printf("Making a short pause\n");
                sleep(PSLEEP);
                s = system("mv wave*.tif tif_wave/");
            }
        }

    }

    if (MOVIE) 
    {
        if (DOUBLE_MOVIE) 
        {
            draw_wave_3d(phi, psi, xy_in, wave, ZPLOT, CPLOT, COLOR_PALETTE, 0, 1.0);
            if (DRAW_COLOR_SCHEME) draw_color_bar_palette(CPLOT, COLORBAR_RANGE, COLOR_PALETTE);   
            glutSwapBuffers();
            
            if (!FADE) for (i=0; i<MID_FRAMES; i++) save_frame();
            else for (i=0; i<MID_FRAMES; i++) 
            {
                draw_wave_3d(phi, psi, xy_in, wave, ZPLOT, CPLOT, COLOR_PALETTE, 1, 1.0 - (double)i/(double)MID_FRAMES);
                if (DRAW_COLOR_SCHEME) draw_color_bar_palette(CPLOT, COLORBAR_RANGE, COLOR_PALETTE);   
                glutSwapBuffers();
                save_frame_counter(NSTEPS + i + 1);
            }
            draw_wave_3d(phi, psi, xy_in, wave, ZPLOT_B, CPLOT_B, COLOR_PALETTE_B, 0, 1.0);
            if (DRAW_COLOR_SCHEME) draw_color_bar_palette(CPLOT_B, COLORBAR_RANGE_B, COLOR_PALETTE_B); 
            glutSwapBuffers();
            
            if (!FADE) for (i=0; i<END_FRAMES; i++) save_frame_counter(NSTEPS + MID_FRAMES + 1 + counter + i);
            else for (i=0; i<END_FRAMES; i++) 
            {
                draw_wave_3d(phi, psi, xy_in, wave, ZPLOT_B, CPLOT_B, COLOR_PALETTE_B, 1, 1.0 - (double)i/(double)END_FRAMES);
                if (DRAW_COLOR_SCHEME) draw_color_bar_palette(CPLOT_B, COLORBAR_RANGE_B, COLOR_PALETTE_B);   
                glutSwapBuffers();
                save_frame_counter(NSTEPS + MID_FRAMES + 1 + counter + i);
            }
        }
        else
        {
            if (!FADE) for (i=0; i<END_FRAMES; i++) save_frame_counter(NSTEPS + MID_FRAMES + 1 + counter + i);
            else for (i=0; i<END_FRAMES; i++) 
            {
                draw_wave_3d(phi, psi, xy_in, wave, ZPLOT, CPLOT, COLOR_PALETTE, 1, 1.0 - (double)i/(double)END_FRAMES);
                if (DRAW_COLOR_SCHEME) draw_color_bar_palette(CPLOT, COLORBAR_RANGE, COLOR_PALETTE); 
                glutSwapBuffers();
                save_frame_counter(NSTEPS + 1 + counter + i);
            }
        }
        
        s = system("mv wave*.tif tif_wave/");
    }
    
    free(xy_in);
    free(phi);
    free(psi);
    free(phi_tmp);
    free(psi_tmp);
    free(total_energy);
    free(color_scale);
    free(tc);
    free(tcc);
    free(tgamma);
    
    free(wave);

    
    if (SAVE_TIME_SERIES)
    {
        fclose(time_series_left);
        fclose(time_series_right);
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
    glutCreateWindow("Wave equation in a planar domain");

    init_3d();

    glutDisplayFunc(display);

    glutMainLoop();

    return 0;
}

