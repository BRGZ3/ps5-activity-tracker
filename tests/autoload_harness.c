#include "offline_update.h"

#include <string.h>

int
main(int argc, char **argv) {
    int result;
    if(argc == 4 && strcmp(argv[1], "remove") == 0) {
        return offline_remove_autoload_entry(argv[2], argv[3]) == 0 ? 0 : 1;
    }
    if(argc != 3) return 2;
    result = offline_ensure_autoload_entry(argv[1], argv[2]);
    return result < 0 ? 2 : result;
}
