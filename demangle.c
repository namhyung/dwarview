/*
 * Simple demangler interface using the external `c++filt` program.
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

struct demangler {
	int	in;
	int	out;
	bool	ok;
};

static struct demangler d;

/* popen() doesn't provide bidirectional streams, do it manually */
void setup_demangler(void)
{
    int p1[2], p2[2];

    d.ok = false;

    if (pipe(p1) < 0 || pipe(p2) < 0)
	    return;

    switch (fork()) {
    case -1:  /* error */
	    return;

    case 0:  /* child */
	    dup2(p1[0], STDIN_FILENO);
	    dup2(p2[1], STDOUT_FILENO);

	    close(p1[0]);
	    close(p1[1]);
	    close(p2[0]);
	    close(p2[1]);

	    execlp("c++filt", NULL);
	    break;

    default:  /* parent */
	    d.in  = p1[1];
	    d.out = p2[0];

	    close(p1[0]);
	    close(p2[1]);

	    d.ok = true;
	    break;
    }
}

void finish_demangler(void)
{
	if (d.ok) {
		close(d.in);
		close(d.out);
	}

	d.ok = false;
}

bool demangler_enabled(void)
{
	return d.ok;
}

int demangle(const char *input, char *output, int outlen)
{
	int ret;
	int len = strlen(input);
	char flush = '\n';

	if (!d.ok) {
		memcpy(output, input, len+1);
		return 0;
	}

	write(d.in, input, len);
	write(d.in, &flush, 1);

	ret = read(d.out, output, outlen);
	output[ret - 1] = '\0';

	return ret;
}
