#include "settings.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int parse_integer(const char *text, int *value)
{
    char *end;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (text == end || errno == ERANGE || parsed < 0 || parsed > 255)
        return -1;
    if (*end == '\r')
        end++;
    if (*end == '\n')
        end++;
    if (*end != '\0')
        return -1;
    *value = (int)parsed;
    return 0;
}

static void apply_line(struct settings *settings, char *line)
{
    char *separator;
    int value;
    int *destination;
    int default_value;

    separator = strchr(line, '=');
    if (!separator || strchr(separator + 1, '='))
        return;
    *separator = '\0';

    if (strcmp(line, "brightness_auto") == 0) {
        destination = &settings->brightness_auto;
        default_value = 1;
    } else if (strcmp(line, "brightness") == 0) {
        destination = &settings->brightness;
        default_value = 200;
    } else if (strcmp(line, "sleep_minutes") == 0) {
        destination = &settings->sleep_minutes;
        default_value = 5;
    } else {
        return;
    }

    *destination = default_value;
    if (parse_integer(separator + 1, &value) < 0)
        return;
    if ((destination == &settings->brightness_auto && value <= 1) ||
        (destination == &settings->brightness && value >= 8) ||
        (destination == &settings->sleep_minutes && settings_valid_sleep(value)))
        *destination = value;
}

static int write_all(int fd, const char *data, size_t length)
{
    while (length > 0) {
        ssize_t written = write(fd, data, length);

        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (written == 0)
            return -1;
        data += written;
        length -= (size_t)written;
    }
    return 0;
}

static char *parent_directory(const char *path)
{
    const char *slash = strrchr(path, '/');
    size_t length;
    char *directory;

    if (!slash)
        return strdup(".");
    length = slash == path ? 1u : (size_t)(slash - path);
    directory = malloc(length + 1);
    if (!directory)
        return NULL;
    memcpy(directory, path, length);
    directory[length] = '\0';
    return directory;
}

static int set_close_on_exec(int fd)
{
    int flags = fcntl(fd, F_GETFD);

    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void settings_defaults(struct settings *settings)
{
    settings->brightness_auto = 1;
    settings->brightness = 200;
    settings->sleep_minutes = 5;
}

int settings_valid_sleep(int minutes)
{
    return minutes == 0 || minutes == 1 || minutes == 5 || minutes == 15;
}

int settings_load(struct settings *settings, const char *path)
{
    char line[256];
    FILE *file;
    int complete = 1;

    settings_defaults(settings);
    file = fopen(path, "r");
    if (!file)
        return -1;

    while (fgets(line, sizeof line, file)) {
        size_t length = strlen(line);

        if (length > 0 && line[length - 1] != '\n' && !feof(file)) {
            int c;

            complete = 0;
            while ((c = fgetc(file)) != '\n' && c != EOF)
                ;
        } else {
            complete = 1;
        }
        if (complete)
            apply_line(settings, line);
    }
    if (ferror(file)) {
        fclose(file);
        settings_defaults(settings);
        return -1;
    }
    if (fclose(file) != 0) {
        settings_defaults(settings);
        return -1;
    }
    return 0;
}

int settings_save(const struct settings *settings, const char *path)
{
    char contents[128];
    char *directory = NULL;
    char *temporary = NULL;
    size_t template_length;
    int content_length;
    int directory_fd = -1;
    int fd = -1;
    int renamed = 0;
    int result = -1;

    content_length = snprintf(contents, sizeof contents,
                              "brightness_auto=%d\nbrightness=%d\n"
                              "sleep_minutes=%d\n",
                              settings->brightness_auto, settings->brightness,
                              settings->sleep_minutes);
    if (content_length < 0 || (size_t)content_length >= sizeof contents)
        return -1;

    directory = parent_directory(path);
    if (!directory)
        goto done;
    directory_fd = open(directory, O_RDONLY);
    if (directory_fd < 0 || set_close_on_exec(directory_fd) < 0)
        goto done;

    template_length = strlen(path) + sizeof ".tmp.XXXXXX";
    temporary = malloc(template_length);
    if (!temporary)
        goto done;
    if (snprintf(temporary, template_length, "%s.tmp.XXXXXX", path) < 0)
        goto done;

    fd = mkstemp(temporary);
    if (fd < 0 || set_close_on_exec(fd) < 0 || fchmod(fd, 0644) < 0 ||
        write_all(fd, contents, (size_t)content_length) < 0 || fsync(fd) < 0)
        goto done;
    if (close(fd) < 0) {
        fd = -1;
        goto done;
    }
    fd = -1;
    if (rename(temporary, path) < 0)
        goto done;
    renamed = 1;

    /* The replacement is durable only after its directory entry is synced. */
    if (fsync(directory_fd) < 0)
        goto done;
    result = 0;

done:
    if (fd >= 0)
        close(fd);
    if (temporary && !renamed)
        unlink(temporary);
    if (directory_fd >= 0 && close(directory_fd) < 0)
        result = -1;
    free(temporary);
    free(directory);
    return result;
}
