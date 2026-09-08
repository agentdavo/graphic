/* test_sndmin -- the audio half's pure maths and its song parser.
 *
 * The graphics side has six agreement checks, a GPU/CPU cull comparison and
 * 41 unit checks. The audio side had two WAV hashes from omega, and 266 lines
 * -- the whole of sndmin_song.c and sndmin_io.c -- that no run executed at
 * all. This closes the half of that gap which needs no audio file.
 *
 * Two things are covered, both chosen because they fail quietly:
 *
 *  - The DSP approximations. sndmin reimplements sqrt, exp2 and sin rather
 *    than calling libm, because libm's accuracy is unspecified and an ulp
 *    inside a feedback delay line is a different WAV. That reasoning is
 *    written in sndmin_dsp.h and was never checked: nothing said how close
 *    these actually are, so nobody could tell a correct one from a subtly
 *    broken one. The bounds below are the contract, and they are deliberately
 *    tight enough that a real mistake breaks them.
 *
 *  - The song parser and scheduler. A tracker format is parsed from a file,
 *    which is untrusted input, and the header claims "no partial success: a
 *    rejected song commits nothing". Both the accepting and the rejecting
 *    half of that are exercised here, and until now neither ever ran.
 *
 * Needs no sound card: sndmin's offline path renders to a WAV, which is the
 * property that makes the audio half testable anywhere at all.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sndmin.h"
#include "sndmin_dsp.h"

static int checks, errors;
static void check(bool ok, const char *what) {
    ++checks;
    if (!ok) { ++errors; printf("  FAIL %s\n", what); }
}

/* ------------------------------------------------------ the approximations -- */

static void test_dsp(void) {
    /* sqrt over the range a length can take, from a hair above zero to a very
     * large squared distance. Newton doubles its digits each pass and there
     * are five, so the tolerance is relative and small. */
    double worst_sqrt = 0;
    for (int i = 0; i <= 20000; ++i) {
        const float x = (float)(i * i) * 1e-3f + 1e-6f;
        const double got = snd_sqrt(x), want = sqrt((double)x);
        const double rel = fabs(got - want) / want;
        if (rel > worst_sqrt) worst_sqrt = rel;
    }
    check(worst_sqrt < 1e-6, "snd_sqrt is within 1e-6 relative over the audio range");

    /* The documented edge cases: zero and negatives return 0 rather than NaN,
     * because every caller is rooting a squared length and a NaN entering a
     * delay line never leaves it. */
    check(snd_sqrt(0.0f) == 0.0f, "snd_sqrt(0) is 0");
    check(snd_sqrt(-1.0f) == 0.0f, "snd_sqrt of a negative is 0, not NaN");
    check(snd_sqrt(4.0f) > 1.9999f && snd_sqrt(4.0f) < 2.0001f, "snd_sqrt(4) is 2");

    /* exp2 over the range a pitch or a decay can ask for. A cent is 2^(1/1200),
     * about 5.8e-4 relative, and the comment promises a fraction of a cent. */
    double worst_exp2 = 0;
    for (int i = -2400; i <= 2000; ++i) {
        const float x = (float)i * 0.01f;
        const double got = snd_exp2(x), want = pow(2.0, (double)x);
        const double rel = fabs(got - want) / want;
        if (rel > worst_exp2) worst_exp2 = rel;
    }
    check(worst_exp2 < 1e-4, "snd_exp2 is within 1e-4 relative, well under a cent");
    check(snd_exp2(0.0f) > 0.9999f && snd_exp2(0.0f) < 1.0001f, "snd_exp2(0) is 1");
    check(snd_exp2(10.0f) > 1023.0f && snd_exp2(10.0f) < 1025.0f, "snd_exp2(10) is 1024");

    /* The clamp is not cosmetic: without it an extreme argument assembles an
     * exponent field out of range and the result is a bit pattern rather than
     * a number. Saturation must stay finite and positive at both ends. */
    check(isfinite(snd_exp2(-1000.0f)) && snd_exp2(-1000.0f) > 0.0f, "snd_exp2 saturates finite and positive when tiny");
    check(isfinite(snd_exp2(1000.0f)), "snd_exp2 saturates finite when huge");

    /* sin takes cycles, not radians, so a whole number of cycles is a zero and
     * a quarter cycle is the peak. Checked against libm at the same points. */
    double worst_sin = 0;
    for (int i = 0; i <= 40000; ++i) {
        const float cycles = (float)i * 1e-4f - 2.0f;   /* -2 .. +2 cycles */
        const double got = snd_sin(cycles), want = sin((double)cycles * 6.283185307179586);
        const double err = fabs(got - want);
        if (err > worst_sin) worst_sin = err;
    }
    check(worst_sin < 1e-5, "snd_sin is within 1e-5 absolute over four cycles");
    check(fabsf(snd_sin(0.0f)) < 1e-6f, "snd_sin(0) is 0");
    check(snd_sin(0.25f) > 0.9999f, "a quarter cycle is the positive peak");
    check(snd_sin(0.75f) < -0.9999f, "three quarters is the negative peak");

    /* Noise is a pure function of position, which is what lets a stolen voice
     * resume identically and a replay reproduce bit for bit. */
    check(snd_noise(12345, 7) == snd_noise(12345, 7), "noise is a pure function of sample and salt");
    check(snd_noise(12345, 7) != snd_noise(12345, 8), "a different salt gives a different stream");
    bool noise_in_range = true;
    double sum = 0;
    for (uint64_t i = 0; i < 20000; ++i) {
        const float n = snd_noise(i, 3);
        if (!(n >= -1.0f && n < 1.0f)) noise_in_range = false;
        sum += (double)n;
    }
    check(noise_in_range, "noise stays inside [-1,1)");
    check(fabs(sum / 20000.0) < 0.02, "noise is centred, so a wall of it does not drift the mix");
}

