#include<stdint.h>
#define f 16384
int64_t toFixedPt(int64_t n);
int64_t toInteger(int64_t x);
int64_t toIntegerRounded(int64_t x);
int64_t addXY(int64_t x, int64_t y);
int64_t subtractXY(int64_t x, int64_t y);
int64_t addXN(int64_t x, int64_t n);
int64_t subtractXN(int64_t x, int64_t n);
int64_t multiplyXY(int64_t x, int64_t y);
int64_t multiplyXN(int64_t x, int64_t n);
int64_t divideXY(int64_t x, int64_t y);
int64_t divideXN(int64_t x, int64_t n);


int64_t toFixedPt(int64_t n) {
    return n*f;
}
int64_t toInteger(int64_t x) {
    return x/f;
}
int64_t toIntegerRounded(int64_t x) {
    return ((x>=0)? (x+ f/2)/f :(x- f/2)/f) ;
}
int64_t addXY(int64_t x, int64_t y) {
    return (x+y);
}

int64_t subtractXY(int64_t x, int64_t y) {
    return (x-y);
}


int64_t addXN(int64_t x, int64_t n) {
    return (x+ n*f);
}

int64_t subtractXN(int64_t x, int64_t n) {
    return (x-n*f);
}
int64_t multiplyXY(int64_t x, int64_t y) {
    return 	((int64_t) x) * y / f;
}

int64_t multiplyXN(int64_t x, int64_t n) {
    return 	x * n;
}

int64_t divideXY(int64_t x, int64_t y) {
    return 	((int64_t) x) * f / y;
}

int64_t divideXN(int64_t x, int64_t n) {
    return 	x / n;
}

