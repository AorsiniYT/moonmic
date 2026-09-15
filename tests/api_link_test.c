#include "moonmic.h"

#include <string.h>

int main(void) {
    const char* version = moonmic_get_version();
    return version != NULL && strlen(version) > 0 ? 0 : 1;
}
