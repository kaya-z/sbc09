10 REM the Mandelbrot set in characters: 0-F is the number of steps before the point leaves the circle of radius 2
20 REM (the same field as the ASCII art benchmark: x from -1.7862 to 1.7862, y from -0.99996 to 0.99996)
30 FOR Y = -12 TO 12
40 FOR X = -39 TO 39
50 CA = X * 0.0458: CB = Y * 0.08333
60 A = CA: B = CB
70 FOR I = 0 TO 15
80 T = A * A - B * B + CA
90 B = 2 * A * B + CB
100 A = T
110 IF A * A + B * B > 4 THEN 150
120 NEXT I
130 PRINT " ";
140 GOTO 170
150 IF I > 9 THEN I = I + 7
160 PRINT CHR$(48 + I);
170 NEXT X
180 PRINT
190 NEXT Y
