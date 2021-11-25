#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int *nums;
size_t cnt, allocated;

struct calc_avg_args {
	int digit;
	double avg;
	int sock[2];
} args[10];

void
usage(const char *msg) {
	if (msg)
		fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "usage: avrg-mt-chan source-file\n");
	exit(1);
}

void *
calc_avg(void *p) {
	struct calc_avg_args *a = p;
	int	ec, sum = 0;
	size_t	cnt = 0, bufread = 0;
	ssize_t	nread;
	struct pollfd pfd[1];
	struct timeval tvlast, tv;
	int wait = 1000, flags;
	union {
		int	n;
		char	buf[sizeof(int)];
	} u;

	flags = fcntl(a->sock[0], F_GETFL);
	fcntl(a->sock[0], F_SETFL, flags | O_NONBLOCK);

	memset(pfd, 0, sizeof(pfd));
	pfd[0].fd = a->sock[0];
	pfd[0].events = POLLIN;		// TODO '| POLLOUT' for partial write case
	gettimeofday(&tvlast, NULL);

	for (;;) {
		pfd[0].revents = 0;
		if (poll(pfd, sizeof(pfd)/sizeof(pfd[0]), wait) == 1) {
			nread = read(a->sock[0], u.buf + bufread, sizeof(u.n) - bufread);
			switch (nread) {
			case -1:
				if (errno != EAGAIN)
					err(1, "read");
				break;

			case 0:
				goto eof;

			default:
				bufread += nread;
				if (bufread == sizeof(int)) {
					sum += u.n;
					cnt++;
					bufread = 0;
				}
			}
		}

		gettimeofday(&tv, NULL);
		if (tv.tv_usec - tvlast.tv_usec + 1000000 * (tv.tv_sec - tvlast.tv_sec) > 1000000) {
			// send current stats
			a->avg = cnt ? (double)sum / (double)cnt : 0;
			write(a->sock[0], &a->avg, sizeof(a->avg));	// TODO 2.1 Исправить частичную запись
			tvlast = tv;
		}

		wait = 1000 - (tv.tv_usec - tvlast.tv_usec + 1000000 * (tv.tv_sec - tvlast.tv_sec)) / 1000;
	}

eof:
	a->avg = cnt ? (double)sum / (double)cnt : 0;
	close(a->sock[0]);
	return NULL;
}

int
main(int argc, char **argv) {
	FILE *fp;
	char *line = NULL;
	size_t linesize = 0, i;
	ssize_t linelen, nwritten;
	pthread_t threads[10];
	int ec;
	struct pollfd pfd[11];
	union {
		int n;
		char buf[sizeof(int)];
	} u[10];
	size_t sent[10];
	int readbuf[10][1024];		// буфер для отправки данных потоку
	size_t readbuf_filled[10] = { 0 };	// насколько заполнен буфер потока
	size_t readbuf_read[10] = { 0 };	// насколько прочитан буфер потока
	size_t nbuffers_fillable = 10;

	if (argc <= 1)
		usage("source missing");
	if ((fp = fopen(argv[1], "r")) == NULL)
		err(1, "could not open source");

	memset(pfd, 0, sizeof(pfd));
	memset(sent, 0, sizeof(sent));
	for (i = 0; i < 10; i++) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, args[i].sock) == -1)
			err(1, "socketpair");
		args[i].digit = i;
		pfd[i].fd = args[i].sock[1];
		pfd[i].events = POLLIN;
		if ((ec = pthread_create(&threads[i], NULL, calc_avg, &args[i])) != 0)
			err(1, "pthread_create");
	}
	pfd[10].fd = fileno(fp);
	pfd[10].events = POLLIN;

	for (;;) {
		for (i = 0; i < sizeof(pfd)/sizeof(pfd[0]); i++)
			pfd[i].revents = 0;

		poll(pfd, sizeof(pfd)/sizeof(pfd[0]), -1);
		for (i = 0; i < 10; i++) {
			if ((pfd[i].revents & POLLOUT) == POLLOUT) {
				nwritten = write(pfd[i].fd, u[i].buf + sent[i], sizeof(int)-sent[i]);
				if (nwritten > 0) {
					sent[i] += nwritten;
					if (sent[i] == sizeof(int)) {
						sent[i] = 0;
						pfd[i].events = POLLIN;
					}
				} else if (errno != EAGAIN) {
					err(1, "write");
				}
			}

			if ((pfd[i].revents & POLLIN) == POLLIN) {
				double curavg;

				// TODO 2.2 Исправить частичное чтение
				if (read(pfd[i].fd, &curavg, sizeof(curavg)) == sizeof(double)) {
					fprintf(stderr, "current average for %zu: %f\n", i, curavg);
				}
			}
		}

		if ((pfd[10].events & POLLIN) == POLLIN) {
			linelen = getline(&line, &linesize, fp);
			if (linelen == -1) {
				if (!ferror(fp))
					break;
				else if (errno != EAGAIN) {
					warn("getline");
					break;
				}
			} else {
				// TODO 1
				// Предупредить ситуацию с отправкой в сокет числа, когда в него ещё
				// не отправилось полностью предыдущее.
				int n = atoi(line);
				int digit = n % 10;
				readbuf[digit][readbuf_filled[digit]++] = n;
			}
		}

		// getline -> atoi -> readbuf[digit]
		// readbuf[digit] -> отправка потоку

		nbuffers_fillable = 0;
		for (i = 0; i < 10; i++) {
			if (readbuf_read[i] < readbuf_filled[i]) {
				int n = readbuf[i][readbuf_read[i]];
				if ((pfd[i].events & POLLOUT) == 0) {
					pfd[i].events |= POLLOUT;
					u[i].n = n;
					readbuf_read[i]++;
				}
			}

			memmove(&readbuf[i][0], &readbuf[i][readbuf_read[i]],
			    (readbuf_filled[i] - readbuf_read[i]) * sizeof(readbuf[0]));
			readbuf_filled[i] -= readbuf_read[i];
			readbuf_read[i] = 0;

			if (readbuf_filled[i] < sizeof(readbuf[i]) / sizeof(readbuf[i][0]))
				nbuffers_fillable++;
		}
	}

	free(line);

	for (i = 0; i < 10; i++) {
		close(args[i].sock[1]);
		pthread_join(threads[i], NULL);
		printf("%zu: %f\n", i, args[i].avg);
	}

	return 0;
}

// TODO 3
// Повысить производительность за счёт передачи/считывания нескольких чисел за раз.
// Например, можно использовать буферизованный ввод-вывод с помощью fdopen().
