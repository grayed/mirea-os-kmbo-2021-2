#include <err.h>
#include <stdio.h>
#include <stdlib.h>

int nums[10];
size_t num_cnt[10];

void
usage(const char *msg) {
	if (msg)
		fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "usage: avrg source-file\n");
	exit(1);
}

int
main(int argc, char **argv) {
	FILE *fp;
	char *line = NULL;
	size_t linesize = 0, i;
	ssize_t linelen;

	if (argc <= 1)
		usage("source missing");
	if ((fp = fopen(argv[1], "r")) == NULL)
		err(1, "could not open source");
	while ((linelen = getline(&line, &linesize, fp)) != -1) {
		int n = atoi(line);
		nums[n % 10] += n;
		num_cnt[n % 10]++;
	}
	free(line);
	if (ferror(fp))
		warn("getline");
	for (i = 0; i < 10; i++) {
		double n = (double)nums[i] / (double)num_cnt[i];
		printf("%zu: %f\n", i, n);
	}
	return 0;
}
