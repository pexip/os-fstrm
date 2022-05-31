/*
 * Copyright (c) 2014, 2018 by Farsight Security, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>

#include <fstrm.h>

#include "libmy/print_string.h"

static fstrm_res
process_start_frame(struct fstrm_reader *r, struct fstrm_writer_options *wopt)
{
	fstrm_res res;
	const struct fstrm_control *control = NULL;
	size_t n_content_type = 0;
	const uint8_t *content_type = NULL;
	size_t len_content_type = 0;

	res = fstrm_reader_get_control(r, FSTRM_CONTROL_START, &control);
	if (res != fstrm_res_success)
		return res;
	fputs("FSTRM_CONTROL_START.\n", stderr);

	res = fstrm_control_get_num_field_content_type(control, &n_content_type);
	if (res != fstrm_res_success)
		return res;
	if (n_content_type > 0) {
		res = fstrm_control_get_field_content_type(control, 0,
			&content_type, &len_content_type);
		if (res != fstrm_res_success)
			return res;
		fprintf(stderr, "FSTRM_CONTROL_FIELD_CONTENT_TYPE (%zd bytes).\n ",
			len_content_type);
		print_string(content_type, len_content_type, stderr);
		fputc('\n', stderr);
	}

	if (wopt != NULL && content_type != NULL) {
		res = fstrm_writer_options_add_content_type(wopt,
			content_type, len_content_type);
		if (res != fstrm_res_success)
			return res;
	}

	return fstrm_res_success;
}

static fstrm_res
print_stop_frame(struct fstrm_reader *r)
{
	fstrm_res res;
	const struct fstrm_control *control = NULL;

	res = fstrm_reader_get_control(r, FSTRM_CONTROL_STOP, &control);
	if (res != fstrm_res_success)
		return res;
	fputs("FSTRM_CONTROL_STOP.\n", stderr);

	return fstrm_res_success;
}

static fstrm_res
print_data_frame(const uint8_t *data, size_t len_data)
{
	fprintf(stderr, "Data frame (%zd) bytes.\n", len_data);
	putchar(' ');
	print_string(data, len_data, stdout);
	putchar('\n');
	return fstrm_res_success;
}

int main(int argc, char **argv)
{
	const char *input_fname = NULL;
	const char *output_fname = NULL;

	fstrm_res res = fstrm_res_failure;
	struct fstrm_file_options *fopt = NULL;
	struct fstrm_writer_options *wopt = NULL;
	struct fstrm_reader *r = NULL;
	struct fstrm_writer *w = NULL;

	int rv = EXIT_FAILURE;

	/* Args. */
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "Usage: %s <INPUT FILE> [<OUTPUT FILE>]\n", argv[0]);
		fprintf(stderr, "Dumps a Frame Streams formatted input file.\n\n");
		return EXIT_FAILURE;
	}
	input_fname = argv[1];
	if (argc == 3)
		output_fname = argv[2];

	/* Line buffering. */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	/* Setup file reader options. */
	fopt = fstrm_file_options_init();
	fstrm_file_options_set_file_path(fopt, input_fname);

	/* Initialize file reader. */
	r = fstrm_file_reader_init(fopt, NULL);
	if (r == NULL) {
		fputs("Error: fstrm_file_reader_init() failed.\n", stderr);
		goto out;
	}
	res = fstrm_reader_open(r);
	if (res != fstrm_res_success) {
		fputs("Error: fstrm_reader_open() failed.\n", stderr);
		goto out;
	}

	if (output_fname != NULL) {
		/* Setup file writer options. */
		fstrm_file_options_set_file_path(fopt, output_fname);

		/* Setup writer options. */
		wopt = fstrm_writer_options_init();

		/* Copy "content type" from the reader's START frame. */
		res = process_start_frame(r, wopt);
		if (res != fstrm_res_success) {
			fputs("Error: process_start_frame() failed.\n", stderr);
			goto out;
		}

		/* Initialize file writer. */
		w = fstrm_file_writer_init(fopt, wopt);
		if (w == NULL) {
			fputs("Error: fstrm_file_writer_init() failed.\n", stderr);
			goto out;
		}
		res = fstrm_writer_open(w);
		if (res != fstrm_res_success) {
			fstrm_writer_destroy(&w);
			fputs("Error: fstrm_writer_open() failed.\n", stderr);
			goto out;
		}
	} else {
		/* Process the START frame. */
		res = process_start_frame(r, NULL);
		if (res != fstrm_res_success) {
			fprintf(stderr, "Error: process_start_frame() failed.\n");
			goto out;
		}
	}

	/* Loop over data frames. */
	for (;;) {
		const uint8_t *data;
		size_t len_data;

		res = fstrm_reader_read(r, &data, &len_data);
		if (res == fstrm_res_success) {
			/* Got a data frame. */
			res = print_data_frame(data, len_data);
			if (res != fstrm_res_success) {
				fprintf(stderr, "Error: print_data_frame() failed.\n");
				goto out;
			}
			if (w != NULL) {
				/* Write the data frame. */
				res = fstrm_writer_write(w, data, len_data);
				if (res != fstrm_res_success) {
					fprintf(stderr, "Error: write_data_frame() failed.\n");
					goto out;
				}
			}
		} else if (res == fstrm_res_stop) {
			/* Normal end of data stream. */
			res = print_stop_frame(r);
			if (res != fstrm_res_success) {
				fprintf(stderr, "Error: unable to read STOP frame.\n");
				goto out;
			}
			rv = EXIT_SUCCESS;
			break;
		} else {
			/* Abnormal end. */
			fprintf(stderr, "Error: fstrm_reader_read() failed.\n");
			goto out;
		}
	}

out:
	/* Cleanup options. */
	fstrm_file_options_destroy(&fopt);
	fstrm_writer_options_destroy(&wopt);

	/* Cleanup reader. */
	fstrm_reader_destroy(&r);

	/* Cleanup writer. */
	if (w != NULL) {
		res = fstrm_writer_close(w);
		if (res != fstrm_res_success) {
			fprintf(stderr, "Error: fstrm_writer_close() failed.\n");
			rv = EXIT_FAILURE;
		}
		fstrm_writer_destroy(&w);
	}

	return rv;
}
