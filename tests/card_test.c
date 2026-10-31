/* Checks of the memory card emulation: the BIOS file system on a card image (create, write, read back
 * across a block, list, delete) and the card events the game waits on. */
#include "psx/libapi.h"
#include "psx/libcard.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
            failures++;                                 \
        }                                               \
    } while (0)

int main(void)
{
    static unsigned char out[0x4000], in[0x4000];
    struct DIRENTRY dir;
    card_set_image_paths(NULL, NULL);

    long ioe = OpenEvent(SwCARD, EvSpIOE, 0x2000, NULL);
    long timeout = OpenEvent(SwCARD, EvSpTIMOUT, 0x2000, NULL);
    EnableEvent(ioe);
    EnableEvent(timeout);
    _card_info(0x00);
    CHECK(TestEvent(ioe) == 1 && TestEvent(timeout) == 0, "slot 1 has a card");
    CHECK(TestEvent(ioe) == 0, "an event is reported once");
    _card_info(0x10);
    CHECK(TestEvent(timeout) == 1, "slot 2 is empty");

    CHECK(firstfile("bu00:*", &dir) == NULL, "a new card is empty");
    int fd = open("bu00:BASCUS-94350TEST", FCREAT | 2 << 16);
    CHECK(fd >= 0, "create two blocks");
    close(fd);
    CHECK(open("bu00:BASCUS-94350TEST", FCREAT | 1 << 16) < 0, "create fails when the file exists");
    for (int i = 0; i < (int)sizeof(out); i++)
        out[i] = (unsigned char)(i * 7 + i / 256);
    fd = open("bu00:BASCUS-94350TEST", FWRITE);
    CHECK(write(fd, out, 0x2000 + 0x80) == 0x2080, "write across the first block");
    CHECK(write(fd, out + 0x2080, 0x100) == 0x100, "write goes on where it stopped");
    close(fd);
    fd = open("bu00:BASCUS-94350TEST", FREAD);
    CHECK(read(fd, in, 0x2180) == 0x2180 && memcmp(in, out, 0x2180) == 0, "read back the same bytes");
    CHECK(read(fd, in, 0x40) < 0, "reads are in 128-byte frames");
    close(fd);

    CHECK(firstfile("bu00:*", &dir) != NULL && strcmp(dir.name, "BASCUS-94350TEST") == 0 && dir.size == 0x4000,
          "the listing shows the file and its size");
    CHECK(nextfile(&dir) == NULL, "and nothing else");
    CHECK(delete("bu00:BASCUS-94350TEST") == 1, "delete");
    CHECK(firstfile("bu00:*", &dir) == NULL, "the card is empty again");
    CHECK(open("bu00:BASCUS-94350TEST", FREAD) < 0, "a deleted file cannot be opened");

    printf("%s\n", failures ? "card test FAILED" : "card test passed");
    return failures != 0;
}
