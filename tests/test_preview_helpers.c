#define _POSIX_C_SOURCE 200809L
#include "preview.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char template[] = "/tmp/xlf-preview-test-XXXXXX";
    char *dir = mkdtemp(template);
    assert(dir != NULL);

    char text_path[4096];
    snprintf(text_path, sizeof(text_path), "%s/sample.txt", dir);

    FILE *f = fopen(text_path, "w");
    assert(f != NULL);
    fputs("hello preview\\nsecond line\\n", f);
    fclose(f);

    assert(is_text_file("README.TXT"));
    assert(is_text_file("source.c"));
    assert(!is_text_file("image.png"));
    assert(is_html_file("index.HTML"));
    assert(is_pdf_file("document.pdf"));
    assert(is_mp3_file("track.MP3"));
    assert(is_media_file("movie.MP4"));
    assert(!is_media_file("notes.txt"));

    assert(is_small_image(text_path, 1024));
    assert(!is_small_image(text_path, 1));

    char *text = load_text_content(text_path, 1024);
    assert(text != NULL);
    assert(strcmp(text, "hello preview\\nsecond line\\n") == 0);
    free(text);

    assert(load_text_content("/path/that/does/not/exist", 1024) == NULL);

    assert(load_text_preview("not-an-allowed-tool", NULL, NULL, text_path) == NULL);
    assert(load_text_preview("pdfinfo", NULL, NULL, "/tmp/path\\nwith-control") == NULL);

    unlink(text_path);
    rmdir(dir);

    puts("preview helper tests: ok");
    return 0;
}
