/* cdemo.c: the C source cbuild/c_sample.dbg describes */
static unsigned char counter;

static void tick(void)
{
	++counter;
}

void main(void)
{
	counter = 0;
	tick();
}
