/* hello.c: a C test ROM for source-level debugging (cc65, NES).
 * main calls add in a loop, so there are C lines to step over, a function
 * to step into, and a call stack two frames deep. */
static unsigned char counter;

static unsigned char add(unsigned char a, unsigned char b)
{
    unsigned char sum;
    sum = a + b;
    return sum;
}

void main(void)
{
    for (;;) {
        counter = add(counter, 1);
        if (counter == 100) {
            counter = 0;
        }
    }
}
