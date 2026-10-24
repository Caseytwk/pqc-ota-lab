/* PQClean randombytes for ESP32. Only key generation and signing use it,
 * the lab firmware only verifies. */
#include "randombytes.h"
#include "esp_random.h"

int randombytes(uint8_t *output, size_t n)
{
    esp_fill_random(output, n);
    return 0;
}
