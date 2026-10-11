#ifndef AMUN_FONT_PROFILES_H
#define AMUN_FONT_PROFILES_H

/* Three deliberately distinct ink profiles in the fixed 8x16 text grid. */
typedef struct {
    unsigned char latin_width, latin_height;
    unsigned char cjk_width, cjk_height;
} amun_font_profile_t;

static const amun_font_profile_t amun_font_profiles[3] = {
    {5, 8, 10, 10},   /* SMALL */
    {7, 13, 14, 14},  /* NORMAL */
    {8, 16, 16, 16}   /* LARGE */
};

#endif
