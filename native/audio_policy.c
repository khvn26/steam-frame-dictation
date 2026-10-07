#include "app.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static bool wpctl_get_default_sink_volume(double *out) {
    FILE *f = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
    if (!f) return false;
    char line[128] = {0};
    bool ok = false;
    if (fgets(line, sizeof(line), f)) {
        char *p = strstr(line, "Volume:");
        if (p) {
            p += strlen("Volume:");
            while (*p && isspace((unsigned char)*p)) p++;
            char *end = NULL;
            double v = strtod(p, &end);
            if (end && end != p) { *out = v; ok = true; }
        }
    }
    pclose(f);
    return ok;
}

static void wpctl_set_default_sink_volume(double volume) {
    if (volume < 0.0) volume = 0.0;
    if (volume > 1.5) volume = 1.5;
    char vol[32];
    snprintf(vol, sizeof(vol), "%.3f", volume);
    pid_t pid = fork();
    if (pid == 0) {
        execlp("wpctl", "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", vol, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) {
        int st;
        waitpid(pid, &st, 0);
    }
}

void audio_policy_begin(app *a) {
    a->has_saved_volume = false;
    if (!a->duck_enabled) return;
    double current = 0.0;
    if (!wpctl_get_default_sink_volume(&current)) {
        fprintf(stderr, "duck warning: could not read default sink volume\n");
        return;
    }
    a->saved_volume = current;
    a->has_saved_volume = true;
    if (current > a->duck_volume) {
        fprintf(stderr, "duck: %.2f -> %.2f\n", current, a->duck_volume);
        wpctl_set_default_sink_volume(a->duck_volume);
    }
}

void audio_policy_end(app *a) {
    if (!a->duck_enabled || !a->has_saved_volume) return;
    fprintf(stderr, "duck restore: %.2f\n", a->saved_volume);
    wpctl_set_default_sink_volume(a->saved_volume);
    a->has_saved_volume = false;
}
