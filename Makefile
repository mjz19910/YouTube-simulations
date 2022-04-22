CFLAGS = -g -O3 -lm -ltiff -lGL -lGLU -lX11 -lXmu -lglut
all: mangrove drop_billiard wave_billiard lennardjones wave_energy heat wave_3d particle_pinball particle_billiard wave_comparison schrodinger

%: %.c
	$(CC) -o $@ $< $(CFLAGS)

