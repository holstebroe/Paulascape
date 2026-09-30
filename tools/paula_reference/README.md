# Paula reference renders

`test/resources/paula/*.f32` are raw 32-bit float renders (left channel, 44.1 kHz, 6000 samples) of one looping
voice made with pt2-clone's own `pt2_paula.c`, `pt2_blep.c` and `pt2_rcfilters.c`. `paula_reference_test`
compares Paulascape's voice and filters with them.

To regenerate, get pt2-clone (https://github.com/8bitbubsy/pt2-clone), then from its `src/` folder:

```
# stub headers that pull in SDL: pt2_header.h (PI, ASSERT, AMIGA_PAL_CCK_HZ 3546894.6), pt2_audio.h, pt2_replayer.h (empty)
gcc -c -I. pt2_paula.c pt2_blep.c pt2_rcfilters.c
g++ -I. gen_reference.cpp pt2_paula.o pt2_blep.o pt2_rcfilters.o -o gen_reference
for p in 214 428 124 113; do for c in 0 1 2; do ./gen_reference <Paulascape>/test/resources/paula $p $c; done; done
```