/* --------------------------------------------------------- the song parser -- */

static bool write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fputs(text, f);
    return fclose(f) == 0;
}

/* A minimal but complete song: two patches, an order, one pattern of four
 * rows, and a rest. Every field the grammar has, at its smallest. */
static const char *const GOOD_SONG =
    "# a test song\n"
    "tempo 120 0.0\n"
    "rows 4\n"
    "patch 1 bass\n"
    "patch 2 lead\n"
    "order 0 0\n"
    "pattern 0\n"
    "C4:1:100:0 --- \n"
    "--- E4:2:80:0 \n"
    "G4:1:60:0 --- \n"
    "--- --- \n";

static void test_song(void) {
    sndmin_ctx *c = sndmin_init(&(sndmin_desc){.offline = true});
    check(c != NULL, "an offline context needs no sound card");
    if (!c) return;

    check(write_file("test_song_good.song", GOOD_SONG), "wrote the test song");
    const sndmin_song song = sndmin_load_song(c, "test_song_good.song");
    check(song.id != 0, "a well-formed song loads");

    /* The rejecting half of "no partial success". Each of these breaks one
     * documented rule, and each must be refused outright. */
    static const struct { const char *name, *text; } bad[] = {
        {"tempo out of range", "tempo 5 0.0\nrows 4\npatch 1 bass\norder 0\npattern 0\nC4:1:100:0\n"},
        {"swing at the limit", "tempo 120 0.9\nrows 4\npatch 1 bass\norder 0\npattern 0\nC4:1:100:0\n"},
        {"a flat, which the parser does not accept", "tempo 120 0\nrows 4\npatch 1 bass\norder 0\npattern 0\nCb4:1:100:0\n"},
        {"a note with no octave", "tempo 120 0\nrows 4\npatch 1 bass\norder 0\npattern 0\nC:1:100:0\n"},
        {"an octave past 8", "tempo 120 0\nrows 4\npatch 1 bass\norder 0\npattern 0\nC9:1:100:0\n"},
        {"an unknown patch name", "tempo 120 0\nrows 4\npatch 1 tuba\norder 0\npattern 0\nC4:1:100:0\n"},
        {"a note naming an undeclared patch", "tempo 120 0\nrows 4\npatch 1 bass\norder 0\npattern 0\nC4:7:100:0\n"},
        {"a volume over 100", "tempo 120 0\nrows 4\npatch 1 bass\norder 0\npattern 0\nC4:1:250:0\n"},
        {"more rows than declared", "tempo 120 0\nrows 1\npatch 1 bass\norder 0\npattern 0\nC4:1:100:0\nC4:1:100:0\n"},
    };
    uint32_t refused = 0;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        if (!write_file("test_song_bad.song", bad[i].text)) continue;
        const sndmin_song s = sndmin_load_song(c, "test_song_bad.song");
        if (s.id == 0) ++refused;
        else printf("  FAIL accepted a song with %s\n", bad[i].name);
    }
    check(refused == sizeof bad / sizeof bad[0], "every malformed song is refused");
    check(sndmin_load_song(c, "no_such_file.song").id == 0, "a missing file is refused, not a crash");
    check(sndmin_ok(c), "a rejected song is recoverable, not a terminal failure");

    /* Scheduling and rendering it: this is the first time sndmin_song.c has
     * produced samples. A song is scheduled in full at play time, so the
     * output must be non-silent and must be the same twice. */
    sndmin_play(c, &(sndmin_play_desc){.song = song});
    sndmin_frame(c, &(sndmin_frame_desc){0});
    check(sndmin_render(c, 60, "test_song_a.wav", NULL), "the song renders offline");
    sndmin_shutdown(c);

    sndmin_ctx *d = sndmin_init(&(sndmin_desc){.offline = true});
    if (d) {
        const sndmin_song again = sndmin_load_song(d, "test_song_good.song");
        sndmin_play(d, &(sndmin_play_desc){.song = again});
        sndmin_frame(d, &(sndmin_frame_desc){0});
        check(sndmin_render(d, 60, "test_song_b.wav", NULL), "and renders a second time");
        sndmin_shutdown(d);
    }

    /* Byte equality between two independent runs is the same property the
     * graphics side checks with two PNGs. */
    long size_a = 0, size_b = 0;
    bool identical = true, silent = true;
    FILE *fa = fopen("test_song_a.wav", "rb"), *fb = fopen("test_song_b.wav", "rb");
    if (fa && fb) {
        int ca, cb;
        long at = 0;
        while ((ca = fgetc(fa)) != EOF && (cb = fgetc(fb)) != EOF) {
            if (ca != cb) identical = false;
            if (at > 44 && ca != 0) silent = false;   /* past the WAV header */
            ++at;
        }
        size_a = at;
        fseek(fb, 0, SEEK_END);
        size_b = ftell(fb);
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    check(size_a > 44 && size_a == size_b, "both renders produced the same number of bytes");
    check(identical, "two independent renders of one song are bit-identical");
    check(!silent, "the song actually made sound, so the scheduler ran");

    remove("test_song_good.song");
    remove("test_song_bad.song");
    remove("test_song_a.wav");
    remove("test_song_b.wav");
}

int main(void) {
    test_dsp();
    test_song();
    printf("sndmin: %d checks, %d failures\n", checks, errors);
    return errors ? 1 : 0;
}
