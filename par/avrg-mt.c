#include <err.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

int *nums;
size_t cnt, allocated;

struct calc_avg_args {
	int digit;
	double avg;
} args[10];

void
usage(const char *msg) {
	if (msg)
		fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "usage: avrg-mt source-file\n");
	exit(1);
}

void *
calc_avg(void *p) {
	struct calc_avg_args *a = p;
	int	sum = 0;
	size_t	n = 0;

	for (size_t i = 0; i < cnt; i++)
		if (nums[i] % 10 == a->digit) {
			sum += nums[i];
			n++;
		}
	a->avg = (double)sum / (double)n;
	return NULL;
}

int
main(int argc, char **argv) {
	FILE *fp;
	char *line = NULL;
	size_t linesize = 0, i;
	ssize_t linelen;
	pthread_t threads[10];
	int ec;

	if (argc <= 1)
		usage("source missing");
	if ((fp = fopen(argv[1], "r")) == NULL)
		err(1, "could not open source");
	while ((linelen = getline(&line, &linesize, fp)) != -1) {
		int n = atoi(line);
		if (cnt == allocated) {
			allocated += 1000;
			void *p = realloc(nums, allocated * sizeof(nums[0]));
			if (p == NULL)
				err(1, "realloc");
			nums = p;
		}
		nums[cnt++] = n;
	}
	free(line);
	if (ferror(fp))
		warn("getline");

	for (i = 0; i < 10; i++) {
		args[i].digit = i;
		if ((ec = pthread_create(&threads[i], NULL, calc_avg, &args[i])) != 0)
			err(1, "pthread_create");
	}
	for (i = 0; i < 10; i++) {
		pthread_join(threads[i], NULL);
		printf("%zu: %f\n", i, args[i].avg);
	}
	free(nums);

	return 0;
}
