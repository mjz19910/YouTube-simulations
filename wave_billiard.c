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

/* General geometrical parameters */

#define WINWIDTH 	1280  /* window width */
#define WINHEIGHT 	720   /* window height */

#define NX 640          /* number of grid points on x axis */
#define NY 360          /* number of grid points on y axis */

/* setting NX to WINWIDTH and NY to WINHEIGHT increases resolution */
/* but will multiply run time by 4                                 */

#define XMIN -2.0
#define XMAX 2.0	/* x interval */
#define YMIN -1.125
#define YMAX 1.125	/* y interval for 9/16 aspect ratio */

/* Choice of the billiard table */

#define B_DOMAIN 8      /* choice of domain shape */

#define D_RECTANGLE 0   /* rectangular domain */
#define D_ELLIPSE 1     /* elliptical domain */
#define D_STADIUM 2     /* stadium-shaped domain */
#define D_SINAI 3       /* Sinai billiard */
#define D_DIAMOND 4     /* diamond-shaped billiard */
#define D_TRIANGLE 5    /* triangular billiard */
#define D_FLAT 6        /* flat interface */
#define D_ANNULUS 7     /* annulus */
#define D_POLYGON 8     /* polygon */
#define D_YOUNG 9       /* Young diffraction slits */
#define D_GRATING 10    /* diffraction grating */
#define D_EHRENFEST 11  /* Ehrenfest urn type geometry */

#define LAMBDA 1.0	    /* parameter controlling the dimensions of domain */
#define MU 0.05	            /* parameter controlling the dimensions of domain */
#define NPOLY 8             /* number of sides of polygon */
#define APOLY 1.0           /* angle by which to turn polygon, in units of Pi/2 */ 
#define FOCI 1              /* set to 1 to draw focal points of ellipse */

/* You can add more billiard tables by adapting the functions */
/* xy_in_billiard and draw_billiard below */

/* Physical patameters of wave equation */

#define COURANT 0.01       /* Courant number */
#define GAMMA 0.0      /* damping factor in wave equation */
// #define GAMMA 5.0e-10      /* damping factor in wave equation */
#define KAPPA 5.0e-6       /* "elasticity" term enforcing oscillations */
// #define KAPPA 5.0e-9       /* "elasticity" term enforcing oscillations */
// #define KAPPA 5.0e-8       /* "elasticity" term enforcing oscillations */
/* The Courant number is given by c*DT/DX, where DT is the time step and DX the lattice spacing */
/* The physical damping coefficient is given by GAMMA/(DT)^2 */
/* Increasing COURANT speeds up the simulation, but decreases accuracy */
/* For similar wave forms, COURANT^2*GAMMA should be kept constant */

/* For debugging purposes only */
#define FLOOR 0         /* set to 1 to limit wave amplitude to VMAX */
#define VMAX 10.0       /* max value of wave amplitude */

/* Parameters for length and speed of simulation */

#define NSTEPS 5000      /* number of frames of movie */
#define NVID 25          /* number of iterations between images displayed on screen */
#define NSEG 100         /* number of segments of boundary */

#define PAUSE 1000         /* number of frames after which to pause */
#define PSLEEP 1         /* sleep time during pause */
#define SLEEP1  1        /* initial sleeping time */
#define SLEEP2  1   /* final sleeping time */

/* Color schemes */

#define BLACK 1          /* background */

#define COLOR_SCHEME 1   /* choice of color scheme */

#define C_LUM 0          /* color scheme modifies luminosity (with slow drift of hue) */
#define C_HUE 1          /* color scheme modifies hue */

#define SCALE 1          /* set to 1 to adjust color scheme to variance of field */
#define SLOPE 1.0        /* sensitivity of color on wave amplitude */
#define ATTENUATION 0.0  /* exponential attenuation coefficient of contrast with time */

#define COLORHUE 260     /* initial hue of water color for scheme C_LUM */
#define COLORDRIFT 0.0   /* how much the color hue drifts during the whole simulation */
#define LUMMEAN 0.5      /* amplitude of luminosity variation for scheme C_LUM */
#define LUMAMP 0.3       /* amplitude of luminosity variation for scheme C_LUM */
#define HUEMEAN 100.0    /* mean value of hue for color scheme C_HUE */
#define HUEAMP 80.0      /* amplitude of variation of hue for color scheme C_HUE */
// #define HUEMEAN 320.0    /* mean value of hue for color scheme C_HUE */
// #define HUEAMP 100.0      /* amplitude of variation of hue for color scheme C_HUE */

/* Basic math */

#define PI 	3.141592654
#define DPI 	6.283185307
#define PID 	1.570796327

#include "sub_wave.c"

double courant2;  /* Courant parameter squared */

void init_wave(x, y, phi, psi, xy_in)
/* initialise field with drop at (x,y) - phi is wave height, psi is phi at time t-1 */
    double x, y, *phi[NX], *psi[NX]; short int * xy_in[NX];

{
    int i, j;
    double xy[2], dist2;

    for (i=0; i<NX; i++)
        for (j=0; j<NY; j++)
        {
            ij_to_xy(i, j, xy);
            dist2 = (xy[0]-x)*(xy[0]-x) + (xy[1]-y)*(xy[1]-y);
	    xy_in[i][j] = xy_in_billiard(xy[0],xy[1]);
	    phi[i][j] = 0.2*exp(-dist2/0.001)*cos(-sqrt(dist2)/0.01);
            psi[i][j] = 0.0;
        }
}

