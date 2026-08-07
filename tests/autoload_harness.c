#include "offline_update.h"

#include <string.h>

int
main(int argc, char **argv) {
    if(argc == 4 && strcmp(argv[1], "remove") == 0) {
        return offline_remove_autoload_entry(argv[2], argv[3]) == 0 ? 0 : 1;
    }
    if(argc != 3) return 2;
    return offline_ensure_autoload_entry(argv[1], argv[2]) == 0 ? 0 : 1;
}
