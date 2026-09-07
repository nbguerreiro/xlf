#include "preview.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(detect_file_type("/path/that/does/not/exist", "photo.JPG") == FILE_TYPE_IMAGE);
    assert(detect_file_type("/path/that/does/not/exist", "README.md") == FILE_TYPE_TEXT);
    assert(detect_file_type("/path/that/does/not/exist", "index.HTML") == FILE_TYPE_HTML);
    assert(detect_file_type("/path/that/does/not/exist", "document.PdF") == FILE_TYPE_PDF);
    assert(detect_file_type("/path/that/does/not/exist", "track.MP3") == FILE_TYPE_MP3);
    assert(detect_file_type("/path/that/does/not/exist", "movie.MP4") == FILE_TYPE_MEDIA);
    assert(detect_file_type("/path/that/does/not/exist", "no-preview.unknown") == FILE_TYPE_UNKNOWN);
    assert(detect_file_type(NULL, NULL) == FILE_TYPE_UNKNOWN);

    puts("type detection tests: ok");
    return 0;
}