void add_drop_to_wave(factor, x, y, phi, psi)
/* add drop at (x,y) to the field with given prefactor */
double factor, x, y, *phi[NX], *psi[NX];
{
    int i, j;
    double xy[2], dist2;

    for (i=0; i<NX; i++)
        for (j=0; j<NY; j++)
        {
            ij_to_xy(i, j, xy);
            dist2 = (xy[0]-x)*(xy[0]-x) + (xy[1]-y)*(xy[1]-y);
            phi[i][j] += 0.2*factor*exp(-dist2/0.001)*cos(-sqrt(dist2)/0.01);
        }
}




/*********************/
/* animation part    */
/*********************/


void draw_wave(phi, psi, xy_in, scale, time)
/* draw the field */
double *phi[NX], *psi[NX], scale;
short int *xy_in[NX];
int time;
{
    int i, j;
    double rgb[3], xy[2], x1, y1, x2, y2;

    glBegin(GL_QUADS);

    for (i=0; i<NX; i++)
        for (j=0; j<NY; j++)
        {
            if (xy_in[i][j])
            {
                color_scheme(COLOR_SCHEME, phi[i][j], scale, time, rgb);
                glColor3f(rgb[0], rgb[1], rgb[2]);

                glVertex2i(i, j);
                glVertex2i(i+1, j);
                glVertex2i(i+1, j+1);
                glVertex2i(i, j+1);
            }
        }

    glEnd ();
}

void evolve_wave(phi, psi, xy_in)
/* time step of field evolution */
/* phi is value of field at time t, psi at time t-1 */
    double *phi[NX], *psi[NX]; short int *xy_in[NX];
{
    int i, j, iplus, iminus, jplus, jminus;
    double delta, x, y;

    #pragma omp parallel for private(i,j,iplus,iminus,jplus,jminus,delta,x,y)
    for (i=0; i<NX; i++){
        for (j=0; j<NY; j++){
            if (xy_in[i][j]){
                /* discretized Laplacian */
		iplus = (i+1) % NX;
		iminus = (i-1) % NX;
		if (iminus < 0) iminus += NX;
                jplus = (j+1) % NY;
                jminus = (j-1) % NY;
                if (jminus < 0) jminus += NY;
                delta = phi[iplus][j] + phi[iminus][j] + phi[i][jplus] + phi[i][jminus] - 4.0*phi[i][j];

                x = phi[i][j];
		y = psi[i][j];

                /* evolve phi */
                phi[i][j] = -y + 2*x + courant2*delta - KAPPA*x - GAMMA*(x-y);

                /* Old versions of the simulation used this: */
//                 phi[i][j] = (-psi[i][j] + 2*phi[i][j] + courant2*delta)*damping;
//                 where damping = 1.0 - 0.0001;

                psi[i][j] = x;

                if (FLOOR)
                {
                    if (phi[i][j] > VMAX) phi[i][j] = VMAX;
                    if (phi[i][j] < -VMAX) phi[i][j] = -VMAX;
                    if (psi[i][j] > VMAX) psi[i][j] = VMAX;
                    if (psi[i][j] < -VMAX) psi[i][j] = -VMAX;
                }
            }
        }
    }
//     printf("phi(0,0) = %.3lg, psi(0,0) = %.3lg\n", phi[NX/2][NY/2], psi[NX/2][NY/2]);
}


double compute_variance(phi, psi, xy_in)
/* compute the variance of the field, to adjust color scheme */
    double *phi[NX], *psi[NX]; short int * xy_in[NX];
{
    int i, j, n = 0;
    double variance = 0.0;

    for (i=1; i<NX; i++)
        for (j=1; j<NY; j++)
        {
            if (xy_in[i][j])
            {
                n++;
                variance += phi[i][j]*phi[i][j];
            }
        }
    if (n==0) n=1;
    return(variance/(double)n);
}


void animation()
{
    double time, scale;
    double *phi[NX], *psi[NX];
    short int *xy_in[NX];
    int i, j, s;

    /* Since NX and NY are big, it seemed wiser to use some memory allocation here */
    for (i=0; i<NX; i++)
    {
        phi[i] = (double *)malloc(NY*sizeof(double));
        psi[i] = (double *)malloc(NY*sizeof(double));
        xy_in[i] = (short int *)malloc(NY*sizeof(short int));
    }

    courant2 = COURANT*COURANT;

    /* initialize wave with a drop at one point, zero elsewhere */
    init_wave(0.0, 0.0, phi, psi, xy_in);

    /* add a drop at another point */
//     add_drop_to_wave(1.0, 0.7, 0.0, phi, psi);
//     add_drop_to_wave(1.0, -0.7, 0.0, phi, psi);
//     add_drop_to_wave(1.0, 0.0, -0.7, phi, psi);

    blank();
    glColor3f(0.0, 0.0, 0.0);
    draw_wave(phi, psi, xy_in, 1.0, 0);
    draw_billiard();

    glutSwapBuffers();



    sleep(SLEEP1);

    for (i=0; i<=NSTEPS; i++)
    {
	//printf("%d\n",i);
        /* compute the variance of the field to adjust color scheme */
        /* the color depends on the field divided by sqrt(1 + variance) */
        if (SCALE)
        {
            scale = sqrt(1.0 + compute_variance(phi,psi, xy_in));
//             printf("Scaling factor: %5lg\n", scale);
        }

        else scale = 1.0;


        draw_wave(phi, psi, xy_in, scale, i);
        for (j=0; j<NVID; j++) evolve_wave(phi, psi, xy_in);
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
                s = system("mv wave*.tif tif_wave/");
            }
        }

    }

    if (MOVIE) 
    {
        for (i=0; i<20; i++) save_frame();
        s = system("mv wave*.tif tif_wave/");
    }
    for (i=0; i<NX; i++)
    {
        free(phi[i]);
        free(psi[i]);
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

    init();

    glutDisplayFunc(display);

    glutMainLoop();

    return 0;
}

