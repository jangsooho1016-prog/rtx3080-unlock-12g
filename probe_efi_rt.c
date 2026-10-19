/* Minimal freestanding runtime for the standalone probe EFI build only.
 * The compiler may lower struct initialisation/copies to these calls. */
#include <stddef.h>
void *memset(void *Dst, int Value, size_t Bytes) {
  volatile unsigned char *D = (volatile unsigned char *)Dst;
  while (Bytes--) *D++ = (unsigned char)Value;
  return Dst;
}
void *memcpy(void *Dst, const void *Src, size_t Bytes) {
  volatile unsigned char *D = (volatile unsigned char *)Dst;
  const unsigned char *S = (const unsigned char *)Src;
  while (Bytes--) *D++ = *S++;
  return Dst;
}
