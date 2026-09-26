// SOURCES:
// The package must install the shell trap alongside, not as, a compiled tool.
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    const char *out = getenv("TEST_OUT");
    assert(out);
    char path[1024];
    snprintf(path, sizeof path, "%s/diag-target/usr/bin/panel-trap", out);
    // A previous passing run must not hide a missing install in this one.
    assert(unlink(path) == 0 || errno == ENOENT);
    assert(system("make --no-print-directory -f tests/control-centre/fixtures/diag-install.mk install-test") == 0);
    struct stat st;
    assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0755);
    FILE *installed = fopen(path, "rb");
    FILE *source = fopen("br2-external/package/taq102-diag/panel-trap", "rb");
    assert(installed && source);
    int byte;
    do {
        byte = fgetc(source);
        assert(fgetc(installed) == byte);
    } while (byte != EOF);
    fclose(installed);
    fclose(source);
    puts("diag_install ok");
    return 0;
}
