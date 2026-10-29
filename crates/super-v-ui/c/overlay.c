#include "overlay.h"
#include <adwaita.h>
#include <stdio.h>

int super_v_ui_init(int *argc, char ***argv) {
    adw_init();
    return 1;
}

void super_v_ui_check_versions(char *buf, int max_len) {
    snprintf(buf, max_len, "GTK %d.%d.%d, Libadwaita %d.%d.%d",
             gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
             adw_get_major_version(), adw_get_minor_version(), adw_get_micro_version());
}
